/*
 * main.c
 *
 * MQTT-CAN-Gateway
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#include "config.h"
#include "log.h"
#include "signals.h"
#include "can.h"
#include "mqtt.h"

/* --------------------------------------------------------------------------
 * Konstanten
 * -------------------------------------------------------------------------- */
#define CAN_RX_TIMEOUT_SECONDS       90
#define CAN_RESTART_TIMEOUT_COUNT     2
#define CAN_FAILED_TIMEOUT_COUNT      4

/* --------------------------------------------------------------------------
 * Typedef
 * -------------------------------------------------------------------------- */

typedef enum
{
    CAN_BUS_OK,
    CAN_BUS_TIMEOUT,
    CAN_BUS_RESTARTING,
    CAN_BUS_RECOVERING,
    CAN_BUS_FAILED
} can_bus_state_t;

/* --------------------------------------------------------------------------
 * Lokale Variablen
 * -------------------------------------------------------------------------- */
static struct timespec last_can_rx_time;
static can_bus_state_t can_bus_state;
static uint32_t can_bus_timeout_count;

/* --------------------------------------------------------------------------
 * Lokale Funktionen
 * -------------------------------------------------------------------------- */
static int init_logging(void);
static int app_init(struct app_config *config);
static int app_reload(struct app_config *config);
static int app_run(struct app_config *config);
static void app_cleanup(void);
static void app_forward_can_to_mqtt(void);
static void app_forward_mqtt_to_can(void);
static void app_check_can_bus(void);
static void app_update_can_rx_time(void);
static void app_handle_can_bus_state(const struct app_config *config);
static int app_restart_can(const struct app_config *config);

static int init_logging(void)
{
    /*
     * Das Logging wird zunächst mit einem festen Standard-Level gestartet.
     * Nach dem Einlesen der Konfiguration wird das konfigurierte Log-Level
     * übernommen.
     */
    if (log_init(LOG_LEVEL_INFO) < 0)
        return -1;

    return 0;
}


static int app_init(struct app_config *config)
{
    if (config == NULL)
        return -1;

    if (config_load(config) < 0) {
        log_error("Konfiguration konnte nicht geladen werden");
        return -1;
    }

    /*
     * Ab jetzt gilt das in der Konfiguration eingestellte Log-Level.
     */
    if (log_set_level(config->daemon.loglevel) < 0) {
        log_error("Konfiguriertes Log-Level ist ungültig");
        return -1;
    }

    if (signals_init() < 0) {
        log_error(
            "Signalbehandlung konnte nicht initialisiert werden: %m");
        return -1;
    }
    
    if (can_init(&config->can) < 0) {
    log_error("CAN-Modul konnte nicht initialisiert werden");
       return -1;
    }
    
    if (clock_gettime(CLOCK_MONOTONIC, &last_can_rx_time) < 0) {
    log_error(
        "Zeit für CAN-Busüberwachung konnte nicht gelesen werden: %m");
        return -1;
    }

    can_bus_state = CAN_BUS_OK;
    can_bus_timeout_count = 0U;

    if (mqtt_init(&config->mqtt) < 0) {
        log_error("MQTT-Modul konnte nicht initialisiert werden");
        can_close();
        return -1;
    }
    
    return 0;
}


static int app_reload(struct app_config *config)
{
    struct app_config new_config;

    if (config == NULL)
        return -1;

    if (config_load(&new_config) < 0) {
        log_error(
            "Neue Konfiguration ist ungültig; "
            "bisherige Konfiguration bleibt aktiv");
        return -1;
    }

    /*
     * to do:
     *
     * - Änderungen vergleichen
     * - CAN gegebenenfalls neu initialisieren
     * - MQTT gegebenenfalls neu verbinden
     *
     * Erst nach erfolgreicher Anwendung wird die neue Konfiguration
     * übernommen.
     */

    if (log_set_level(new_config.daemon.loglevel) < 0) {
        log_error(
            "Neues Log-Level ist ungültig; "
            "bisherige Konfiguration bleibt aktiv");
        return -1;
    }

    *config = new_config;
    
    log_set_level(config->daemon.loglevel);
    
    log_info("Konfiguration neu geladen");

    return 0;
}


static int app_run(struct app_config *config)
{
    if (config == NULL)
        return -1;

    while (!signals_terminate_requested()) {

        app_forward_mqtt_to_can();
        app_forward_can_to_mqtt();
        app_check_can_bus();
        app_handle_can_bus_state(config);
        
        if (signals_reload_requested()) {
            signals_clear_reload();
            app_reload(config);
        }

        usleep(10000U);
    }

    return 0;
}


static void app_cleanup(void)
{
    mqtt_close();
    can_close();
    
    /*
     * Später in umgekehrter Reihenfolge abbauen:
     *
     * eventloop_cleanup();
     */
}

static void app_forward_can_to_mqtt(void)
{
    uint16_t id;
    uint8_t data[CAN_MAX_DLEN];
    uint8_t length;

    while (can_receive(&id, data, &length) > 0) {
        app_update_can_rx_time();

        if (mqtt_publish(id, data, length) < 0) {
            log_error(
                "CAN-Telegramm konnte nicht über MQTT "
                "veröffentlicht werden: ID=%u, DLC=%u",
                (unsigned int)id,
                (unsigned int)length);
        }
    }
}

