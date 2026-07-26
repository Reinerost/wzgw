/*
 * MQTT-CAN-Gateway
 * Datei: signals.c
 *
 * Aufgabe:
 *     Behandlung der Prozesssignale.
 *
 * Verantwortlich für:
 *     - Registrierung der Signalhandler
 *     - Speicherung von Beenden- und Reload-Anforderungen
 *     - Bereitstellung der Signalzustände für die Hauptschleife
 */


/******************************************************************************
 * Includes
 ******************************************************************************/

#include "signals.h"

#include <signal.h>
#include <stddef.h>


/******************************************************************************
 * Lokale Variablen
 ******************************************************************************/

static volatile sig_atomic_t terminate_requested = 0;
static volatile sig_atomic_t reload_requested = 0;


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static void signal_handler(int signal_number);

static int install_signal_handler(int signal_number);


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

int signals_init(void)
{
    terminate_requested = 0;
    reload_requested = 0;

    if (install_signal_handler(SIGINT) < 0)
        return -1;

    if (install_signal_handler(SIGTERM) < 0)
        return -1;

    if (install_signal_handler(SIGHUP) < 0)
        return -1;

    return 0;
}


int signals_terminate_requested(void)
{
    return terminate_requested != 0;
}


int signals_reload_requested(void)
{
    return reload_requested != 0;
}


void signals_clear_reload(void)
{
    reload_requested = 0;
}


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static void signal_handler(int signal_number)
{
    switch (signal_number) {
    case SIGINT:
    case SIGTERM:
        terminate_requested = 1;
        break;

    case SIGHUP:
        reload_requested = 1;
        break;

    default:
        break;
    }
}


static int install_signal_handler(int signal_number)
{
    struct sigaction action;

    action.sa_handler = signal_handler;
    action.sa_flags = 0;

    if (sigemptyset(&action.sa_mask) < 0)
        return -1;

    if (sigaction(signal_number, &action, NULL) < 0)
        return -1;

    return 0;
}
