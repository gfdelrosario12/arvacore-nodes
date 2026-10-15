#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "esp_system.h"
#include "esp_mac.h"

// ============================================================
// ARVACORE / ARVANODE - AWD FIELD SENSOR NODE FIRMWARE
// Board: ESP32 Dev Module / ESP32-WROOM-32
//
// Existing sensor and button wiring is preserved.
//
// Sensors:
//   Soil AO             -> GPIO 34
//   Ultrasonic TRIG     -> GPIO 5
//   Ultrasonic ECHO     -> GPIO 18 (voltage divider required)
//
// Buttons (active LOW):
//   Activation Button   -> GPIO 19
//   Pairing Button      -> GPIO 23
//
// New LEDs (active HIGH):
//   Green Pairing LED   -> GPIO 25 through 220-330 ohm resistor
//   Blue Data LED       -> GPIO 26 through 220-330 ohm resistor
//
// Serial Monitor: 115200 baud
// ============================================================

// -------------------- BACKEND CONFIGURATION -----------------

const char* REMOTE_BACKEND_URL =
    "https://api-aquaflow.gladwindr.me/api/v1";

const char* LOCAL_BACKEND_URL =
    "http://192.168.1.88:8080/api/v1";

const char* BACKEND_BASE_URL = REMOTE_BACKEND_URL;

const char* GATEWAY_KEY =
    "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9";

// -------------------- WIFI CONFIGURATION --------------------

const char* WIFI_SSID = "Cream";
const char* WIFI_PASS = "hatdog123";

// -------------------- PIN ASSIGNMENTS -----------------------

const uint8_t SOIL_PIN     = 34;
const uint8_t TRIG_PIN     = 5;
const uint8_t ECHO_PIN     = 18;

const uint8_t BTN_ACTIVATE = 19;
const uint8_t BTN_PAIRING  = 23;

const uint8_t LED_PAIRING  = 25;  // Green
const uint8_t LED_DATA     = 26;  // Blue

// -------------------- TIMING & DEBOUNCE ---------------------

const unsigned long DEBOUNCE_DELAY_MS = 50UL;
const unsigned long ECHO_TIMEOUT_US = 30000UL;
const unsigned long TRANSMISSION_INTERVAL_MS = 10000UL;

const uint8_t SOIL_SAMPLES = 10;
const uint8_t ULTRASONIC_SAMPLES = 5;

// -------------------- SENSOR LIMITS ------------------------

const float MIN_DISTANCE_CM = 20.0f;
const float MAX_DISTANCE_CM = 450.0f;
const float SPEED_OF_SOUND_CM_PER_US = 0.0343f;

const int SOIL_AIR_VALUE = 3200;
const int SOIL_WATER_VALUE = 1500;

const float SENSOR_DATUM_OFFSET_CM = 20.0f;

// -------------------- PERSISTENT STATE ----------------------

Preferences prefs;

String nodeId = "NODE-01";
String macAddress = "";

bool isPairedAndApproved = false;
bool isMonitoringActive = false;

// -------------------- BUTTON TRACKERS -----------------------

int lastActivateBtnReading = HIGH;
int activateBtnState = HIGH;
unsigned long lastActivateDebounceTime = 0;

int lastPairingBtnReading = HIGH;
int pairingBtnState = HIGH;
unsigned long lastPairingDebounceTime = 0;

// -------------------- RUNTIME STATE -------------------------

unsigned long lastTransmissionTime = 0;
unsigned long telemetrySeq = 0;

// ============================================================
// LED STATUS HELPERS
// ============================================================

// Green LED is solid ON only when the node is approved.
// During pairing, it temporarily turns ON while the request
// is being processed, then returns to the approved state.

void updatePairingLed() {
    digitalWrite(
        LED_PAIRING,
        isPairedAndApproved ? HIGH : LOW
    );
}

// Flash the blue LED a specified number of times.

void flashDataLed(
    uint8_t flashes,
    unsigned int onMs = 100,
    unsigned int offMs = 100
) {
    for (uint8_t i = 0; i < flashes; i++) {
        digitalWrite(LED_DATA, HIGH);
        delay(onMs);

        digitalWrite(LED_DATA, LOW);

        if (i + 1 < flashes) {
            delay(offMs);
        }
    }
}