static void app_forward_mqtt_to_can(void)
{
    struct mqtt_message message;
    int result;
        mqtt_poll();
    while (mqtt_receive(&message) > 0) {
        result = can_send(
            message.id,
            message.data,
            message.length);

        log_debug(
            "can_send() Ergebnis: %d",
            result);

        if (result < 0) {
            log_error(
                "CAN-Telegramm konnte nicht gesendet werden: "
                "ID=%u, DLC=%u",
                (unsigned int)message.id,
                (unsigned int)message.length);
        }
    }
}

static void app_update_can_rx_time(void)
{
    if (clock_gettime(CLOCK_MONOTONIC, &last_can_rx_time) < 0) {
        log_error(
            "Zeit für CAN-Busüberwachung konnte nicht gelesen werden: %m");
        return;
    }

    if (can_bus_state != CAN_BUS_OK) {
        log_info(
            "CAN-Kommunikation wiederhergestellt "
            "(Timeouts: %u)",
            (unsigned int)can_bus_timeout_count);
    }

    can_bus_state = CAN_BUS_OK;
    can_bus_timeout_count = 0U;
}

static void app_check_can_bus(void)
{
    struct timespec now;
    time_t elapsed;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        log_error(
            "Zeit für CAN-Busüberwachung konnte nicht gelesen werden: %m");
        return;
    }

    elapsed = now.tv_sec - last_can_rx_time.tv_sec;

    if (elapsed < CAN_RX_TIMEOUT_SECONDS)
        return;

    /*
     * Der nächste Timeout darf erst nach einem weiteren vollständigen
     * Überwachungsintervall gezählt werden.
     */
    last_can_rx_time = now;

    if (can_bus_timeout_count < UINT32_MAX)
        can_bus_timeout_count++;

    if (can_bus_timeout_count >= CAN_FAILED_TIMEOUT_COUNT) {
        if (can_bus_state != CAN_BUS_FAILED) {
            can_bus_state = CAN_BUS_FAILED;

            log_error(
                "CAN-Kommunikation dauerhaft ausgefallen "
                "(Timeouts: %u)",
                (unsigned int)can_bus_timeout_count);
        }

        return;
    }

    if (can_bus_timeout_count >= CAN_RESTART_TIMEOUT_COUNT) {
        can_bus_state = CAN_BUS_RESTARTING;

        log_warning(
            "CAN-Kommunikation weiterhin ausgefallen; "
            "Neustart erforderlich (Timeouts: %u)",
            (unsigned int)can_bus_timeout_count);

        return;
    }

    can_bus_state = CAN_BUS_TIMEOUT;

    log_warning(
        "Seit %u Sekunden kein CAN-Telegramm empfangen "
        "(Timeouts: %u)",
        CAN_RX_TIMEOUT_SECONDS,
        (unsigned int)can_bus_timeout_count);
}

static void app_handle_can_bus_state(const struct app_config *config)
{
    struct timespec now;

    if (can_bus_state != CAN_BUS_RESTARTING)
        return;

    if (app_restart_can(config) < 0) {
        can_bus_state = CAN_BUS_FAILED;

        log_error(
            "Automatische Wiederherstellung der "
            "CAN-Kommunikation fehlgeschlagen");

        return;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        log_error(
            "Zeit für CAN-Busüberwachung konnte nicht gelesen werden: %m");

        can_bus_state = CAN_BUS_FAILED;
        return;
    }

    /*
     * Nach dem Neustart bekommt der Bus ein vollständiges
     * Überwachungsintervall Zeit für ein Telegramm.
     */
    last_can_rx_time = now;
    can_bus_state = CAN_BUS_RECOVERING;

    log_info(
        "Warte nach CAN-Neustart auf ein Telegramm "
        "(Timeouts: %u)",
        (unsigned int)can_bus_timeout_count);
}

static int app_restart_can(const struct app_config *config)
{
    if (config == NULL)
        return -1;

    log_info("CAN-Interface wird neu gestartet");

    can_close();

    usleep(config->can.restart_ms * 1000U);

    if (can_init(&config->can) < 0) {
        log_error("CAN-Interface konnte nicht neu gestartet werden");
        return -1;
    }

    log_info("CAN-Interface wurde neu gestartet");

    return 0;
}

int main(int argc, char **argv)
{
    struct app_config config;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    if (init_logging() < 0)
        return EXIT_FAILURE;

    log_info("Programm wird gestartet");

    if (app_init(&config) < 0)
        goto cleanup;

    if (app_run(&config) < 0) {
        log_error("Programmablauf wurde mit einem Fehler beendet");
        goto cleanup;
    }

    result = EXIT_SUCCESS;

cleanup:
    app_cleanup();

    if (result == EXIT_SUCCESS)
        log_info("Programm wurde beendet");
    else
        log_error("Programm wurde wegen eines Fehlers beendet");

    log_close();

    return result;
}
 
