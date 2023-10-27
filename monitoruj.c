/********************************************************************
* FILE NAME: monitoruj.c                                            *
*                                                                   *
* PURPOSE: Uruchamia demona z odpowiednimi argumentami podanymi     *
*          przez użytkownika w celu monitorowania zmian w zadanym   *
*          katalogu.                                                *
********************************************************************/

#define _POSIX_SOURCE // SIG_BLOCK
#define _DEFAULT_SOURCE //scandir, alpahsort
#include <stdio.h> 
#include <stdlib.h> // realpath
#include <signal.h> //sig_atomic_t typ
#include <getopt.h>
#include <sys/types.h> // pid_t
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h> //getuid, pid_t
#include <syslog.h> //openlog, syslog
#include <string.h> // strerror, perror
#include <errno.h> //do uzywania zmiennej errno (wyluskiwanie kodu bledu, makra bledow)
//limits.h
#include <setjmp.h>
#include <dirent.h>
#include <assert.h>
#include <fcntl.h>
#include <utime.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sys/sendfile.h>

int ilosc_prob = 0;

jmp_buf powrot;

int isDir(char *string_to_check);

int PodajIloscWpisowWKatalogu(char *katalog);

int PodajIloscZwyklychPlikowWKatalogu(char *katalog);

struct dirent **PobierzWpisyZKatalogu(char *katalog, struct dirent **zawartosc_katalogu);

struct dirent ** PobierzTylkoPlikiZwykleZKatalogu (char *katalog, struct dirent **zawartosc_katalogu);

int porownajPliki(int fd1, int fd2);

int skopiujPlikNiskopoziomowo(int fd_zrodlo, int fd_cel);

int skopiujPlikEfektywnie(int fd_zrodlo, int fd_cel);

void handler(int signum)
{
    longjmp(powrot, 1);
}