// ============================================================
// WIFI CONNECTION
// ============================================================

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.print(F("[WIFI] Connecting to SSID: "));
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;

    while (
        WiFi.status() != WL_CONNECTED &&
        attempts < 20
    ) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();

        Serial.print(F("[WIFI] Connected! IP: "));
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();

        Serial.println(
            F("[WIFI] Connection timed out. Will retry next cycle.")
        );
    }
}

// ============================================================
// SENSOR READINGS
// ============================================================

int readRawSoilMoisture() {
    uint32_t total = 0;

    for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
        total += analogRead(SOIL_PIN);
        delay(10);
    }

    return total / SOIL_SAMPLES;
}

float calculateSoilMoisturePercent(int rawAdc) {
    float pct =
        ((float)(SOIL_AIR_VALUE - rawAdc) /
         (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE)) * 100.0f;

    if (pct < 0.0f) {
        pct = 0.0f;
    }

    if (pct > 100.0f) {
        pct = 100.0f;
    }

    return pct;
}

float readSingleUltrasonicCM() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(3);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG_PIN, LOW);

    unsigned long duration = pulseIn(
        ECHO_PIN,
        HIGH,
        ECHO_TIMEOUT_US
    );

    if (duration == 0) {
        return -1.0f;
    }

    return (duration * SPEED_OF_SOUND_CM_PER_US) / 2.0f;
}

bool measureUltrasonicDistance(float &distanceCm) {
    float sum = 0.0f;
    uint8_t validCount = 0;

    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++) {
        float d = readSingleUltrasonicCM();

        if (
            d >= MIN_DISTANCE_CM &&
            d <= MAX_DISTANCE_CM
        ) {
            sum += d;
            validCount++;
        }

        delay(40);
    }

    if (validCount > 0) {
        distanceCm = sum / validCount;
        return true;
    }

    return false;
}

// ============================================================
// PAIRING REQUEST
// ============================================================

void sendPairingRequest() {
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("[PAIRING] Pairing request initiated"));

    Serial.printf(
        "[PAIRING] Node: %s (%s)\n",
        nodeId.c_str(),
        macAddress.c_str()
    );

    Serial.println(F("========================================"));

    // Indicate pairing activity.
    digitalWrite(LED_PAIRING, HIGH);

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(
                F("[PAIRING] Cannot send request: Wi-Fi offline")
            );

            updatePairingLed();
            return;
        }
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    String pairingUrl =
        String(BACKEND_BASE_URL) + "/pairing/request";

    if (!http.begin(client, pairingUrl)) {
        Serial.println(
            F("[PAIRING] HTTP client initialization failed")
        );

        updatePairingLed();
        return;
    }

    http.addHeader("Content-Type", "application/json");

    http.addHeader(
        "X-ArvaCore-Gateway-Key",
        GATEWAY_KEY
    );

    String payload = "{";

    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"macAddress\":\"" + macAddress + "\",";
    payload += "\"hardwareModel\":\"ESP32-WROOM-32\",";
    payload += "\"firmwareVersion\":\"2.0.0\"";

    payload += "}";

    Serial.printf(
        "[PAIRING] POST %s\n",
        pairingUrl.c_str()
    );

    Serial.printf(
        "[PAIRING] Payload: %s\n",
        payload.c_str()
    );

    int httpCode = http.POST(payload);

    if (httpCode == 200 || httpCode == 201) {
        String response = http.getString();

        Serial.printf(
            "[PAIRING] HTTP %d. Response: %s\n",
            httpCode,
            response.c_str()
        );

        if (
            response.indexOf(
                "\"pairingStatus\":\"APPROVED\""
            ) >= 0
        ) {
            isPairedAndApproved = true;
            prefs.putBool("paired", true);

            Serial.println(
                F("[PAIRING] Node APPROVED by backend")
            );
        } else if (
            response.indexOf(
                "\"pairingStatus\":\"PENDING\""
            ) >= 0
        ) {
            isPairedAndApproved = false;
            prefs.putBool("paired", false);

            Serial.println(
                F("[PAIRING] Awaiting administrator approval")
            );
        } else if (
            response.indexOf(
                "\"pairingStatus\":\"REJECTED\""
            ) >= 0
        ) {
            isPairedAndApproved = false;
            prefs.putBool("paired", false);

            Serial.println(
                F("[PAIRING] Request rejected by administrator")
            );
        } else {
            Serial.println(
                F("[PAIRING] Response did not contain a recognized status")
            );
        }
    } else {
        Serial.printf(
            "[PAIRING] Request failed. HTTP %d: %s\n",
            httpCode,
            http.errorToString(httpCode).c_str()
        );

        String errResp = http.getString();

        if (errResp.length() > 0) {
            Serial.printf(
                "[PAIRING] Error response: %s\n",
                errResp.c_str()
            );
        }

        if (httpCode == 401) {
            Serial.println(
                F("[PAIRING_ERROR] Unauthorized: check gateway key")
            );
        } else if (
            httpCode == 400 &&
            errResp.indexOf("MASTER_OFF") >= 0
        ) {
            Serial.println(
                F("[PAIRING_ERROR] Master system is OFF")
            );
        } else if (httpCode <= 0) {
            Serial.println(
                F("[PAIRING_ERROR] Server unreachable or connection failed")
            );

            // Preserve the original backend failover behavior.
            if (BACKEND_BASE_URL == REMOTE_BACKEND_URL) {
                Serial.printf(
                    "[FAILOVER] Remote to local: %s\n",
                    LOCAL_BACKEND_URL
                );

                BACKEND_BASE_URL = LOCAL_BACKEND_URL;
            } else {
                Serial.printf(
                    "[FAILOVER] Local to remote: %s\n",
                    REMOTE_BACKEND_URL
                );

                BACKEND_BASE_URL = REMOTE_BACKEND_URL;
            }
        }
    }

    http.end();

    // Restore the final pairing indicator state.
    updatePairingLed();
}

