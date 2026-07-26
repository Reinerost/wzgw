/*
 * MQTT-CAN-Gateway
 * Datei: can.c
 *
 * Aufgabe:
 *     Initialisierung und grundlegender Zugriff auf die CAN-Schnittstelle.
 *
 * Verantwortlich für:
 *     - Prüfen der konfigurierten Schnittstelle
 *     - Herunterfahren der Schnittstelle
 *     - Einstellen von Bitrate und Restart-Zeit
 *     - Hochfahren der Schnittstelle
 *     - Öffnen und Binden des SocketCAN-Sockets
 */


/******************************************************************************
 * Includes
 ******************************************************************************/

#include "can.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/netlink.h>
#include <linux/can/raw.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "log.h"


/******************************************************************************
 * Konstanten
 ******************************************************************************/

#define CAN_INVALID_FD       (-1)
#define CAN_NETLINK_BUFFER   4096U


/******************************************************************************
 * Typdefinitionen
 ******************************************************************************/

struct can_netlink_request {
    struct nlmsghdr header;
    struct ifinfomsg interface;
    char buffer[512];
};


/******************************************************************************
 * Lokale Variablen
 ******************************************************************************/

static int can_fd = CAN_INVALID_FD;


/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static int can_check_config(const struct can_config *config);

static int can_set_interface_up(
    const char *interface_name,
    bool up);

static int can_configure_interface(
    unsigned int interface_index,
    unsigned int bitrate,
    unsigned int restart_ms);

static int can_open_socket(
    const char *interface_name,
    unsigned int interface_index);

static int netlink_add_attribute(
    struct nlmsghdr *header,
    size_t maximum_length,
    unsigned short type,
    const void *data,
    size_t data_length);

static struct rtattr *netlink_begin_nested(
    struct nlmsghdr *header,
    size_t maximum_length,
    unsigned short type);

static void netlink_end_nested(
    struct nlmsghdr *header,
    struct rtattr *attribute);

static int netlink_send_request(
    struct nlmsghdr *request);


/******************************************************************************
 * Öffentliche Funktionen
 ******************************************************************************/

int can_init(const struct can_config *config)
{
    unsigned int interface_index;

    if (can_fd != CAN_INVALID_FD) {
        log_error("CAN-Modul ist bereits initialisiert");
        return -1;
    }

    if (can_check_config(config) < 0)
        return -1;

    interface_index = if_nametoindex(config->interface);

    if (interface_index == 0U) {
        log_error(
            "CAN-Schnittstelle '%s' wurde nicht gefunden: %m",
            config->interface);
        return -1;
    }

    log_debug(
        "CAN-Schnittstelle '%s' hat Index %u",
        config->interface,
        interface_index);

    /*
     * Bitrate und andere CAN-Parameter dürfen nur geändert werden,
     * während die Schnittstelle heruntergefahren ist.
     */
    if (can_set_interface_up(config->interface, false) < 0)
        return -1;

    if (can_configure_interface(
            interface_index,
            config->bitrate,
            config->restart_ms) < 0) {
        return -1;
    }

    if (can_set_interface_up(config->interface, true) < 0)
        return -1;

    if (can_open_socket(
            config->interface,
            interface_index) < 0) {
        return -1;
    }

    log_info(
        "CAN-Schnittstelle '%s' initialisiert: "
        "Bitrate=%u, Restart=%u ms",
        config->interface,
        config->bitrate,
        config->restart_ms);

    return 0;
}


void can_close(void)
{
    if (can_fd == CAN_INVALID_FD)
        return;

    close(can_fd);
    can_fd = CAN_INVALID_FD;

    log_info("CAN-Socket geschlossen");
}

/******************************************************************************
 * Lokale Funktionen
 ******************************************************************************/

static int can_check_config(const struct can_config *config)
{
    size_t interface_length;

    if (config == NULL) {
        log_error("CAN-Konfiguration fehlt");
        return -1;
    }

    interface_length = strnlen(
        config->interface,
        sizeof(config->interface));

    if (interface_length == 0U) {
        log_error("CAN-Schnittstellenname fehlt");
        return -1;
    }

    if (interface_length >= sizeof(config->interface) ||
        interface_length >= IFNAMSIZ) {
        log_error("CAN-Schnittstellenname ist zu lang");
        return -1;
    }

    if (config->bitrate == 0U) {
        log_error("CAN-Bitrate darf nicht null sein");
        return -1;
    }

    return 0;
}