int main (int argc, char **argv)
{
    int blad;

    seteuid(getuid());
    setegid(getgid());

    pid_t pid, sid;
    
    /*
     * Odforkowanie procesu.
     * Jesli sie uda: proces_rodzica.pid = pid procesu dziecka (>0),
     *                proces_dziecka.pid = 0;
     * Jesli sie nie uda: proces_rodzica.pid = -1, brak procesu dziecka.
    */

    pid = fork();

    if (pid < 0)
        exit(EXIT_FAILURE); // nie udalo sie odforkowac

    if (pid > 0)
        exit(EXIT_SUCCESS); // udalo sie odforkowac, zamkniecie procesu rodzica

    umask(0);

    openlog("Demon_monitorujacy", LOG_PERROR | LOG_PID | LOG_NDELAY, 
                                0); // logowanie do syslogu

    sid = setsid(); // przypisanie procesu potomnego do sesji
    if (sid < 0) 
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_ERR), "Przypisanie procesowi sesji: %s (exit)", 
                                                                strerror(blad));
            exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) // zmiana katalogu roboczego na jedyny na pewno istniejacy
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_ERR), "Zmiana katalogu na /: %s (exit)", 
                                                        strerror(blad));
        exit(EXIT_FAILURE);
    }

    /* Demon nie powinien komunikowac sie standardowo z uzytkownikiem */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "-----------------------");
    syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE),
        "Rozpoczecie dzialania demona");
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "-----------------------");

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "SID procesu demona: %d", 
                                                            sid);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Katalog roboczy: /");

    /* Deklaracja zmiennych na argumenty inicjalizacyjne */

    static int podano_katalog_zrodlowy_flaga = 0;    /* flaga opcji s */
    static char *katalog_zrodlowy;                   /* wartosc opcji s */

    static int podano_katalog_docelowy_flaga = 0;    /* flaga opcji t */
    static char *katalog_docelowy;                   /* wartosc opcji t */

    static int synchronizuj_rowniez_katalogi = 0;    /* flaga opcji R */

    static int czas_spania_demona = 300;     /* w sekundach */

    static float prog_dozego_pliku_w_MB = 10.0;

    int processed_argument;     /* przetwarzany przez getopt_long() */

    /******************************/

    /* wczytaj opcje i argumenty */
    
    while (1)
    {
        /* lista możliwych dlugich opcji i argumentow */
        static struct option long_options[] =
        {
            {"source", required_argument, 0, 's'},
            {"target", required_argument, 0, 't'},
            {"sleeptime", required_argument, 0, 'e'},
            {"prog", required_argument, 0, 'p'},
            {0, 0, 0, 0}
        };
        int long_option_index = 0;
        
        processed_argument = getopt_long(argc, argv, "Rs:t:e:p:",
                                          long_options, &long_option_index);
        
        if (processed_argument == -1)
            break;

        /* processed argument przyjmuje wartosci R, s, t, e oraz p */
        switch (processed_argument)
        {
            case 0:
                printf("weszlo do case 0");
                break;
            case 'R':
                synchronizuj_rowniez_katalogi = 1;
                break;
            case 's':
                podano_katalog_zrodlowy_flaga = 1;
                katalog_zrodlowy = optarg;
                break;
            case 't':
                podano_katalog_docelowy_flaga = 1;
                katalog_docelowy = optarg;
                break;
            case 'e':
                sscanf(optarg, "%d", &czas_spania_demona);
                break;
            case 'p':
                sscanf(optarg, "%f", &prog_dozego_pliku_w_MB);
                break;
            case '?':
                break;
            default:
                abort(); //trzeba to lepiej obsluzyc
        }
    }

    /* Walidacja argumentow */

    if (czas_spania_demona <= 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Czas spania demona musi byc dodatni. (exit)");
        exit(EXIT_FAILURE);
    }

    if (prog_dozego_pliku_w_MB <= 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Wielkosc progowa dozego pliku musi byc dodatnia. (exit)");
        exit(EXIT_FAILURE);
    }

    if (podano_katalog_zrodlowy_flaga == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Nalezy podac katalog zrodlowy. (exit)");
        exit(EXIT_FAILURE);
    }

    if (podano_katalog_docelowy_flaga == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Nalezy podac katalog docelowy. (exit)");
        exit(EXIT_FAILURE);
    }

    if ( isDir(katalog_zrodlowy) == -1 )
        exit(EXIT_FAILURE); //blad zalogowany w funkcji isDir

    if ( access(katalog_zrodlowy, R_OK | X_OK) == -1 )
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", katalog_zrodlowy
                                                        , strerror(blad));
        exit(EXIT_FAILURE);
    }

    if ( isDir(katalog_docelowy) == -1)
        exit(EXIT_FAILURE); //blad zalogowany w funkcji isDir

    if ( access(katalog_docelowy, R_OK | W_OK | X_OK) == -1 )
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", katalog_docelowy
                                                        , strerror(blad));
        exit(EXIT_FAILURE);
    }

    if (strcmp(katalog_docelowy, katalog_zrodlowy) == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Katalog zrodlowy i katalog docelowy nie moga byc takie same");
        exit(EXIT_FAILURE);
    }

    /* Nadmiarowe argumenty */

    if (optind < argc)
    {
        int ilosc_blednych_argumentow = argc - optind;
        char *bledne_argumenty[ilosc_blednych_argumentow];

        /* zapisz pominiete argumenty do tablicy */
        
        for (int i = 0; i < ilosc_blednych_argumentow; ++i)
            bledne_argumenty[i] = argv[optind++];

        /* zaloguj pominiete argumenty */
        
        for (int i = 0; i < ilosc_blednych_argumentow; ++i)
            syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                    "Pominieto bledny argument: %s", bledne_argumenty[i]);
        printf("\n");
    }

    /* Uzyskanie bezwglednych sciezek katalogow */

    realpath(katalog_zrodlowy, katalog_zrodlowy);
    realpath(katalog_docelowy, katalog_docelowy);

    /* Bledy realpath() pokrywaja sie z juz sprawdzanymi bledami stat()*/

    if(katalog_docelowy == NULL)
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", katalog_zrodlowy
                                                        , strerror(blad));
        exit(EXIT_FAILURE);
    }
    if(katalog_zrodlowy == NULL)
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", katalog_docelowy
                                                        , strerror(blad));
        exit(EXIT_FAILURE);
    }

    /* zalogowanie poprawnych argumentow wejsciowych */

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "-----------------------");
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), 
                    "Wszystkie argumenty inicjalizacyjne sa poprawne.");
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), 
                "Katalog zrodlowy: %s", katalog_zrodlowy);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), 
                "Katalog docelowy: %s", katalog_docelowy);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), 
                "Synchronizuj podkatalogi: %d", synchronizuj_rowniez_katalogi);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), 
                "Czas spania demona[s]: %d", czas_spania_demona);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), 
                "Prog dozego pliku[MB]: %f", prog_dozego_pliku_w_MB);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "-----------------------");
    
    /* blokuj SIGUSR1 dopuki demon nie spi */

    sigset_t sygnaly_do_blokowania;
    sigemptyset(&sygnaly_do_blokowania); // wyzerowanie maski
    if (sigaddset(&sygnaly_do_blokowania, SIGUSR1) == -1) //dodaj SIGUSR1 do maski
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_WARNING), "%s (exit)", strerror(blad));
        exit(EXIT_FAILURE);
    }

    /* Stworz katalog o tej samej nazwie w katalogu docelowym */
	
	char *nazwa_katalogu_zapisu_tmp = strrchr(katalog_zrodlowy, '/'); // nazwa to /TOCOCHCE
	char nazwa_katalogu_zapisu[strlen(nazwa_katalogu_zapisu_tmp) - 1];
	strcpy(nazwa_katalogu_zapisu, nazwa_katalogu_zapisu_tmp + 1);
	nazwa_katalogu_zapisu_tmp = NULL;
	chdir(katalog_docelowy);
	if ( mkdir(nazwa_katalogu_zapisu, S_IRWXU) == -1 ) // u+rwx
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s/%s %s",
    katalog_docelowy, nazwa_katalogu_zapisu, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    /* okresl sciezke stworzonego katalogu */

    int dlugosc_sciezki_stworzonego_katalogu = strlen(katalog_docelowy) + strlen(nazwa_katalogu_zapisu) + 2;
    char sciezka_stworzonego_katalogu[dlugosc_sciezki_stworzonego_katalogu];
    snprintf(sciezka_stworzonego_katalogu, sizeof(sciezka_stworzonego_katalogu), "%s/%s", katalog_docelowy, nazwa_katalogu_zapisu);

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Stworzenie katalog zapisu %s", sciezka_stworzonego_katalogu);

    /* Odczytaj zawartosc katalogu zrodlowego (tylko pliki zwykle) */

    int ilosc_wpisow_w_katalogu = PodajIloscZwyklychPlikowWKatalogu(katalog_zrodlowy);
    struct dirent **zawartosc_katalogu_zrodlowego = malloc(sizeof(struct dirent*)*ilosc_wpisow_w_katalogu);
    zawartosc_katalogu_zrodlowego = PobierzTylkoPlikiZwykleZKatalogu(katalog_zrodlowy, zawartosc_katalogu_zrodlowego);

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Odczytano zawartosc katalogu %s", katalog_zrodlowy);

    /* 
    *  dla kazdego wpisu w katalogu okresl sciezke do pliku i sciezke do kopiowania,
    *  otworz deskryptory z okreslonych sciezek i zmien date dostepu i modyfikacji
    */

    int dlugosc_nazwy_pliku_zrodlo, dlugosc_nazwy_pliku_cel;
    int zrodlowy_fd, docelowy_fd;
    for (int j=0; j < ilosc_wpisow_w_katalogu; ++j) //dla kazdego wpisu w katalogu
    {
        /* okresl sciezke pliku zrodlowego */

        dlugosc_nazwy_pliku_zrodlo = strlen(katalog_zrodlowy) + strlen(zawartosc_katalogu_zrodlowego[j]->d_name) + 2;
        char sciezka_pliku_zrodlo[dlugosc_nazwy_pliku_zrodlo];
        snprintf(sciezka_pliku_zrodlo, sizeof(sciezka_pliku_zrodlo), "%s/%s", katalog_zrodlowy, zawartosc_katalogu_zrodlowego[j]->d_name);
        
        /* okresl sciezke pliku docelowego */

        dlugosc_nazwy_pliku_cel = strlen(sciezka_stworzonego_katalogu) + strlen(zawartosc_katalogu_zrodlowego[j]->d_name) + 2;
        char sciezka_pliku_cel[dlugosc_nazwy_pliku_cel];
        snprintf(sciezka_pliku_cel, sizeof(sciezka_pliku_cel), "%s/%s", sciezka_stworzonego_katalogu, zawartosc_katalogu_zrodlowego[j]->d_name);

        /* otworz plik zrodlowy */

        zrodlowy_fd = open(sciezka_pliku_zrodlo, O_RDONLY);//otworz deskryptor p. zrodlowego
        if(zrodlowy_fd == -1)
            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s %s", sciezka_pliku_zrodlo, strerror(errno));
        
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Otworcie pliku %s\n", sciezka_pliku_zrodlo);
        
        /* kopiowanie daty modyfikacji i dostepu pliku zrodlowego */

        struct stat st_zrodlowy;
        fstat(zrodlowy_fd, &st_zrodlowy);
        struct utimbuf czasy;
        czasy.actime = st_zrodlowy.st_atim.tv_sec;
        czasy.modtime = st_zrodlowy.st_mtim.tv_sec;

        /* otworz plik docelowy */

        docelowy_fd = open(sciezka_pliku_cel, O_WRONLY | O_CREAT | O_EXCL, S_IRWXU); // u+rwx
        if(docelowy_fd == -1)
            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s %s", sciezka_pliku_cel, strerror(errno));
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Stworzenie i otwarcie pliku %s\n", sciezka_pliku_cel);
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "------------------------------------");
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Kopiuje plik o wadze %ld bajtow", st_zrodlowy.st_size);


        if (st_zrodlowy.st_size < (prog_dozego_pliku_w_MB * 1000000))
            skopiujPlikNiskopoziomowo(zrodlowy_fd, docelowy_fd);
        else
            skopiujPlikEfektywnie(zrodlowy_fd, docelowy_fd);

        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Skopiowano plik %s do %s",
        sciezka_pliku_zrodlo, sciezka_pliku_cel);
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "------------------------------------");
        
        /* zamnkij deskryptory plikow */
        
        close(zrodlowy_fd);
        close(docelowy_fd);

        /* ustaw odpowiedni m_time i a_time w skopiowanym pliku */

        utime(sciezka_pliku_cel, &czasy);
    }
    free(zawartosc_katalogu_zrodlowego); //zwolnij pamiec tablicy wpisow z katalogu

    /* deklaracja roboczych zmiennych do petli demona */

    dlugosc_nazwy_pliku_cel = 0;
    dlugosc_nazwy_pliku_zrodlo = 0;
    int fd_zrodlowy, fd_zapisu; //deskryptory do operowanych plikow
    int czy_pierwsza_iteracja = 1;

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "---------------------------");
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Wejscie do petli demona");
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "---------------------------");

    signal(SIGUSR1, handler); //obsluguj sygnal SIGUSR1
    
    while (1)
    {
        if (setjmp(powrot) != 0) // jesli nie weszlo tu z longjump to setjmp zwraca 0
        {
            signal(SIGUSR1, handler); //obsluguj sygnal SIGUSR1
            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO),
            "Przebudzenie demona - sygnal SIGUSR1");
        }

        /* blokuj sygnal SIGUSR1 do momentu uspienia demona */
        
        sigprocmask (SIG_BLOCK, &sygnaly_do_blokowania, NULL);

        if(czy_pierwsza_iteracja == 0) //za pierwszym razem przejdz do uspienia
        {
        
        /* Tryb bez synchronizacji katalogow */

        if (synchronizuj_rowniez_katalogi == 0)
        {
            /* Odczytaj zawartosc katalogu zrodlowego (tylko pliki zwykle) */

            int ilosc_wpisow_w_katalogu_zrodlowym = PodajIloscZwyklychPlikowWKatalogu(katalog_zrodlowy);
            struct dirent **zawartosc_katalogu_zrodlowego = malloc(sizeof(struct dirent*)*ilosc_wpisow_w_katalogu_zrodlowym);
            zawartosc_katalogu_zrodlowego = PobierzTylkoPlikiZwykleZKatalogu(katalog_zrodlowy, zawartosc_katalogu_zrodlowego);

            /* Odczytaj zawartosc katalogu zapisu (tylko pliki zwykle) */

            int ilosc_wpisow_w_katalogu_zapisu = PodajIloscZwyklychPlikowWKatalogu(sciezka_stworzonego_katalogu);
            struct dirent **zawartosc_katalogu_zapisu = malloc(sizeof(struct dirent*)*ilosc_wpisow_w_katalogu_zapisu);
            zawartosc_katalogu_zapisu = PobierzTylkoPlikiZwykleZKatalogu(sciezka_stworzonego_katalogu, zawartosc_katalogu_zapisu);

            /* Porownaj zawartosc katalogow */
            
            /* Sprawdz czy jakis plik nie zniknal */

            for(int i = 0; i < ilosc_wpisow_w_katalogu_zapisu; ++i)
            {
                /* dla kazdego pliku zwyklego w katalogu zapisu */

                int czy_znaleziono_plik = 0;

                for(int j = 0; j < ilosc_wpisow_w_katalogu_zrodlowym; ++j)
                {
                    /* sprawdz czy plik nadal znajduje sie w katalogu zrodlowym */
                    if(strcmp(zawartosc_katalogu_zrodlowego[j]->d_name, zawartosc_katalogu_zapisu[i]->d_name) == 0)
                        czy_znaleziono_plik = 1;
                }

                /* 
                *  jesli w katalogu zrodlowym usunieto plik i - zalogowanie tego i
                *  usuniecie pliku z katalogu zapisu
                */

                if (czy_znaleziono_plik == 0)
                {
                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "W katalogu zrodlowym usunieto plik %s",
                    zawartosc_katalogu_zapisu[i]->d_name);

                    /* okresl sciezke pliku zapisu */

                    dlugosc_nazwy_pliku_cel = strlen(sciezka_stworzonego_katalogu) + strlen(zawartosc_katalogu_zapisu[i]->d_name) + 2;
                    char sciezka_pliku_cel[dlugosc_nazwy_pliku_cel];
                    snprintf(sciezka_pliku_cel, sizeof(sciezka_pliku_cel), "%s/%s", sciezka_stworzonego_katalogu, zawartosc_katalogu_zapisu[i]->d_name);

                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Usuwam plik %s z katalogu zapisu",
                    zawartosc_katalogu_zapisu[i]->d_name);

                    remove(sciezka_pliku_cel);
                }
            }

            /* Sprawdz czy dodano jakis plik */

            for(int i = 0; i < ilosc_wpisow_w_katalogu_zrodlowym; ++i)
            {
                /* dla kazdego pliku zwyklego w katalogu zrodlowym */

                int czy_znaleziono_plik = 0;

                for(int j = 0; j < ilosc_wpisow_w_katalogu_zapisu; ++j)
                {
                    /* sprawdz czy plik znajduje sie w katalogu zapisu */
                    if(strcmp(zawartosc_katalogu_zrodlowego[i]->d_name, zawartosc_katalogu_zapisu[j]->d_name) == 0)
                        czy_znaleziono_plik = 1;
                }

                /* 
                *  jesli w katalogu zapisu nie ma pliku i - zalogowanie tego i
                *  dodanie pliku do katalogu zapisu
                */

               if (czy_znaleziono_plik == 0)
               {
                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "W katalogu zrodlowym dodano plik %s",
                    zawartosc_katalogu_zrodlowego[i]->d_name);

                    /* okresl sciezke pliku zrodlowego */
                        
                    dlugosc_nazwy_pliku_zrodlo = strlen(katalog_zrodlowy) + strlen(zawartosc_katalogu_zrodlowego[i]->d_name) + 2;
                    char sciezka_pliku_zrodlo[dlugosc_nazwy_pliku_zrodlo];
                    snprintf(sciezka_pliku_zrodlo, sizeof(sciezka_pliku_zrodlo), "%s/%s", katalog_zrodlowy, zawartosc_katalogu_zrodlowego[i]->d_name);

                    /* okresl sciezke pliku zapisu */

                    dlugosc_nazwy_pliku_cel = strlen(sciezka_stworzonego_katalogu) + strlen(zawartosc_katalogu_zrodlowego[i]->d_name) + 2;
                    char sciezka_pliku_cel[dlugosc_nazwy_pliku_cel];
                    snprintf(sciezka_pliku_cel, sizeof(sciezka_pliku_cel), "%s/%s", sciezka_stworzonego_katalogu, zawartosc_katalogu_zrodlowego[i]->d_name);

                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Dodaje plik %s do katalogu zapisu",
                    zawartosc_katalogu_zrodlowego[i]->d_name);

                    fd_zrodlowy = open(sciezka_pliku_zrodlo, O_RDONLY); // des. p. zrod.
                    if(fd_zrodlowy == -1)
                        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s %s", sciezka_pliku_cel, strerror(errno));

                    /* otworz plik zapisu */

                    fd_zapisu = open(sciezka_pliku_cel, O_WRONLY | O_CREAT | O_EXCL, S_IRWXU); // u+rwx otworz deskryptor p. zapisu
                    if(fd_zapisu == -1)
                        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s %s", sciezka_pliku_cel, strerror(errno));
                    
                    struct stat st_zrodlowego;
                    fstat(fd_zrodlowy, &st_zrodlowego);

                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Stworzenie i otwarcie pliku %s w katalogu zapisu", sciezka_pliku_cel);
                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "------------------------------------");
                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Kopiuje plik o wadze %ld bajtow", st_zrodlowego.st_size);

                    if (st_zrodlowego.st_size < (prog_dozego_pliku_w_MB * 1000000))
                        skopiujPlikNiskopoziomowo(zrodlowy_fd, docelowy_fd);
                    else
                        skopiujPlikEfektywnie(zrodlowy_fd, docelowy_fd);

                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Skopiowano plik %s do %s",
                    zawartosc_katalogu_zrodlowego[i]->d_name, sciezka_pliku_cel);
                    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "------------------------------------");

                    close(fd_zrodlowy);
                    close(fd_zapisu);

               }
            }

            /* Dla plikow istniejacych w obu katalogach sprawdz, czy sa takie same */

            for(int i = 0; i < ilosc_wpisow_w_katalogu_zrodlowym; ++i)
            {
                /* dla kazdego pliku zwyklego i w katalogu zrodlowym */

                for(int j = 0; j < ilosc_wpisow_w_katalogu_zapisu; ++j)
                {
                    /* znajdz jego odpowiednik j w katalogu zapisu */
                    if(strcmp(zawartosc_katalogu_zrodlowego[i]->d_name, zawartosc_katalogu_zapisu[j]->d_name) == 0)
                    {
                        /* 
                        *  Sprawdz czy sumy kontrolne plikow sie roznia,
                        *  jesli tak - zaloguj i przekopiuj plik na nowo
                        */

                        /* okresl sciezke pliku zrodlowego */
                        
                        dlugosc_nazwy_pliku_zrodlo = strlen(katalog_zrodlowy) + strlen(zawartosc_katalogu_zrodlowego[i]->d_name) + 2;
                        char sciezka_pliku_zrodlo[dlugosc_nazwy_pliku_zrodlo];
                        snprintf(sciezka_pliku_zrodlo, sizeof(sciezka_pliku_zrodlo), "%s/%s", katalog_zrodlowy, zawartosc_katalogu_zrodlowego[i]->d_name);
                        
                        /* okresl sciezke pliku zapisu */

                        dlugosc_nazwy_pliku_cel = strlen(sciezka_stworzonego_katalogu) + strlen(zawartosc_katalogu_zapisu[j]->d_name) + 2;
                        char sciezka_pliku_cel[dlugosc_nazwy_pliku_cel];
                        snprintf(sciezka_pliku_cel, sizeof(sciezka_pliku_cel), "%s/%s", sciezka_stworzonego_katalogu, zawartosc_katalogu_zapisu[j]->d_name);

                        /* otworz plik zrodlowy */

                        fd_zrodlowy = open(sciezka_pliku_zrodlo, O_RDONLY);//otworz deskryptor p. zrodlowego
                        if(fd_zrodlowy == -1)
                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s %s", sciezka_pliku_zrodlo, strerror(errno));
        
                        //syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Otworcie pliku %s z katalogu zrodlowego", sciezka_pliku_zrodlo);

                        /* otworz plik zapisu */

                        fd_zapisu = open(sciezka_pliku_cel, O_RDWR);//otworz deskryptor p. zapisu
                        if(fd_zapisu == -1)
                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s %s", sciezka_pliku_cel, strerror(errno));
    
                        /* Jesli sumy kontrolne plikow sie roznia, zaloguj i zaktualizuj */

                        if (porownajPliki(fd_zrodlowy, fd_zapisu) != 0)
                        {
                            /* zamknac i otworzyc deskryptor w celu czytania pliku od poczatku */
                            
                            close(fd_zrodlowy);
                            close(fd_zapisu);

                            fd_zrodlowy = open(sciezka_pliku_zrodlo, O_RDONLY);
                            fd_zapisu = open(sciezka_pliku_cel, O_RDWR);

                            struct stat st_zrodlowego;
                            fstat(fd_zrodlowy, &st_zrodlowego);

                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Zmodyfikowano plik %s", sciezka_pliku_zrodlo);
                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "------------------------------------");
                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Kopiuje plik o wadze %ld bajtow", st_zrodlowego.st_size);
                            
                            if (st_zrodlowego.st_size < (prog_dozego_pliku_w_MB * 1000000))
                                skopiujPlikNiskopoziomowo(zrodlowy_fd, docelowy_fd);
                            else
                                skopiujPlikEfektywnie(zrodlowy_fd, docelowy_fd);

                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Skopiowano plik %s", sciezka_pliku_zrodlo);
                            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "------------------------------------");
                            
                        }
                        close(fd_zrodlowy);
                        close(fd_zapisu);
                    }
                }
            }
        }

        /* Tryb z synchronizacja katalogow */

        if (synchronizuj_rowniez_katalogi == 1)
        {
            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Tryb synchronizacji katalogow nie jest obslugiwany");
            exit(EXIT_SUCCESS);
        }

        }

        czy_pierwsza_iteracja = 0;
        
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO),
        "Uspienie demona");

        /* odblokuj sygnal SIGUSR1 i uspij demona */

        sigprocmask (SIG_UNBLOCK, &sygnaly_do_blokowania, NULL);
        
        sleep(czas_spania_demona);

        /* demon budzi sie naturalnie - nowa iteracja */
        
        syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO),
        "Planowe rzebudzenie demona");

    }

    exit (EXIT_FAILURE);
}

