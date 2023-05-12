/********************************************************************
* FILE NAME: monitoruj.c                                            *
*                                                                   *
* PURPOSE: Uruchamia demona z odpowiednimi argumentami podanymi     *
*          przez użytkownika w celu monitorowania zmian w zadanym   *
*          katalogu przy użyciu dołączonych modułów.                *
* FILE REFERENCES:                                                  *
*                                                                   *
* Name I/O Description                                              *
* ---- --- -----------                                              *
*                                                                   *
* EXTERNAL VARIABLES:                                               *
* Source: < >                                                       *
*                                                                   *
* Name Type I/O Description                                         *
* ---- ---- --- -----------                                         *
*                                                                   *
* EXTERNAL REFERENCES:                                              *
*                                                                   *
* Name Description                                                  *
* ---- -----------                                                  *
*                                                                   *
* ABNORMAL TERMINATION CONDITIONS, ERROR AND WARNING MESSAGES:      *
*                                                                   *
* NOTES:                                                            *
*                                                                   *
********************************************************************/

#define _POSIX_SOURCE // SIG_BLOCK
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
#include <limits.h>
#include <setjmp.h>
/*
#define _NSIG 64 
#define _NSIG_BPW 32 
#define _NSIG_WORDS (_NSIG / _NSIG_BPW) 

typedef struct { 
unsigned long sig[_NSIG_WORDS]; 
} sigset_t;*/

jmp_buf powrot;

int isDir(char *string_to_check);

void skanujKatalog(char *katalog);

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

    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "SID procesu demona: %d", 
                                                            sid);
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Katalog roboczy: /");

    /* Deklaracja zmiennych na argumenty inicjalizacyjne */

    static int podano_katalog_zrodlowy_flaga = 0;    /* flaga opcji s */
    static char *katalog_zrodlowy;                   /* wartosc opcji s */

    static int podano_katalog_docelowy_flaga = 0;    /* flaga opcji t */
    static char *katalog_docelowy;                   /* wartosc opcji t */

    static int synchronizuj_rowniez_katalogi = 0;    /* flaga opcji R */

    static int czas_spania_demona = 30;     /* w sekundach */

    static float prog_dozego_pliku_w_MB = 10.0;

    int processed_argument;     /* przetwarzany przez getopt_long() */

    /******************************/


    
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
        exit(EXIT_FAILURE);

    if ( access(katalog_zrodlowy, R_OK | X_OK) == -1 )
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", katalog_zrodlowy
                                                        , strerror(blad));
        exit(EXIT_FAILURE);
    }

    if ( isDir(katalog_docelowy) == -1)
        exit(EXIT_FAILURE);

    if ( access(katalog_docelowy, R_OK | W_OK | X_OK) == -1 )
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s (exit)", katalog_docelowy
                                                        , strerror(blad));
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
    
    sigset_t sygnaly_do_blokowania; //blokuj SIGUSR1 dopuki demon nie spi
    sigemptyset(&sygnaly_do_blokowania); // wyzerowanie maski
    if (sigaddset(&sygnaly_do_blokowania, SIGUSR1) == -1)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_WARNING), "%s (exit)", strerror(blad));
        exit(EXIT_FAILURE);
    }

    signal(SIGUSR1, handler); //TODO obsluga bledow!
	
	// #include <string.h> :OOOOOOOOOOOOOOOO
	// char *nazwa_katalogu_zapisu_tmp = strrchr(katalog_zrodlowy, '/'); // nazwa to /TOCOCHCE
	// char nazwa_katalogu_zapisu[strlen(nazwa_katalogu_zapisu_tmp)];
	// strcpy(nazwa_katalogu_zapisu, nazwa_katalogu_zapisu_tmp + 1);
	// nazwa_katalogu_zapisu_tmp = NULL;
	// chdir(katalog_docelowy);
	// mkdir(nazwa_katalogu_zapisu, S_IRWXU); // u+rwx
    while (1)
    {
        sigprocmask (SIG_UNBLOCK, &sygnaly_do_blokowania, NULL);
        if (setjmp(powrot) != 0)
        {
            syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO),
            "Przebudzenie demona - sygnal SIGUSR1");
        }
        /* skan katalogu zrodlowego */
		//------------------------------------------------------ WADY I ZALETY MMAP, jaka jest roznica miedzy strategia right through i right behind przy realizacji pamieci dysku jakeigso  tam HDD, dlaczego żadka spotyka sie systemy plikow  , jak dziala prealokacja blokow w systemie plikow, gdzie przechowywane sa atrybuty plikow w systemie FAT32 a gdzie w systemie unixowym , jak dziala indexowanie posrednie i czemu w praktyce mu so by c potronjnie potrzebbrne, dlaczego na gearf opisujacy strukture (jaka?) nazucony jest warunek acyklicznosci
		// if( (mkdir(tablica od return do konca, 0777)) == -1)
		//{
			//blad = errno;
			//syslog(LOG_MAKEPRI(LOG_USER, LOG_WARNING), "%s (exit)", strerror(blad));
			//exit(EXIT_FAILURE);
		//} 
		// chdir("%s/nazwa_fold", katalog_docelowy);
		//--------------------------------------------------------- albo (bo chyba mam juz nazwe katalogu)
		//
		

        /* zrob cala reszte*/

        sigprocmask (SIG_UNBLOCK, &sygnaly_do_blokowania, NULL);
        /* spij */

    }
    
    exit (0); //Nie tu konczyc i nwm czy tak.
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

void skanujKatalog(char *katalog)
{
    
}

