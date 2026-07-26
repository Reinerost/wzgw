/*
 * MQTT-CAN-Gateway
 * Datei: mqtt.c
 *
 * Aufgabe:
 *     Anbindung des Gateways an einen MQTT-Broker.
 *
 * Verantwortlich für:
 *     - Aufbau und Überwachung der MQTT-Verbindung
 *     - Abonnieren der MQTT-Kommandotopics
 *     - Umwandeln empfangener MQTT-Nachrichten in CAN-Daten
 *     - Veröffentlichen empfangener CAN-Telegramme über MQTT
 */


/******************************************************************************
 * Includes
 ******************************************************************************/

#include "mqtt.h"

#include <ctype.h>
#include <errno.h>
#include <linux/can.h>
#include <mosquitto.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "log.h"


/******************************************************************************
 * Konstanten
 ******************************************************************************/

#define MQTT_TOPIC_MAX_LENGTH       256U
#define MQTT_PAYLOAD_MAX_LENGTH      64U
#define MQTT_MESSAGE_QUEUE_SIZE      16U
#define MQTT_RECONNECT_DELAY_SECONDS  5U


/******************************************************************************
 * Typdefinitionen
 ******************************************************************************/

struct mqtt_message_queue
{
    struct mqtt_message messages[MQTT_MESSAGE_QUEUE_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned int count;
};


/******************************************************************************
 * Lokale Variablen
 ******************************************************************************/

static struct mosquitto *mqtt_client = NULL;
static const struct mqtt_config *mqtt_configuration = NULL;

static struct mqtt_message_queue mqtt_receive_queue;

static bool mqtt_library_initialized = false;
static bool mqtt_connected = false;

static time_t mqtt_next_reconnect = 0;


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static int mqtt_build_subscription_topic(
    char *topic,
    size_t topic_size);

static int mqtt_build_publish_topic(
    uint16_t id,
    char *topic,
    size_t topic_size);

static int mqtt_build_payload(
    const uint8_t *data,
    uint8_t length,
    char *payload,
    size_t payload_size);

static int mqtt_parse_topic(
    const char *topic,
    uint16_t *id);

static int mqtt_parse_payload(
    const void *payload,
    int payload_length,
    uint8_t *data,
    uint8_t *length);

static int mqtt_queue_push(
    const struct mqtt_message *message);

static int mqtt_queue_pop(
    struct mqtt_message *message);

static void mqtt_schedule_reconnect(void);

static void mqtt_on_connect(
    struct mosquitto *client,
    void *userdata,
    int result);

static void mqtt_on_disconnect(
    struct mosquitto *client,
    void *userdata,
    int result);

static void mqtt_on_message(
    struct mosquitto *client,
    void *userdata,
    const struct mosquitto_message *message);


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

int mqtt_init(const struct mqtt_config *config)
{
    int result;

    if (mqtt_client != NULL) {
        log_error("MQTT-Modul ist bereits initialisiert");
        return -1;
    }

    result = mosquitto_lib_init();

    if (result != MOSQ_ERR_SUCCESS) {
        log_error(
            "Mosquitto-Bibliothek konnte nicht initialisiert werden: %s",
            mosquitto_strerror(result));
        return -1;
    }

    mqtt_library_initialized = true;
    mqtt_configuration = config;

    memset(
        &mqtt_receive_queue,
        0,
        sizeof(mqtt_receive_queue));

    mqtt_client = mosquitto_new(
        config->client_id,
        true,
        NULL);

    if (mqtt_client == NULL) {
        if (errno == ENOMEM)
            log_error("MQTT-Client konnte nicht angelegt werden: Speichermangel");
        else
            log_error("MQTT-Client konnte nicht angelegt werden");

        mqtt_close();
        return -1;
    }

    mosquitto_connect_callback_set(
        mqtt_client,
        mqtt_on_connect);

    mosquitto_disconnect_callback_set(
        mqtt_client,
        mqtt_on_disconnect);

    mosquitto_message_callback_set(
        mqtt_client,
        mqtt_on_message);

    result = mosquitto_connect_async(
        mqtt_client,
        config->host,
        (int)config->port,
        (int)config->keepalive);

    if (result != MOSQ_ERR_SUCCESS) {
        log_error(
            "MQTT-Verbindungsaufbau konnte nicht gestartet werden: %s",
            mosquitto_strerror(result));
        return -1;
    }
    
    mqtt_schedule_reconnect();
    
    log_info(
        "MQTT-Client initialisiert: Broker=%s:%u, Client-ID='%s'",
        config->host,
        config->port,
        config->client_id);

    return 0;
}


void mqtt_close(void)
{
    if (mqtt_client != NULL) {
        if (mqtt_connected)
            mosquitto_disconnect(mqtt_client);

        mosquitto_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    if (mqtt_library_initialized) {
        mosquitto_lib_cleanup();
        mqtt_library_initialized = false;
    }

    mqtt_configuration = NULL;
    mqtt_connected = false;
    mqtt_next_reconnect = 0;

    memset(
        &mqtt_receive_queue,
        0,
        sizeof(mqtt_receive_queue));

    log_info("MQTT-Verbindung geschlossen");
}


void mqtt_poll(void)
{
    time_t current_time;
    int result;

    if (mqtt_client == NULL)
        return;

    result = mosquitto_loop(
        mqtt_client,
        0,
        1);

    if (result == MOSQ_ERR_SUCCESS && mqtt_connected)
        return;

    if (result != MOSQ_ERR_SUCCESS &&
        result != MOSQ_ERR_NO_CONN &&
        result != MOSQ_ERR_CONN_LOST) {

        log_error(
            "Fehler bei der MQTT-Verarbeitung: %s",
            mosquitto_strerror(result));
    }

    current_time = time(NULL);

    if (current_time == (time_t)-1)
        return;

    if (current_time < mqtt_next_reconnect)
        return;

    result = mosquitto_reconnect_async(mqtt_client);

    if (result == MOSQ_ERR_SUCCESS) {
        log_info("Erneuter MQTT-Verbindungsaufbau gestartet");
    } else {
        log_error(
            "MQTT-Verbindungsaufbau konnte nicht gestartet werden: %s",
            mosquitto_strerror(result));
    }

    mqtt_schedule_reconnect();
}

int mqtt_publish(
    uint16_t id,
    const uint8_t *data,
    uint8_t length)
{
    char topic[MQTT_TOPIC_MAX_LENGTH];
    char payload[MQTT_PAYLOAD_MAX_LENGTH];
    int result;

    if (mqtt_client == NULL) {
        log_error("MQTT-Modul ist nicht initialisiert");
        return -1;
    }

    if (!mqtt_connected) {
        log_error("Keine Verbindung zum MQTT-Broker");
        return -1;
    }

    if (id > CAN_SFF_MASK) {
        log_error(
            "Ungültige CAN-ID für MQTT-Veröffentlichung: %u",
            (unsigned int)id);
        return -1;
    }

    if (data == NULL) {
        log_error("MQTT-Veröffentlichungsdaten fehlen");
        return -1;
    }

    if (length == 0U || length > CAN_MAX_DLEN) {
        log_error(
            "Ungültige Datenlänge für MQTT-Veröffentlichung: %u",
            (unsigned int)length);
        return -1;
    }

    if (mqtt_build_publish_topic(
            id,
            topic,
            sizeof(topic)) < 0) {
        return -1;
    }

    if (mqtt_build_payload(
            data,
            length,
            payload,
            sizeof(payload)) < 0) {
        return -1;
    }

    result = mosquitto_publish(
        mqtt_client,
        NULL,
        topic,
        (int)strlen(payload),
        payload,
        0,
        false);

    if (result != MOSQ_ERR_SUCCESS) {
        log_error(
            "MQTT-Nachricht konnte nicht veröffentlicht werden: %s",
            mosquitto_strerror(result));
        return -1;
    }

    log_debug(
        "MQTT veröffentlicht: Topic='%s', Payload='%s'",
        topic,
        payload);

    return 0;
}


int mqtt_receive(struct mqtt_message *message)
{
    if (message == NULL) {
        log_error("Ungültiger Parameter für MQTT-Empfang");
        return -1;
    }

    return mqtt_queue_pop(message);
}


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static int mqtt_build_subscription_topic(
    char *topic,
    size_t topic_size)
{
    int written;

    written = snprintf(
        topic,
        topic_size,
        "%s#",
        mqtt_configuration->topic_command);

    if (written < 0 || (size_t)written >= topic_size) {
        log_error("MQTT-Abonnementtopic ist zu lang");
        return -1;
    }

    return 0;
}


static int mqtt_build_publish_topic(
    uint16_t id,
    char *topic,
    size_t topic_size)
{
    int written;

    if (mqtt_configuration->topic_id_format == NUMBER_FORMAT_HEX) {
        written = snprintf(
            topic,
            topic_size,
            "%s%X",
            mqtt_configuration->topic_publish,
            (unsigned int)id);
    } else {
        written = snprintf(
            topic,
            topic_size,
            "%s%u",
            mqtt_configuration->topic_publish,
            (unsigned int)id);
    }

    if (written < 0 || (size_t)written >= topic_size) {
        log_error("MQTT-Publishstopic ist zu lang");
        return -1;
    }

    return 0;
}


static int mqtt_build_payload(
    const uint8_t *data,
    uint8_t length,
    char *payload,
    size_t payload_size)
{
    size_t position;
    uint8_t index;
    int written;

    position = 0U;

    for (index = 0U; index < length; index++) {

        if (mqtt_configuration->payload_format == NUMBER_FORMAT_HEX) {

            written = snprintf(
                payload + position,
                payload_size - position,
                index == 0U ? "%02X" : " %02X",
                (unsigned int)data[index]);

        } else {

            written = snprintf(
                payload + position,
                payload_size - position,
                index == 0U ? "%u" : " %u",
                (unsigned int)data[index]);
        }

        if (written < 0 ||
            (size_t)written >= payload_size - position) {
            log_error("MQTT-Payload ist zu lang");
            return -1;
        }

        position += (size_t)written;
    }

    return 0;
}


static int mqtt_parse_topic(
    const char *topic,
    uint16_t *id)
{
    const char *id_text;
    char *end;
    size_t base_length;
    unsigned long value;
    int base;

    if (topic == NULL || id == NULL)
        return -1;

    base_length = strlen(mqtt_configuration->topic_command);

    if (strncmp(
            topic,
            mqtt_configuration->topic_command,
            base_length) != 0) {
        return 0;
    }

    id_text = topic + base_length;

    /*
     * Nur direkte numerische Untertopics sind Kommandos.
     */
    if (*id_text == '\0')
        return 0;

    /*
     * Zahlenbasis entsprechend der Konfiguration wählen.
     */
    if (mqtt_configuration->topic_id_format == NUMBER_FORMAT_HEX) {
        base = 16;
    } else {
        base = 10;
    }

    errno = 0;

    value = strtoul(
        id_text,
        &end,
        base);

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        value > CAN_SFF_MASK) {
        return 0;
    }

    *id = (uint16_t)value;

    return 1;
}


static int mqtt_parse_payload(
    const void *payload,
    int payload_length,
    uint8_t *data,
    uint8_t *length)
{
    char text[MQTT_PAYLOAD_MAX_LENGTH];
    char *position;
    char *end;
    unsigned long value;
    uint8_t count;
    int base;

    if (payload == NULL ||
        data == NULL ||
        length == NULL ||
        payload_length <= 0 ||
        (size_t)payload_length >= sizeof(text)) {
        return 0;
    }

    memcpy(
        text,
        payload,
        (size_t)payload_length);

    text[payload_length] = '\0';

    if (mqtt_configuration->payload_format == NUMBER_FORMAT_HEX) {
        base = 16;
    } else {
        base = 10;
    }

    position = text;
    count = 0U;

    while (*position != '\0') {
        while (isspace((unsigned char)*position))
            position++;

        if (*position == '\0')
            break;

        if (count >= CAN_MAX_DLEN)
            return 0;

        errno = 0;

        value = strtoul(
            position,
            &end,
            base);

        if (errno != 0 ||
            end == position ||
            value > UINT8_MAX) {
            return 0;
        }

        if (*end != '\0' &&
            !isspace((unsigned char)*end)) {
            return 0;
        }

        data[count] = (uint8_t)value;
        count++;

        position = end;
    }

    if (count == 0U)
        return 0;

    *length = count;

    return 1;
}


static int mqtt_queue_push(
    const struct mqtt_message *message)
{
    if (mqtt_receive_queue.count >= MQTT_MESSAGE_QUEUE_SIZE) {
        log_error(
            "MQTT-Empfangswarteschlange ist voll; "
            "Nachricht wird verworfen");
        return -1;
    }

    mqtt_receive_queue.messages[mqtt_receive_queue.head] = *message;

    mqtt_receive_queue.head++;

    if (mqtt_receive_queue.head >= MQTT_MESSAGE_QUEUE_SIZE)
        mqtt_receive_queue.head = 0U;

    mqtt_receive_queue.count++;

    return 0;
}


static int mqtt_queue_pop(struct mqtt_message *message)
{
    if (mqtt_receive_queue.count == 0U)
        return 0;

    *message = mqtt_receive_queue.messages[mqtt_receive_queue.tail];

    mqtt_receive_queue.tail++;

    if (mqtt_receive_queue.tail >= MQTT_MESSAGE_QUEUE_SIZE)
        mqtt_receive_queue.tail = 0U;

    mqtt_receive_queue.count--;

    return 1;
}


static void mqtt_schedule_reconnect(void)
{
    time_t current_time;

    current_time = time(NULL);

    if (current_time == (time_t)-1) {
        mqtt_next_reconnect = 0;
        return;
    }

    mqtt_next_reconnect =
        current_time + MQTT_RECONNECT_DELAY_SECONDS;
}


static void mqtt_on_connect(
    struct mosquitto *client,
    void *userdata,
    int result)
{
    char subscription_topic[MQTT_TOPIC_MAX_LENGTH];
    int subscribe_result;

    (void)userdata;

    if (result != 0) {
        mqtt_connected = false;

        log_error(
            "MQTT-Verbindung wurde abgelehnt: %s",
            mosquitto_connack_string(result));

        mqtt_schedule_reconnect();
        return;
    }

    mqtt_connected = true;
    mqtt_next_reconnect = 0;

    log_info("Mit MQTT-Broker verbunden");

    if (mqtt_build_subscription_topic(
            subscription_topic,
            sizeof(subscription_topic)) < 0) {
        return;
    }

    subscribe_result = mosquitto_subscribe(
        client,
        NULL,
        subscription_topic,
        0);

    if (subscribe_result != MOSQ_ERR_SUCCESS) {
        log_error(
            "MQTT-Topic '%s' konnte nicht abonniert werden: %s",
            subscription_topic,
            mosquitto_strerror(subscribe_result));
        return;
    }

    log_info(
        "MQTT-Topic abonniert: '%s'",
        subscription_topic);
}


static void mqtt_on_disconnect(
    struct mosquitto *client,
    void *userdata,
    int result)
{
    (void)client;
    (void)userdata;

    mqtt_connected = false;

    if (result == 0) {
        log_info("MQTT-Verbindung getrennt");
        return;
    }

    log_error(
        "MQTT-Verbindung unerwartet getrennt: %s",
        mosquitto_strerror(result));

    mqtt_schedule_reconnect();
}


static void mqtt_on_message(
    struct mosquitto *client,
    void *userdata,
    const struct mosquitto_message *message)
{
    struct mqtt_message received_message;
    int result;

    (void)client;
    (void)userdata;

    if (message == NULL ||
        message->topic == NULL) {
        return;
    }
    
    memset(
        &received_message,
        0,
        sizeof(received_message));

    result = mqtt_parse_topic(
        message->topic,
        &received_message.id);

    if (result <= 0)
        return;

    result = mqtt_parse_payload(
        message->payload,
        message->payloadlen,
        received_message.data,
        &received_message.length);

    if (result <= 0) {
        log_error(
            "Ungültiger MQTT-Payload für Topic '%s'",
            message->topic);
        return;
    }

    if (mqtt_queue_push(&received_message) < 0)
        return;

    log_debug(
        "MQTT empfangen: Topic='%s', DLC=%u",
        message->topic,
        (unsigned int)received_message.length);
}
