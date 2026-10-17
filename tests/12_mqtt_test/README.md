# Test 12 — MQTT Connectivity (Architectural Status & Optional Bridge)

## Architectural Status
Following source code inspection of the ArvaCore node repository (`sketch_oct11a/sketch_oct11a.ino` and `test_uplink_gateway.py`), the ArvaNode system **strictly communicates via HTTPS REST APIs** over TCP port 443 with custom JSON payloads (`POST /telemetry/uplink/{nodeId}` and `POST /pairing/request`).

**MQTT is NOT deployed in the ESP32 node firmware.**
Consequently, running an MQTT test against the production ArvaCore backend is not applicable to the current system architecture.

## Optional MQTT Implementation Guide
If a field gateway or broker (e.g. Mosquitto, EMQX, AWS IoT Core) is deployed in future project iterations:

### 1. Library Requirement
Install `PubSubClient` by Nick O'Leary via the Arduino Library Manager.

### 2. Standard Integration Pattern
```cpp
#include <PubSubClient.h>

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void connectMqtt() {
    mqttClient.setServer("broker.example.com", 1883);
    while (!mqttClient.connected()) {
        if (mqttClient.connect("ArvaNode-01", "user", "pass")) {
            mqttClient.publish("arvacore/telemetry", "{\"soil\":42.0}");
        }
        delay(1000);
    }
}
```

### 3. Engineering Caveat
Even if an MQTT broker connects successfully, it does not prove backend message ingestion or pump actuation unless verified end-to-end against the database or event pipeline.
