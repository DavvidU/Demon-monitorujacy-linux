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

#include <stdio.h> 
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h> // pid_t
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h> //getuid, pid_t
#include <syslog.h> //openlog, syslog
#include <string.h> // strerror, perror
#include <signal.h> //sig_atomic_t typ
#include <errno.h> //do uzywania zmiennej errno (wyluskiwanie kodu bledu, makra bledow)

volatile sig_atomic_t work_in_loop = 1;

int isDir(char *string_to_check);

void handler(int signum)
{
    work_in_loop = 0;
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
                                0); // logowanie bledow getopt_long

    sid = setsid(); // przypisanie procesu potomnego do sesji
    if (sid < 0) 
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_ERR), "Przypisanie procesowi sesji: %s", 
                                                                strerror(blad));
            exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) // zmiana katalogu roboczego na jedyny na pewno istniejacy
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_ERR), "Zmiana katalogu na /: %s", 
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

    static int podano_katalog_zrodlowy_flaga = 0;    /* flaga opcji s */
    static char *katalog_zrodlowy;                   /* wartosc opcji s */

    static int podano_katalog_docelowy_flaga = 0;    /* flaga opcji t */
    static char *katalog_docelowy;                   /* wartosc opcji t */

    static int synchronizuj_rowniez_katalogi = 0;    /* flaga opcji R */

    static int czas_spania_demona = 30;     /* w sekundach */

    static float prog_dozego_pliku_w_MB = 10.0;

    int processed_argument;     /* przetwarzany przez getopt_long() */
    
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
                            "Czas spania demona musi byc dodatni.");
        exit(EXIT_FAILURE);
    }

    if (prog_dozego_pliku_w_MB <= 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Wielkosc progowa dozego pliku musi byc dodatnia.");
        exit(EXIT_FAILURE);
    }

    if (podano_katalog_zrodlowy_flaga == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Nalezy podac katalog zrodlowy.");
        exit(EXIT_FAILURE);
    }

    if (podano_katalog_docelowy_flaga == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                            "Nalezy podac katalog docelowy.");
        exit(EXIT_FAILURE);
    }

    if ( isDir(katalog_zrodlowy) == -1 )
        exit(EXIT_FAILURE);

    if ( access(katalog_zrodlowy, R_OK) == -1 )
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s", katalog_zrodlowy
                                                        , strerror(blad));
        exit(EXIT_FAILURE);
    }

    if ( isDir(katalog_docelowy) == -1)
        exit(EXIT_FAILURE);

    if ( access(katalog_docelowy, R_OK | W_OK) == -1 )
    {
        blad = errno;
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s", katalog_docelowy
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
    
    signal(SIGUSR1, handler); //TODO obsluga bledow!
    while (work_in_loop)
    {
        /* skan katalogu zrodlowego */

    }
    syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "Otrzymano sygnal SIGUSR1");
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

        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), "%s %s", string_to_check
                                                        , strerror(blad));

        return -1;
    }
    if (S_ISDIR(katalog_do_weryfikacji.st_mode) == 0)
    {
        syslog(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), 
                "%s nie jest katalogiem.", string_to_check);
        return -1;
    }
    return 0;
}
