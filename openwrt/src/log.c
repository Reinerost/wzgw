/******************************************************************************
 *
 *  Projekt : WZGW
 *  Datei   : log.c
 *
 *  Aufgabe :
 *      Zentrale Ausgabe der Programmmeldungen über syslog.
 *
 *  Verantwortlich für:
 *      - Initialisierung und Beendigung des Loggings
 *      - Verwaltung des aktuellen Log-Levels
 *      - Filterung und Ausgabe formatierter Log-Meldungen
 *
 ******************************************************************************/

/******************************************************************************
 * Includes
 ******************************************************************************/

#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <stddef.h>


/******************************************************************************
 * Konstanten
 ******************************************************************************/

#define LOG_IDENT       "wzgw"
#define LOG_FACILITY    LOG_DAEMON


/******************************************************************************
 * Lokale Variablen
 ******************************************************************************/

static enum log_level current_level = LOG_LEVEL_INFO;
static bool initialized = false;


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static bool log_level_valid(enum log_level level);

static int log_level_to_syslog_priority(enum log_level level);

static void log_message(
    enum log_level level,
    const char *format,
    va_list arguments);


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

int log_init(enum log_level level)
{
    if (!log_level_valid(level))
        return -1;

    if (!initialized) {
        openlog(LOG_IDENT, LOG_PID, LOG_FACILITY);
        initialized = true;
    }

    return log_set_level(level);
}


int log_set_level(enum log_level level)
{
    int priority;

    if (!log_level_valid(level))
        return -1;

    current_level = level;

    /*
     * syslog erhält ebenfalls eine passende Maske.
     * Die zusätzliche Prüfung in log_message() bleibt trotzdem bestehen,
     * damit das Logging-Modul selbst über die Ausgabe entscheidet.
     */
    priority = log_level_to_syslog_priority(level);
    setlogmask(LOG_UPTO(priority));

    return 0;
}


enum log_level log_get_level(void)
{
    return current_level;
}


void log_close(void)
{
    if (!initialized)
        return;

    closelog();
    initialized = false;
}


void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    log_message(LOG_LEVEL_ERROR, format, arguments);
    va_end(arguments);
}


void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    log_message(LOG_LEVEL_WARNING, format, arguments);
    va_end(arguments);
}


void log_notice(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    log_message(LOG_LEVEL_NOTICE, format, arguments);
    va_end(arguments);
}


void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    log_message(LOG_LEVEL_INFO, format, arguments);
    va_end(arguments);
}


void log_debug(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    log_message(LOG_LEVEL_DEBUG, format, arguments);
    va_end(arguments);
}


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static bool log_level_valid(enum log_level level)
{
    return level >= LOG_LEVEL_ERROR &&
           level <= LOG_LEVEL_DEBUG;
}


static int log_level_to_syslog_priority(enum log_level level)
{
    switch (level) {
    case LOG_LEVEL_ERROR:
        return LOG_ERR;

    case LOG_LEVEL_WARNING:
        return LOG_WARNING;

    case LOG_LEVEL_NOTICE:
        return LOG_NOTICE;

    case LOG_LEVEL_INFO:
        return LOG_INFO;

    case LOG_LEVEL_DEBUG:
        return LOG_DEBUG;

    default:
        /*
         * Dieser Fall sollte durch log_level_valid() nicht auftreten.
         */
        return LOG_ERR;
    }
}


static void log_message(
    enum log_level level,
    const char *format,
    va_list arguments)
{
    int priority;

    if (format == NULL)
        return;

    if (!log_level_valid(level))
        return;

    /*
     * Kleinere Enum-Werte sind wichtiger:
     *
     * ERROR   = 0
     * WARNING = 1
     * NOTICE  = 2
     * INFO    = 3
     * DEBUG   = 4
     *
     * Bei LOG_LEVEL_INFO werden daher ERROR bis INFO ausgegeben,
     * DEBUG dagegen nicht.
     */
    if (level > current_level)
        return;

    priority = log_level_to_syslog_priority(level);

    vsyslog(priority, format, arguments);
}