// ============================================================
// TELEMETRY TRANSMISSION
// ============================================================

void sendTelemetry(
    float distanceCm,
    bool distanceValid,
    float soilMoisturePercent
) {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();

        if (WiFi.status() != WL_CONNECTED) {
            digitalWrite(LED_DATA, LOW);
            return;
        }
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    String uplinkUrl =
        String(BACKEND_BASE_URL) +
        "/telemetry/uplink/" +
        nodeId;

    if (!http.begin(client, uplinkUrl)) {
        Serial.println(
            F("[HTTP] Failed to initialize connection")
        );

        flashDataLed(3);
        return;
    }

    http.addHeader("Content-Type", "application/json");

    http.addHeader(
        "X-ArvaCore-Gateway-Key",
        GATEWAY_KEY
    );

    telemetrySeq++;

    float waterLevelCm = 0.0f;

    if (distanceValid) {
        waterLevelCm =
            SENSOR_DATUM_OFFSET_CM - distanceCm;
    }

    String json = "{";

    json += "\"nodeId\":\"" + nodeId + "\",";
    json += "\"seq\":" + String(telemetrySeq) + ",";
    json += "\"batteryVoltage\":4.15,";
    json += "\"batteryLevel\":95.0,";
    json += "\"soilMoisturePercent\":" +
            String(soilMoisturePercent, 2) + ",";

    if (distanceValid) {
        json += "\"water_level_distance\":" +
                String(distanceCm, 2) + ",";

        json += "\"waterLevelCm\":" +
                String(waterLevelCm, 2) + ",";
    }

    json += "\"measurements\":{";

    json += "\"soil_moisture\":" +
            String(soilMoisturePercent, 2);

    if (distanceValid) {
        json += ",\"water_level_distance\":" +
                String(distanceCm, 2);
    }

    json += "}";
    json += "}";

    Serial.println();

    Serial.printf(
        "[HTTP] Uplinking telemetry to %s\n",
        uplinkUrl.c_str()
    );

    Serial.printf(
        "[HTTP] Payload: %s\n",
        json.c_str()
    );

    // Blue LED stays ON during the HTTP POST operation.
    digitalWrite(LED_DATA, HIGH);

    int httpCode = http.POST(json);

    digitalWrite(LED_DATA, LOW);

    if (httpCode > 0) {
        Serial.printf(
            "[HTTP] Uplink HTTP %d\n",
            httpCode
        );

        String response = http.getString();

        Serial.printf(
            "[HTTP] Server Response: %s\n",
            response.c_str()
        );

        if (response.indexOf("MASTER_OFF") >= 0) {
            Serial.println(
                F("[SYSTEM] Master system is OFF; telemetry may be suspended")
            );
        }
    } else {
        Serial.printf(
            "[HTTP] POST failed: %s\n",
            http.errorToString(httpCode).c_str()
        );

        // Preserve the original backend failover behavior.
        if (BACKEND_BASE_URL == REMOTE_BACKEND_URL) {
            Serial.printf(
                "[FAILOVER] Remote to local: %s\n",
                LOCAL_BACKEND_URL
            );

            BACKEND_BASE_URL = LOCAL_BACKEND_URL;
        } else {
            Serial.printf(
                "[FAILOVER] Local to remote: %s\n",
                REMOTE_BACKEND_URL
            );

            BACKEND_BASE_URL = REMOTE_BACKEND_URL;
        }
    }

    http.end();

    // Two flashes: HTTP success status (2xx).
    // Three flashes: network failure or non-2xx status.
    if (httpCode >= 200 && httpCode < 300) {
        flashDataLed(2);

        Serial.println(
            F("[LED] Telemetry request returned HTTP success")
        );
    } else {
        flashDataLed(3);

        Serial.println(
            F("[LED] Telemetry transmission failed")
        );
    }
}

