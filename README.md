# WZGW

**WZGW** ist ein leichtgewichtiges Gateway zur bidirektionalen Kommunikation zwischen einem klassischen CAN-Bus (SocketCAN) und einem MQTT-Broker.

Die Software wurde speziell für **OpenWrt** entwickelt und läuft als Hintergrunddienst (Daemon). Sie eignet sich insbesondere für Anwendungen der Haus- und Gebäudeautomation.

---

## Eigenschaften

* CAN ↔ MQTT in beide Richtungen
* SocketCAN-Unterstützung
* MQTT über libmosquitto
* Konfiguration über UCI
* Syslog-Unterstützung
* automatische CAN-Bus-Überwachung
* automatischer Neustart des CAN-Interfaces bei Fehlern
* Reload der Konfiguration per `SIGHUP`
* geringer Speicher- und CPU-Bedarf

---

## Hardware

Entwickelt und getestet mit

* OrangePi PC
* OrangePi Zero
* MCP2515 CAN-Controller
* klassischer CAN 2.0 Bus

---

## Software

* OpenWrt 21.02.x
* SocketCAN
* libmosquitto
* libuci

---

## Projektübersicht

```text
                 MQTT Broker
                      ^
                      |
                  libmosquitto
                      |
               +--------------+
               |    WZGW      |
               +--------------+
               | CAN <-> MQTT |
               +--------------+
                      |
                  SocketCAN
                      |
                     can0
                      |
                   MCP2515
                      |
                   CAN-Bus
```

---

## Konfiguration

Die Konfiguration erfolgt vollständig über UCI.

Beispiel:

```uci
config daemon
    option loglevel 'info'

config can
    option interface 'can0'
    option bitrate '100000'
    option restart_ms '100'

config mqtt
    option host '192.168.0.22'
    option port '1883'
    option client_id 'wzgw'

    option topic_command '/can0/'
    option topic_publish '/can0_wz/'

    option keepalive '60'

    option topic_id_format 'hex'
    option payload_format 'decimal'
```

---

## MQTT-Topics

### MQTT → CAN

Topic

```text
/can0/600
```

Payload

```text
1 240 255
```

---

### CAN → MQTT

Topic

```text
/can0_wz/600
```

Payload

```text
1 240 255
```

---

## Darstellungsformate

Die Darstellung kann unabhängig konfiguriert werden.

### CAN-Identifier

Hexadezimal

```text
600
```

Dezimal

```text
1536
```

---

### Nutzdaten

Hexadezimal

```text
01 F0 FF
```

Dezimal

```text
1 240 255
```

Damit kann das Gateway problemlos an bestehende MQTT-Installationen angepasst werden.

---

## Start

Nach der Installation kann der Dienst wie gewohnt gestartet werden.

```bash
/etc/init.d/wzgw enable
/etc/init.d/wzgw start
```

---

## Logging

Alle Meldungen werden über Syslog ausgegeben.

Unterstützte Log-Level

* debug
* info
* notice
* warning
* error

---

## Lizenz

Dieses Projekt wird als Open-Source-Software veröffentlicht.

Die genaue Lizenz ist der Datei `LICENSE` zu entnehmen.

---

## Autor

Reiner Ost

---

## Projektstatus

**Version 0.9.2**

Das Gateway unterstützt:

* bidirektionale CAN-/MQTT-Kommunikation
* konfigurierbare Darstellung von CAN-Identifiern (Hex/Dezimal)
* konfigurierbare Darstellung der Nutzdaten (Hex/Dezimal)
* automatische Überwachung und Wiederherstellung des CAN-Interfaces
* dauerhaften 24/7-Betrieb unter OpenWrt

