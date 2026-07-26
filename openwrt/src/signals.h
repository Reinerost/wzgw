/*
 * MQTT-CAN-Gateway
 * Datei: signals.h
 *
 * Aufgabe:
 *     Öffentliche Schnittstelle zur Behandlung von Prozesssignalen.
 *
 * Verantwortlich für:
 *     - Initialisierung der Signalbehandlung
 *     - Abfrage von Beenden- und Reload-Anforderungen
 *     - Rücksetzen der Reload-Anforderung
 */

#ifndef SIGNALS_H
#define SIGNALS_H


/*
 * Installiert die Signalbehandlung.
 *
 * Behandelte Signale:
 *
 * SIGINT   Programm beenden
 * SIGTERM  Programm beenden
 * SIGHUP   Konfiguration neu laden
 *
 * Rückgabewert:
 *
 *  0  erfolgreich
 * -1  Fehler
 */
int signals_init(void);


/*
 * Liefert ungleich null, wenn das Programm beendet werden soll.
 */
int signals_terminate_requested(void);


/*
 * Liefert ungleich null, wenn die Konfiguration neu geladen werden soll.
 */
int signals_reload_requested(void);


/*
 * Löscht die Reload-Anforderung.
 */
void signals_clear_reload(void);


#endif