int isDir(char *string_to_check)
{
    struct stat katalog_do_weryfikacji;
    int succesful;

    succesful = lstat(string_to_check, &katalog_do_weryfikacji);

    /* Jesli wystapi jakikolwiek blad funkcja zwraca -1, jesli nie: 0 */

    if (succesful == -1)
    {
        int blad = errno;

        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", string_to_check
                                                        , strerror(blad));

        return -1;
    }
    if (S_ISDIR(katalog_do_weryfikacji.st_mode) == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                "%s nie jest katalogiem. (exit)", string_to_check);
        return -1;
    }
    return 0;
}

static int PlikiIKatalogi (const struct dirent *entry)
{
    if(entry->d_type == DT_REG || entry->d_type == DT_DIR)
        return 1;
    else
        return 0;
}

static int tylkoPlikiZwykle (const struct dirent *entry)
{
    if (entry->d_type == DT_REG)
        return 1;
    else
        return 0;
}

int PodajIloscWpisowWKatalogu(char *katalog)
{

    struct dirent **zawartosc_katalogu;
    int n;
    int blad;

    n = scandir(katalog, &zawartosc_katalogu, PlikiIKatalogi, alphasort);
    if(n < 0)
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s",katalog, strerror(errno));

    return n;
}

int PodajIloscZwyklychPlikowWKatalogu(char *katalog)
{

    struct dirent **zawartosc_katalogu;
    int n;
    int blad;

    n = scandir(katalog, &zawartosc_katalogu, tylkoPlikiZwykle, alphasort);
    if(n < 0)
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s",katalog, strerror(errno));

    return n;
}

