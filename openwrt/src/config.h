/******************************************************************************
 *
 *  Projekt : WZGW
 *  Datei   : config.h
 *
 *  Aufgabe :
 *      Öffentliche Schnittstelle des Konfigurationsmoduls.
 *
 *  Verantwortlich für:
 *      - Definition der Anwendungskonfiguration
 *      - Laden der UCI-Konfiguration
 *
 ******************************************************************************/

#ifndef WZGW_CONFIG_H
#define WZGW_CONFIG_H

#include "log.h"

/******************************************************************************
 * Konstanten
 ******************************************************************************/

#define CONFIG_CAN_INTERFACE_LENGTH     16
#define CONFIG_MQTT_HOST_LENGTH         64
#define CONFIG_MQTT_CLIENT_ID_LENGTH    64
#define CONFIG_MQTT_TOPIC_LENGTH       128

/******************************************************************************
 * Typdefinitionen
 ******************************************************************************/

struct daemon_config {
    enum log_level loglevel;
};

typedef enum
{
    NUMBER_FORMAT_HEX,
    NUMBER_FORMAT_DECIMAL
} number_format_t;

struct can_config {
    char interface[CONFIG_CAN_INTERFACE_LENGTH];

    unsigned int bitrate;
    unsigned int restart_ms;
};


struct mqtt_config {
    char host[CONFIG_MQTT_HOST_LENGTH];
    char client_id[CONFIG_MQTT_CLIENT_ID_LENGTH];
    char topic_command[CONFIG_MQTT_TOPIC_LENGTH];
    char topic_publish[CONFIG_MQTT_TOPIC_LENGTH];
    
    unsigned int port;
    unsigned int keepalive;
    
    number_format_t topic_id_format;
    number_format_t payload_format;
};


struct app_config {
    struct daemon_config daemon;
    struct can_config can;
    struct mqtt_config mqtt;
};


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

/**
 * Lädt die Konfiguration aus /etc/config/wzgw.
 *
 * Vor dem Einlesen werden alle Werte mit Standardwerten belegt.
 * Fehlende Optionen behalten ihren Standardwert.
 *
 * Rückgabewert:
 *      0   Konfiguration erfolgreich geladen und geprüft
 *     -1   Konfiguration konnte nicht geladen oder geprüft werden
 */
int config_load(struct app_config *config);


#endif /* WZGW_CONFIG_H */
