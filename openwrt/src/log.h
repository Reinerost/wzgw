/******************************************************************************
 *
 *  Projekt : WZGW
 *  Datei   : log.h
 *
 *  Aufgabe :
 *      Öffentliche Schnittstelle des Logging-Moduls.
 *
 *  Verantwortlich für:
 *      - Definition der Log-Level
 *      - Initialisierung und Beendigung des Loggings
 *      - Ausgabe formatierter Log-Meldungen
 *
 ******************************************************************************/

#ifndef WZGW_LOG_H
#define WZGW_LOG_H


/******************************************************************************
 * Makros
 ******************************************************************************/

/*
 * Ermöglicht dem Compiler die Prüfung der printf-Formatstrings.
 */
#if defined(__GNUC__)
#define LOG_PRINTF_FORMAT(format_index, argument_index) \
    __attribute__((format(printf, format_index, argument_index)))
#else
#define LOG_PRINTF_FORMAT(format_index, argument_index)
#endif


/******************************************************************************
 * Typdefinitionen
 ******************************************************************************/

enum log_level {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_NOTICE,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

/**
 * Initialisiert das Logging.
 *
 * level:
 *      Höchster Log-Level, der ausgegeben werden soll.
 *
 * Rückgabewert:
 *      0   Logging erfolgreich initialisiert
 *     -1   Ungültiger Log-Level
 */
int log_init(enum log_level level);


/**
 * Ändert den aktuellen Log-Level.
 *
 * Diese Funktion wird insbesondere nach dem Laden oder erneuten Laden
 * der Konfiguration verwendet.
 *
 * Rückgabewert:
 *      0   Log-Level erfolgreich übernommen
 *     -1   Ungültiger Log-Level
 */
int log_set_level(enum log_level level);


/**
 * Gibt den aktuell eingestellten Log-Level zurück.
 */
enum log_level log_get_level(void);


/**
 * Beendet das Logging.
 */
void log_close(void);


/**
 * Gibt eine Fehlermeldung aus.
 */
void log_error(const char *format, ...)
    LOG_PRINTF_FORMAT(1, 2);


/**
 * Gibt eine Warnung aus.
 */
void log_warning(const char *format, ...)
    LOG_PRINTF_FORMAT(1, 2);


/**
 * Gibt eine Hinweismeldung aus.
 */
void log_notice(const char *format, ...)
    LOG_PRINTF_FORMAT(1, 2);


/**
 * Gibt eine Informationsmeldung aus.
 */
void log_info(const char *format, ...)
    LOG_PRINTF_FORMAT(1, 2);


/**
 * Gibt eine Debug-Meldung aus.
 */
void log_debug(const char *format, ...)
    LOG_PRINTF_FORMAT(1, 2);


#endif /* WZGW_LOG_H */