struct dirent ** PobierzWpisyZKatalogu (char *katalog, struct dirent **zawartosc_katalogu)
{
    int n;

    n = scandir(katalog, &zawartosc_katalogu, PlikiIKatalogi, alphasort);
    if(n < 0)
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s",katalog, strerror(errno));

    return zawartosc_katalogu;
}

struct dirent ** PobierzTylkoPlikiZwykleZKatalogu (char *katalog, struct dirent **zawartosc_katalogu)
{
    int n;

    n = scandir(katalog, &zawartosc_katalogu, tylkoPlikiZwykle, alphasort);
    if(n < 0)
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s",katalog, strerror(errno));

    return zawartosc_katalogu;
}

int porownajPliki(int fd1, int fd2)
{
    // Inicjalizacja kontekstu SHA
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_sha1();
    EVP_DigestInit_ex(md_ctx, md, NULL);

    // Bufor odczytu plików
    unsigned char buf1[BUFSIZ];
    unsigned char buf2[BUFSIZ];

    // Odczyt danych z plików i wyliczenie sum kontrolnych
    ssize_t bajty1, bajty2;
    unsigned char digest1[EVP_MAX_MD_SIZE], digest2[EVP_MAX_MD_SIZE];
    unsigned int digest_len;

    do {
        // Odczytaj kolejne fragmenty plików
        bajty1 = read(fd1, buf1, BUFSIZ);
        bajty2 = read(fd2, buf2, BUFSIZ);

        // Aktualizuj sumy kontrolne
        EVP_DigestUpdate(md_ctx, buf1, bajty1);
        EVP_DigestFinal_ex(md_ctx, digest1, &digest_len);
        EVP_DigestInit_ex(md_ctx, md, NULL);

        EVP_DigestUpdate(md_ctx, buf2, bajty2);
        EVP_DigestFinal_ex(md_ctx, digest2, &digest_len);
        EVP_DigestInit_ex(md_ctx, md, NULL);

        // Porównaj sumy kontrolne fragmentów
        if (bajty1 != bajty2 || memcmp(buf1, buf2, bajty1) != 0 ||
            memcmp(digest1, digest2, SHA_DIGEST_LENGTH) != 0) {
            EVP_MD_CTX_free(md_ctx);
            return 1; // Pliki są różne
        }
    } while (bajty1 > 0 && bajty2 > 0);

    // Sprawdz czy wystąpił błąd podczas odczytu plików
    if (bajty1 < 0 || bajty2 < 0) {
        EVP_MD_CTX_free(md_ctx);
        return 1; // Błąd odczytu pliku
    }

    // Pliki są takie same
    EVP_MD_CTX_free(md_ctx);
    return 0;
}

int skopiujPlikNiskopoziomowo(int fd_zrodlo, int fd_cel)
{
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Kopiowanie niskopoziomowe");
    char buf[8192]; //bufor na przepisywanie 8 kiB
    ssize_t bajty_przeczytane, bajty_zapisane;
    while( (bajty_przeczytane = read(fd_zrodlo, buf, 8192)) > 0)
    {
        bajty_zapisane = write(fd_cel, buf, bajty_przeczytane);
        if(bajty_zapisane != bajty_przeczytane || bajty_przeczytane == -1 || bajty_zapisane == -1)
        {
            syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "blad zapisu: %s", strerror(errno));
        }
    }
}

int skopiujPlikEfektywnie(int fd_zrodlo, int fd_cel)
{
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Kopiowanie poprzez sendfile");

    struct stat st;
    fstat(fd_zrodlo, &st);

    off_t offset = 0;
    int wynik;

    while (offset < st.st_size)
    {
        wynik = sendfile(fd_cel, fd_zrodlo, &offset, 8192);
        if(wynik == -1)
        {
            syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "Blad kopiowania pliku %s", strerror(errno));
        }
    }
    return wynik;
}