/*
 * MQTT-CAN-Gateway
 * Datei: can.h
 *
 * Aufgabe:
 *     Öffentliche Schnittstelle des CAN-Moduls.
 *
 * Verantwortlich für:
 *     - Prüfen und Konfigurieren der CAN-Schnittstelle
 *     - Öffnen und Schließen des SocketCAN-Sockets
 *     - Bereitstellen des CAN-Dateideskriptors
 */

#ifndef CAN_H
#define CAN_H

#include <stdint.h>
#include <linux/can.h>


/******************************************************************************
 * Typdefinitionen
 ******************************************************************************/

struct can_config;


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

/*
 * Prüft und konfiguriert die CAN-Schnittstelle und öffnet anschließend
 * den SocketCAN-Socket.
 *
 * Rückgabewert:
 *
 *  0  erfolgreich
 * -1  Fehler
 */
int can_init(const struct can_config *config);


/*
 * Schließt den SocketCAN-Socket.
 *
 * Die Funktion darf auch aufgerufen werden, wenn das CAN-Modul nicht
 * initialisiert wurde.
 */
void can_close(void);

/*
 * Sendet ein CAN-Telegramm mit Standard-ID und bis zu 8 Datenbytes.
 *
 * Rückgabewert:
 *      0 bei Erfolg
 *     -1 bei einem Fehler
 */
int can_send(uint16_t id, const uint8_t *data, uint8_t length);


/*
 * Liest ein CAN-Telegramm mit Standard-ID und bis zu 8 Datenbytes.
 *
 * Rückgabewert:
 *      1 wenn ein gültiges Telegramm empfangen wurde
 *      0 wenn kein Telegramm vorliegt oder das Telegramm nicht unterstützt wird
 *     -1 bei einem Fehler
 */
int can_receive(uint16_t *id, uint8_t *data, uint8_t *length);

#endif