// ============================================================
// BUTTON HANDLING
// ============================================================

void handleButtons() {
    unsigned long now = millis();

    // Activation Button: GPIO 19
    int readingActivate = digitalRead(BTN_ACTIVATE);

    if (readingActivate != lastActivateBtnReading) {
        lastActivateDebounceTime = now;
    }

    if (
        (now - lastActivateDebounceTime) >
        DEBOUNCE_DELAY_MS
    ) {
        if (readingActivate != activateBtnState) {
            activateBtnState = readingActivate;

            if (activateBtnState == LOW) {
                if (!isPairedAndApproved) {
                    Serial.println();

                    Serial.println(
                        F("[ACTIVATION] REJECTED: node is not approved")
                    );

                    Serial.println(
                        F("[ACTIVATION] Press pairing button and obtain approval")
                    );
                } else {
                    isMonitoringActive = !isMonitoringActive;

                    prefs.putBool(
                        "active",
                        isMonitoringActive
                    );

                    Serial.println();

                    Serial.printf(
                        "[ACTIVATION] Monitoring: %s\n",
                        isMonitoringActive ? "ENABLED" : "DISABLED"
                    );
                }
            }
        }
    }

    lastActivateBtnReading = readingActivate;

    // Pairing Button: GPIO 23
    int readingPairing = digitalRead(BTN_PAIRING);

    if (readingPairing != lastPairingBtnReading) {
        lastPairingDebounceTime = now;
    }

    if (
        (now - lastPairingDebounceTime) >
        DEBOUNCE_DELAY_MS
    ) {
        if (readingPairing != pairingBtnState) {
            pairingBtnState = readingPairing;

            if (pairingBtnState == LOW) {
                sendPairingRequest();
            }
        }
    }

    lastPairingBtnReading = readingPairing;
}

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Initialize LEDs.
    pinMode(LED_PAIRING, OUTPUT);
    pinMode(LED_DATA, OUTPUT);

    digitalWrite(LED_PAIRING, LOW);
    digitalWrite(LED_DATA, LOW);

    // Initialize existing sensors.
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    digitalWrite(TRIG_PIN, LOW);

    // Initialize existing buttons.
    pinMode(BTN_ACTIVATE, INPUT_PULLUP);
    pinMode(BTN_PAIRING, INPUT_PULLUP);

    analogReadResolution(12);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);

    // Persistent state.
    prefs.begin("arvanode", false);

    isPairedAndApproved = prefs.getBool("paired", false);
    isMonitoringActive = prefs.getBool("active", false);

    updatePairingLed();

    // Connect Wi-Fi.
    connectWiFi();

    // Read Wi-Fi MAC address.
    uint8_t baseMac[6] = {0};

    esp_err_t ret = esp_read_mac(
        baseMac,
        ESP_MAC_WIFI_STA
    );

    if (
        ret == ESP_OK &&
        (baseMac[0] != 0 ||
         baseMac[1] != 0 ||
         baseMac[2] != 0)
    ) {
        char macBuf[18];

        snprintf(
            macBuf,
            sizeof(macBuf),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            baseMac[0],
            baseMac[1],
            baseMac[2],
            baseMac[3],
            baseMac[4],
            baseMac[5]
        );

        macAddress = String(macBuf);
    } else {
        String wifiMac = WiFi.macAddress();

        if (
            wifiMac.length() > 0 &&
            wifiMac != "00:00:00:00:00:00"
        ) {
            macAddress = wifiMac;
        }
    }

    // Generate a stable node ID from the MAC suffix.
    String macSuffix = macAddress;
    macSuffix.replace(":", "");

    if (
        macSuffix.length() >= 4 &&
        macSuffix != "000000000000"
    ) {
        nodeId =
            "NODE-" +
            macSuffix.substring(macSuffix.length() - 4);
    } else {
        nodeId = "NODE-01";
    }

    Serial.println();

    Serial.println(
        F("========================================")
    );

    Serial.println(
        F(" ARVACORE / ARVANODE - AWD SENSOR NODE")
    );

    Serial.println(
        F(" Pairing, Activation and LED Status")
    );

    Serial.println(
        F("========================================")
    );

    Serial.printf(
        "Stable Node ID: %s\n",
        nodeId.c_str()
    );

    Serial.printf(
        "Hardware MAC:   %s\n",
        macAddress.c_str()
    );

    Serial.printf(
        "Paired Status:  %s\n",
        isPairedAndApproved ? "APPROVED" : "UNPAIRED / PENDING"
    );

    Serial.printf(
        "Telemetry Mode: %s\n",
        isMonitoringActive ? "ACTIVE" : "STANDBY"
    );

    Serial.println(F("Pins:"));
    Serial.println(F("  Soil AO:           GPIO 34"));
    Serial.println(F("  Ultrasonic TRIG:   GPIO 5"));
    Serial.println(F("  Ultrasonic ECHO:   GPIO 18"));
    Serial.println(F("  Activation Button: GPIO 19"));
    Serial.println(F("  Pairing Button:    GPIO 23"));
    Serial.println(F("  Pairing LED:       GPIO 25 (green)"));
    Serial.println(F("  Data LED:          GPIO 26 (blue)"));

    Serial.println(
        F("========================================")
    );

    // Automatically submit a pairing request if unpaired.
    if (
        !isPairedAndApproved &&
        WiFi.status() == WL_CONNECTED
    ) {
        Serial.println(
            F("[BOOT] Unpaired node; submitting pairing request")
        );

        sendPairingRequest();
    }
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
    // Process physical buttons.
    handleButtons();

    unsigned long currentTime = millis();

    // Acquire and transmit telemetry only when monitoring is active.
    if (
        isMonitoringActive &&
        (currentTime - lastTransmissionTime >=
         TRANSMISSION_INTERVAL_MS)
    ) {
        lastTransmissionTime = currentTime;

        // 1. Read soil moisture.
        int rawSoilAdc = readRawSoilMoisture();

        float soilMoisturePercent =
            calculateSoilMoisturePercent(rawSoilAdc);

        // 2. Read ultrasonic distance.
        float distanceCm = 0.0f;

        bool distanceValid =
            measureUltrasonicDistance(distanceCm);

        Serial.println();

        Serial.println(
            F("--- Sensor Acquisition Cycle ---")
        );

        Serial.printf(
            "Soil ADC: %d | Moisture: %.2f%%\n",
            rawSoilAdc,
            soilMoisturePercent
        );

        if (distanceValid) {
            float waterLevelCm =
                SENSOR_DATUM_OFFSET_CM - distanceCm;

            Serial.printf(
                "Ultrasonic Distance: %.2f cm | Water Level: %.2f cm\n",
                distanceCm,
                waterLevelCm
            );
        } else {
            Serial.println(
                F("Ultrasonic Distance: invalid, out of range, or timeout")
            );
        }

        // 3. Transmit telemetry.
        sendTelemetry(
            distanceCm,
            distanceValid,
            soilMoisturePercent
        );
    }
}