static int can_set_interface_up(
    const char *interface_name,
    bool up)
{
    struct ifreq request;
    int control_fd;

    control_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_fd < 0) {
        log_error(
            "Kontrollsocket für CAN-Schnittstelle konnte "
            "nicht geöffnet werden: %m");
        return -1;
    }

    memset(&request, 0, sizeof(request));

    memcpy(
        request.ifr_name,
        interface_name,
        strlen(interface_name) + 1U);

    if (ioctl(control_fd, SIOCGIFFLAGS, &request) < 0) {
        log_error(
            "Status der CAN-Schnittstelle '%s' konnte "
            "nicht gelesen werden: %m",
            interface_name);
        close(control_fd);
        return -1;
    }

    if (up)
        request.ifr_flags |= IFF_UP;
    else
        request.ifr_flags &= (short)~IFF_UP;

    if (ioctl(control_fd, SIOCSIFFLAGS, &request) < 0) {
        log_error(
            "CAN-Schnittstelle '%s' konnte nicht %s "
            "werden: %m",
            interface_name,
            up ? "hochgefahren" : "heruntergefahren");
        close(control_fd);
        return -1;
    }

    close(control_fd);

    log_debug(
        "CAN-Schnittstelle '%s' wurde %s",
        interface_name,
        up ? "hochgefahren" : "heruntergefahren");

    return 0;
}


static int can_configure_interface(
    unsigned int interface_index,
    unsigned int bitrate,
    unsigned int restart_ms)
{
    struct can_netlink_request request;
    struct can_bittiming bit_timing;
    struct rtattr *link_info;
    struct rtattr *info_data;
    const char interface_type[] = "can";

    memset(&request, 0, sizeof(request));
    memset(&bit_timing, 0, sizeof(bit_timing));

    request.header.nlmsg_len =
        NLMSG_LENGTH(sizeof(struct ifinfomsg));

    request.header.nlmsg_type = RTM_NEWLINK;

    request.header.nlmsg_flags =
        NLM_F_REQUEST |
        NLM_F_ACK;

    request.interface.ifi_family = AF_UNSPEC;
    request.interface.ifi_index = (int)interface_index;

    link_info = netlink_begin_nested(
        &request.header,
        sizeof(request),
        IFLA_LINKINFO);

    if (link_info == NULL)
        return -1;

    if (netlink_add_attribute(
            &request.header,
            sizeof(request),
            IFLA_INFO_KIND,
            interface_type,
            sizeof(interface_type)) < 0) {
        return -1;
    }

    info_data = netlink_begin_nested(
        &request.header,
        sizeof(request),
        IFLA_INFO_DATA);

    if (info_data == NULL)
        return -1;

    bit_timing.bitrate = bitrate;

    if (netlink_add_attribute(
            &request.header,
            sizeof(request),
            IFLA_CAN_BITTIMING,
            &bit_timing,
            sizeof(bit_timing)) < 0) {
        return -1;
    }

    if (netlink_add_attribute(
            &request.header,
            sizeof(request),
            IFLA_CAN_RESTART_MS,
            &restart_ms,
            sizeof(restart_ms)) < 0) {
        return -1;
    }

    netlink_end_nested(
        &request.header,
        info_data);

    netlink_end_nested(
        &request.header,
        link_info);

    if (netlink_send_request(&request.header) < 0) {
        log_error(
            "CAN-Schnittstelle konnte nicht konfiguriert werden");
        return -1;
    }

    log_debug(
        "CAN-Konfiguration gesetzt: "
        "Bitrate=%u, Restart=%u ms",
        bitrate,
        restart_ms);

    return 0;
}


static int can_open_socket(
    const char *interface_name,
    unsigned int interface_index)
{
    struct sockaddr_can address;
    int flags;
    int fd;

    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        log_error(
            "CAN-Raw-Socket konnte nicht geöffnet werden: %m");
        return -1;
    }

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        log_error(
            "Flags des CAN-Sockets konnten nicht gelesen werden: %m");
        close(fd);
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        log_error(
            "CAN-Socket konnte nicht auf nichtblockierenden "
            "Betrieb gestellt werden: %m");
        close(fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));

    address.can_family = AF_CAN;
    address.can_ifindex = (int)interface_index;

    if (bind(
            fd,
            (struct sockaddr *)&address,
            sizeof(address)) < 0) {
        log_error(
            "CAN-Socket konnte nicht an Schnittstelle '%s' "
            "gebunden werden: %m",
            interface_name);
        close(fd);
        return -1;
    }

    can_fd = fd;

    log_debug(
        "CAN-Socket an Schnittstelle '%s' gebunden",
        interface_name);

    return 0;
}


static int netlink_add_attribute(
    struct nlmsghdr *header,
    size_t maximum_length,
    unsigned short type,
    const void *data,
    size_t data_length)
{
    size_t attribute_length;
    size_t new_length;
    struct rtattr *attribute;

    attribute_length = RTA_LENGTH(data_length);

    new_length =
        NLMSG_ALIGN(header->nlmsg_len) +
        RTA_ALIGN(attribute_length);

    if (new_length > maximum_length) {
        log_error("CAN-Netlink-Nachricht ist zu groß");
        return -1;
    }

    attribute = (struct rtattr *)(
        (char *)header +
        NLMSG_ALIGN(header->nlmsg_len));

    attribute->rta_type = type;
    attribute->rta_len = (unsigned short)attribute_length;

    if (data_length > 0U && data != NULL) {
        memcpy(
            RTA_DATA(attribute),
            data,
            data_length);
    }

    header->nlmsg_len = (unsigned int)new_length;

    return 0;
}


