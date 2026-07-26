/******************************************************************************
 *
 *  Projekt : WZGW
 *  Datei   : config.c
 *
 *  Aufgabe :
 *      Laden und Prüfen der Anwendungskonfiguration aus UCI.
 *
 *  Verantwortlich für:
 *      - Zugriff auf /etc/config/wzgw
 *      - Setzen von Standardwerten
 *      - Einlesen der UCI-Optionen
 *      - Plausibilitätsprüfung
 *
 ******************************************************************************/

/******************************************************************************
 * Includes
 ******************************************************************************/

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <uci.h>

#include "log.h"

/******************************************************************************
 * Konstanten
 ******************************************************************************/

#define UCI_PACKAGE_NAME                  "wzgw"

#define DEFAULT_LOGLEVEL                  LOG_LEVEL_INFO

#define DEFAULT_CAN_INTERFACE             "can0"
#define DEFAULT_CAN_BITRATE               100000U
#define DEFAULT_CAN_RESTART_MS            100U

#define DEFAULT_MQTT_HOST                 "192.168.0.22"
#define DEFAULT_MQTT_PORT                 1883U
#define DEFAULT_MQTT_CLIENT_ID            "wzgw"
#define DEFAULT_MQTT_TOPIC_COMMAND        "/can0/"
#define DEFAULT_MQTT_TOPIC_PUBLISH        "/can0_wz/"
#define DEFAULT_MQTT_KEEPALIVE            60U

#define DEFAULT_MQTT_TOPIC_ID_FORMAT      NUMBER_FORMAT_HEX
#define DEFAULT_MQTT_PAYLOAD_FORMAT       NUMBER_FORMAT_DECIMAL

/******************************************************************************
 * Lokale Typdefinitionen
 ******************************************************************************/

/*
 * Zur Erkennung mehrfach vorhandener UCI-Sektionen.
 */
struct section_state {
    bool daemon_seen;
    bool can_seen;
    bool mqtt_seen;
};

/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static void config_set_defaults(struct app_config *config);

static int config_read_daemon_section(
    struct uci_context *uci,
    struct uci_section *section,
    struct daemon_config *config);

static int config_read_can_section(
    struct uci_context *uci,
    struct uci_section *section,
    struct can_config *config);

static int config_read_mqtt_section(
    struct uci_context *uci,
    struct uci_section *section,
    struct mqtt_config *config);

static int config_validate(const struct app_config *config);

static const char *config_get_option(
    struct uci_context *uci,
    struct uci_section *section,
    const char *name);

static int config_read_string(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    char *destination,
    size_t destination_size);

static int config_read_uint(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    unsigned int minimum,
    unsigned int maximum,
    unsigned int *destination);

static int config_read_loglevel(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    enum log_level *destination);

static int config_read_number_format(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    number_format_t *destination);

/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

