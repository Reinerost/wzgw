/*
 * MQTT-CAN-Gateway
 * Datei: mqtt.h
 *
 * Aufgabe:
 *     Öffentliche Schnittstelle für den Zugriff auf den MQTT-Broker.
 *
 * Verantwortlich für:
 *     - Initialisieren und Beenden der MQTT-Verbindung
 *     - Veröffentlichen von CAN-Telegrammen
 *     - Empfangen von MQTT-Nachrichten
 */


#ifndef MQTT_H
#define MQTT_H


/******************************************************************************
 * Includes
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <linux/can.h>


/******************************************************************************
 * Typdefinitionen
 ******************************************************************************/

struct mqtt_config;


/*
 * Empfangene MQTT-Nachricht
 */
struct mqtt_message
{
    uint16_t id;
    uint8_t  data[CAN_MAX_DLEN];
    uint8_t  length;
};


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/
 
/*
 * Initialisiert die MQTT-Verbindung.
 *
 * Rückgabewert:
 *      0 bei Erfolg
 *     -1 bei einem Fehler
 */
int mqtt_init(
    const struct mqtt_config *config);


/*
 * Trennt die Verbindung zum MQTT-Broker.
 */
void mqtt_close(void);


/*
 * Bearbeitet anstehende MQTT-Ereignisse.
 *
 * Diese Funktion sollte regelmäßig aus der Hauptschleife
 * aufgerufen werden.
 */
void mqtt_poll(void);


/*
 * Veröffentlicht ein CAN-Telegramm.
 *
 * Rückgabewert:
 *      0 bei Erfolg
 *     -1 bei einem Fehler
 */
int mqtt_publish(
    uint16_t id,
    const uint8_t *data,
    uint8_t length);


/*
 * Liefert eine empfangene MQTT-Nachricht.
 *
 * Rückgabewert:
 *      1 Nachricht vorhanden
 *      0 keine Nachricht vorhanden
 *     -1 Fehler
 */
int mqtt_receive(
    struct mqtt_message *message);


#endif /* MQTT_H */