static struct rtattr *netlink_begin_nested(
    struct nlmsghdr *header,
    size_t maximum_length,
    unsigned short type)
{
    struct rtattr *attribute;

    attribute = (struct rtattr *)(
        (char *)header +
        NLMSG_ALIGN(header->nlmsg_len));

    if (netlink_add_attribute(
            header,
            maximum_length,
            type,
            NULL,
            0U) < 0) {
        return NULL;
    }

    attribute->rta_type |= NLA_F_NESTED;

    return attribute;
}


static void netlink_end_nested(
    struct nlmsghdr *header,
    struct rtattr *attribute)
{
    attribute->rta_len = (unsigned short)(
        (char *)header +
        NLMSG_ALIGN(header->nlmsg_len) -
        (char *)attribute);
}


static int netlink_send_request(
    struct nlmsghdr *request)
{
    char receive_buffer[CAN_NETLINK_BUFFER];
    struct sockaddr_nl address;
    struct nlmsghdr *response;
    struct nlmsgerr *error_message;
    ssize_t received;
    int remaining;
    int fd;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        log_error(
            "Route-Netlink-Socket konnte nicht geöffnet werden: %m");
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;

    if (sendto(
            fd,
            request,
            request->nlmsg_len,
            0,
            (struct sockaddr *)&address,
            sizeof(address)) < 0) {
        log_error(
            "CAN-Netlink-Anfrage konnte nicht gesendet werden: %m");
        close(fd);
        return -1;
    }

    received = recv(
        fd,
        receive_buffer,
        sizeof(receive_buffer),
        0);

    if (received < 0) {
        log_error(
            "Antwort auf CAN-Netlink-Anfrage konnte "
            "nicht gelesen werden: %m");
        close(fd);
        return -1;
    }

    remaining = (int)received;

    for (response = (struct nlmsghdr *)receive_buffer;
         NLMSG_OK(response, remaining);
         response = NLMSG_NEXT(response, remaining)) {

        if (response->nlmsg_type != NLMSG_ERROR)
            continue;

        error_message = (struct nlmsgerr *)NLMSG_DATA(response);

        if (error_message->error == 0) {
            close(fd);
            return 0;
        }

        errno = -error_message->error;

        log_error(
            "Kernel hat CAN-Netlink-Anfrage abgelehnt: %m");

        close(fd);
        return -1;
    }

    log_error(
        "Keine gültige Bestätigung auf CAN-Netlink-Anfrage erhalten");

    close(fd);
    return -1;
}

int can_send(uint16_t id, const uint8_t *data, uint8_t length)
{
    struct can_frame frame;
    ssize_t written;

    if (can_fd == CAN_INVALID_FD) {
        log_error("CAN-Modul ist nicht initialisiert");
        return -1;
    }

    if (id > CAN_SFF_MASK) {
        log_error(
            "Ungültige CAN-ID %u: Standard-ID erwartet",
            (unsigned int)id);
        return -1;
    }

    if (data == NULL) {
        log_error("CAN-Sendedaten fehlen");
        return -1;
    }

    if (length == 0U || length > CAN_MAX_DLEN) {
        log_error(
            "Ungültige CAN-Datenlänge %u",
            (unsigned int)length);
        return -1;
    }

    memset(&frame, 0, sizeof(frame));

    frame.can_id = (canid_t)id;
    frame.can_dlc = length;

    memcpy(
        frame.data,
        data,
        length);

    written = write(
        can_fd,
        &frame,
        sizeof(frame));

    if (written < 0) {
        log_error(
            "CAN-Telegramm konnte nicht gesendet werden: %m");
        return -1;
    }

    if ((size_t)written != sizeof(frame)) {
        log_error(
            "CAN-Telegramm wurde unvollständig gesendet: "
            "%zd von %zu Bytes",
            written,
            sizeof(frame));
        return -1;
    }

    return 0;
}

int can_receive(uint16_t *id, uint8_t *data, uint8_t *length)
{
    struct can_frame frame;
    ssize_t received;

    if (id == NULL || data == NULL || length == NULL) {
        log_error("Ungültige Parameter für CAN-Empfang");
        return -1;
    }

    if (can_fd == CAN_INVALID_FD) {
        log_error("CAN-Modul ist nicht initialisiert");
        return -1;
    }

    for (;;) {
        received = read(
            can_fd,
            &frame,
            sizeof(frame));

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }

            log_error(
                "CAN-Telegramm konnte nicht gelesen werden: %m");
            return -1;
        }

        if ((size_t)received != sizeof(frame)) {
            log_error(
                "Unvollständiges CAN-Telegramm empfangen: "
                "%zd von %zu Bytes",
                received,
                sizeof(frame));
            return -1;
        }

        if ((frame.can_id &
             (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U) {
            continue;
        }

        if (frame.can_dlc > CAN_MAX_DLEN) {
            continue;
        }

        *id = (uint16_t)(frame.can_id & CAN_SFF_MASK);
        *length = frame.can_dlc;

        memcpy(
            data,
            frame.data,
            frame.can_dlc);

        return 1;
    }
}