int config_load(struct app_config *config)
{
    struct uci_context *uci = NULL;
    struct uci_package *package = NULL;
    struct uci_element *element;
    struct section_state sections = { false, false, false };
    int result = -1;

    if (config == NULL) {
        log_error("config_load(): Ungültiger Konfigurationszeiger");
        return -1;
    }

    /*
     * Zunächst vollständige, definierte Standardwerte setzen.
     */
    config_set_defaults(config);

    uci = uci_alloc_context();

    if (uci == NULL) {
        log_error("UCI-Kontext konnte nicht angelegt werden");
        return -1;
    }

    if (uci_load(uci, UCI_PACKAGE_NAME, &package) != UCI_OK) {
        log_error(
            "UCI-Konfiguration '/etc/config/%s' "
            "konnte nicht geladen werden",
            UCI_PACKAGE_NAME);
        goto cleanup;
    }

    uci_foreach_element(&package->sections, element) {
        struct uci_section *section;

        section = uci_to_section(element);

        if (section == NULL || section->type == NULL)
            continue;

        if (strcmp(section->type, "daemon") == 0) {
            if (sections.daemon_seen) {
                log_warning(
                    "Mehrere UCI-Sektionen vom Typ 'daemon'; "
                    "die letzte Sektion wird verwendet");
            }

            sections.daemon_seen = true;

            if (config_read_daemon_section(
                    uci,
                    section,
                    &config->daemon) < 0) {
                goto cleanup;
            }

        } else if (strcmp(section->type, "can") == 0) {
            if (sections.can_seen) {
                log_warning(
                    "Mehrere UCI-Sektionen vom Typ 'can'; "
                    "die letzte Sektion wird verwendet");
            }

            sections.can_seen = true;

            if (config_read_can_section(
                    uci,
                    section,
                    &config->can) < 0) {
                goto cleanup;
            }

        } else if (strcmp(section->type, "mqtt") == 0) {
            if (sections.mqtt_seen) {
                log_warning(
                    "Mehrere UCI-Sektionen vom Typ 'mqtt'; "
                    "die letzte Sektion wird verwendet");
            }

            sections.mqtt_seen = true;

            if (config_read_mqtt_section(
                    uci,
                    section,
                    &config->mqtt) < 0) {
                goto cleanup;
            }

        } else {
            log_warning(
                "Unbekannte UCI-Sektion vom Typ '%s' wird ignoriert",
                section->type);
        }
    }

    if (!sections.daemon_seen) {
        log_warning(
            "UCI-Sektion 'daemon' fehlt; "
            "Standardwerte werden verwendet");
    }

    if (!sections.can_seen) {
        log_warning(
            "UCI-Sektion 'can' fehlt; "
            "Standardwerte werden verwendet");
    }

    if (!sections.mqtt_seen) {
        log_warning(
            "UCI-Sektion 'mqtt' fehlt; "
            "Standardwerte werden verwendet");
    }

    if (config_validate(config) < 0)
        goto cleanup;

    result = 0;

cleanup:
    if (package != NULL)
        uci_unload(uci, package);

    uci_free_context(uci);

    return result;
}

/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static void config_set_defaults(struct app_config *config)
{
    memset(config, 0, sizeof(*config));

    config->daemon.loglevel = DEFAULT_LOGLEVEL;

    strcpy(config->can.interface, DEFAULT_CAN_INTERFACE);
    config->can.bitrate = DEFAULT_CAN_BITRATE;
    config->can.restart_ms = DEFAULT_CAN_RESTART_MS;

    strcpy(config->mqtt.host, DEFAULT_MQTT_HOST);
    strcpy(config->mqtt.client_id, DEFAULT_MQTT_CLIENT_ID);
    strcpy(config->mqtt.topic_command, DEFAULT_MQTT_TOPIC_COMMAND);
    strcpy(config->mqtt.topic_publish, DEFAULT_MQTT_TOPIC_PUBLISH);

    config->mqtt.port = DEFAULT_MQTT_PORT;
    config->mqtt.keepalive = DEFAULT_MQTT_KEEPALIVE;

    config->mqtt.topic_id_format = DEFAULT_MQTT_TOPIC_ID_FORMAT;
    config->mqtt.payload_format = DEFAULT_MQTT_PAYLOAD_FORMAT;
}


static int config_read_daemon_section(
    struct uci_context *uci,
    struct uci_section *section,
    struct daemon_config *config)
{
    if (config_read_loglevel(
            uci,
            section,
            "loglevel",
            &config->loglevel) < 0) {
        return -1;
    }

    return 0;
}


static int config_read_can_section(
    struct uci_context *uci,
    struct uci_section *section,
    struct can_config *config)
{
    if (config_read_string(
            uci,
            section,
            "interface",
            config->interface,
            sizeof(config->interface)) < 0) {
        return -1;
    }

    if (config_read_uint(
            uci,
            section,
            "bitrate",
            1000U,
            1000000U,
            &config->bitrate) < 0) {
        return -1;
    }

    if (config_read_uint(
            uci,
            section,
            "restart_ms",
            0U,
            600000U,
            &config->restart_ms) < 0) {
        return -1;
    }

    return 0;
}


static int config_read_mqtt_section(
    struct uci_context *uci,
    struct uci_section *section,
    struct mqtt_config *config)
{
    if (config_read_string(
            uci,
            section,
            "host",
            config->host,
            sizeof(config->host)) < 0) {
        return -1;
    }

    if (config_read_uint(
            uci,
            section,
            "port",
            1U,
            65535U,
            &config->port) < 0) {
        return -1;
    }

    if (config_read_string(
            uci,
            section,
            "client_id",
            config->client_id,
            sizeof(config->client_id)) < 0) {
        return -1;
    }

    if (config_read_string(
            uci,
            section,
            "topic_command",
            config->topic_command,
            sizeof(config->topic_command)) < 0) {
        return -1;
    }

    if (config_read_string(
            uci,
            section,
            "topic_publish",
            config->topic_publish,
            sizeof(config->topic_publish)) < 0) {
        return -1;
    }

    if (config_read_uint(
            uci,
            section,
            "keepalive",
            0U,
            65535U,
            &config->keepalive) < 0) {
        return -1;
    }

    if (config_read_number_format(
            uci,
            section,
            "topic_id_format",
            &config->topic_id_format) < 0) {
        return -1;
    }

    if (config_read_number_format(
            uci,
            section,
            "payload_format",
            &config->payload_format) < 0) {
        return -1;
    }

    return 0;
}


static int config_validate(const struct app_config *config)
{
    size_t topic_length;

    if (config->can.interface[0] == '\0') {
        log_error("CAN-Interface darf nicht leer sein");
        return -1;
    }

    if (config->can.bitrate == 0U) {
        log_error("CAN-Bitrate darf nicht null sein");
        return -1;
    }

    if (config->mqtt.host[0] == '\0') {
        log_error("MQTT-Host darf nicht leer sein");
        return -1;
    }

    if (config->mqtt.client_id[0] == '\0') {
        log_error("MQTT-Client-ID darf nicht leer sein");
        return -1;
    }

    if (config->mqtt.topic_command[0] == '\0') {
        log_error("MQTT-Kommandotopic darf nicht leer sein");
        return -1;
    }

    topic_length = strlen(config->mqtt.topic_command);

    if (config->mqtt.topic_command[topic_length - 1U] != '/') {
        log_error("MQTT-Kommandotopic muss mit '/' enden");
        return -1;
    }

    if (config->mqtt.topic_publish[0] == '\0') {
        log_error("MQTT-Publishtopic darf nicht leer sein");
        return -1;
    }

    topic_length = strlen(config->mqtt.topic_publish);

    if (config->mqtt.topic_publish[topic_length - 1U] != '/') {
        log_error("MQTT-Publishtopic muss mit '/' enden");
        return -1;
    }

    if (config->mqtt.topic_id_format != NUMBER_FORMAT_HEX &&
        config->mqtt.topic_id_format != NUMBER_FORMAT_DECIMAL) {
        log_error("Ungültiges Format für die MQTT-Topic-ID");
        return -1;
    }

    if (config->mqtt.payload_format != NUMBER_FORMAT_HEX &&
        config->mqtt.payload_format != NUMBER_FORMAT_DECIMAL) {
        log_error("Ungültiges Format für den MQTT-Payload");
        return -1;
    }

    return 0;
}


static const char *config_get_option(
    struct uci_context *uci,
    struct uci_section *section,
    const char *name)
{
    return uci_lookup_option_string(uci, section, name);
}


static int config_read_string(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    char *destination,
    size_t destination_size)
{
    const char *value;
    size_t value_length;

    if (destination == NULL || destination_size == 0U)
        return -1;

    value = config_get_option(uci, section, option_name);

    if (value == NULL) {
        log_warning(
            "UCI-Option '%s.%s' fehlt; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    value_length = strlen(value);

    if (value_length == 0U) {
        log_warning(
            "UCI-Option '%s.%s' ist leer; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    if (value_length >= destination_size) {
        log_error(
            "UCI-Option '%s.%s' ist zu lang "
            "(maximal %u Zeichen)",
            section->type,
            option_name,
            (unsigned int)(destination_size - 1U));
        return -1;
    }

    memcpy(destination, value, value_length + 1U);

    return 0;
}


static int config_read_uint(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    unsigned int minimum,
    unsigned int maximum,
    unsigned int *destination)
{
    const char *value;
    char *end = NULL;
    unsigned long number;

    if (destination == NULL)
        return -1;

    value = config_get_option(uci, section, option_name);

    if (value == NULL) {
        log_warning(
            "UCI-Option '%s.%s' fehlt; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    if (*value == '\0') {
        log_warning(
            "UCI-Option '%s.%s' ist leer; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    errno = 0;
    number = strtoul(value, &end, 10);

    if (errno != 0 ||
        end == value ||
        *end != '\0' ||
        number > UINT_MAX) {
        log_error(
            "UCI-Option '%s.%s' enthält keine gültige Zahl: '%s'",
            section->type,
            option_name,
            value);
        return -1;
    }

    if (number < minimum || number > maximum) {
        log_error(
            "UCI-Option '%s.%s' liegt außerhalb des "
            "zulässigen Bereichs %u bis %u: '%s'",
            section->type,
            option_name,
            minimum,
            maximum,
            value);
        return -1;
    }

    *destination = (unsigned int)number;

    return 0;
}


static int config_read_loglevel(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    enum log_level *destination)
{
    const char *value;

    if (destination == NULL)
        return -1;

    value = config_get_option(uci, section, option_name);

    if (value == NULL) {
        log_warning(
            "UCI-Option '%s.%s' fehlt; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    if (*value == '\0') {
        log_warning(
            "UCI-Option '%s.%s' ist leer; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    if (strcmp(value, "debug") == 0) {
        *destination = LOG_LEVEL_DEBUG;

    } else if (strcmp(value, "info") == 0) {
        *destination = LOG_LEVEL_INFO;

    } else if (strcmp(value, "notice") == 0) {
        *destination = LOG_LEVEL_NOTICE;

    } else if (strcmp(value, "warning") == 0 ||
               strcmp(value, "warn") == 0) {
        *destination = LOG_LEVEL_WARNING;

    } else if (strcmp(value, "error") == 0 ||
               strcmp(value, "err") == 0) {
        *destination = LOG_LEVEL_ERROR;

    } else {
        log_error(
            "Ungültiges Log-Level '%s'; "
            "erlaubt sind debug, info, notice, warning und error",
            value);
        return -1;
    }

    return 0;
}


static int config_read_number_format(
    struct uci_context *uci,
    struct uci_section *section,
    const char *option_name,
    number_format_t *destination)
{
    const char *value;

    if (destination == NULL)
        return -1;

    value = config_get_option(uci, section, option_name);

    if (value == NULL) {
        log_warning(
            "UCI-Option '%s.%s' fehlt; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    if (*value == '\0') {
        log_warning(
            "UCI-Option '%s.%s' ist leer; "
            "Standardwert bleibt aktiv",
            section->type,
            option_name);
        return 0;
    }

    if (strcmp(value, "hex") == 0) {
        *destination = NUMBER_FORMAT_HEX;

    } else if (strcmp(value, "decimal") == 0 ||
               strcmp(value, "dec") == 0) {
        *destination = NUMBER_FORMAT_DECIMAL;

    } else {
        log_error(
            "Ungültiges Zahlenformat '%s' für UCI-Option '%s.%s'; "
            "erlaubt sind hex und decimal",
            value,
            section->type,
            option_name);
        return -1;
    }

    return 0;
}
