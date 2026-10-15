
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "esp_system.h"
#include "esp_mac.h"

// ============================================================
// ARVACORE / ARVANODE SENSOR NODE FIRMWARE
// Board: ESP32 Dev Module / ESP32-WROOM-32
//
// GPIO 34 : Soil moisture AO
// GPIO 5  : Ultrasonic TRIG
// GPIO 18 : Ultrasonic ECHO
// GPIO 19 : Activation button, active LOW
// GPIO 23 : Pairing button, active LOW
// GPIO 25 : Green pairing/status LED
// GPIO 26 : Blue monitoring LED
//
// OPERATION
// - Monitoring starts OFF after reboot.
// - Pairing button submits a pairing request.
// - Activation requires backend approval and runs a hardware audit.
// - Every 30 seconds, backend pairing status is verified.
// - Telemetry is sent only after backend confirms APPROVED.
// - Unknown state, HTTP failure, or connection failure blocks telemetry.
// - PENDING, REJECTED, or REVOKED stops monitoring.
// - Switching activation OFF stops periodic monitoring.
//
// SECURITY NOTE
// WiFiClientSecure::setInsecure() disables TLS certificate validation.
// ============================================================

// ---------------- NETWORK CONFIGURATION ----------------------

const char* WIFI_SSID = "Cream";
const char* WIFI_PASSWORD = "hatdog123";

const char* BACKEND_BASE_URL =
    "https://api-aquaflow.gladwindr.me/api/v1";

const char* GATEWAY_KEY = "YOUR_GATEWAY_KEY";

const char* PAIRING_STATUS_PATH = "/pairing/status/";

// ---------------- GPIO ---------------------------------------

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t TRIG_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;

constexpr uint8_t BTN_ACTIVATE = 19;
constexpr uint8_t BTN_PAIRING = 23;

constexpr uint8_t LED_PAIRING = 25;
constexpr uint8_t LED_DATA = 26;

// ---------------- TIMING -------------------------------------

constexpr unsigned long DEBOUNCE_MS = 50;
constexpr unsigned long WIFI_TIMEOUT_MS = 10000;
constexpr unsigned long HTTP_TIMEOUT_MS = 10000;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 30000;

// ---------------- SENSOR CONFIGURATION -----------------------

constexpr uint8_t SOIL_SAMPLES = 10;
constexpr uint8_t ULTRASONIC_SAMPLES = 5;

constexpr unsigned long ECHO_TIMEOUT_US = 30000;

constexpr float MIN_DISTANCE_CM = 20.0f;
constexpr float MAX_DISTANCE_CM = 450.0f;
constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;

constexpr int SOIL_AIR_VALUE = 3200;
constexpr int SOIL_WATER_VALUE = 1500;

constexpr float SENSOR_DATUM_OFFSET_CM = 20.0f;

// ---------------- PAIRING STATES -----------------------------

enum PairingState {
    PAIRING_UNVERIFIED,
    PAIRING_PENDING,
    PAIRING_APPROVED,
    PAIRING_REJECTED
};

Preferences prefs;

String nodeId;
String macAddress;

PairingState pairingState = PAIRING_UNVERIFIED;

bool isPairedAndApproved = false;
bool isMonitoringActive = false;

unsigned long lastTransmissionTime = 0;
unsigned long telemetrySequence = 0;

unsigned long auditCount = 0;
unsigned long pairingAttempts = 0;
unsigned long pairingSuccesses = 0;
unsigned long pairingFailures = 0;
unsigned long statusChecks = 0;
unsigned long telemetryAttempts = 0;
unsigned long telemetrySuccesses = 0;
unsigned long telemetryFailures = 0;

// ---------------- BUTTON DEBOUNCE ----------------------------

int lastActivateReading = HIGH;
int activateStableState = HIGH;
unsigned long activateDebounceAt = 0;

int lastPairingReading = HIGH;
int pairingStableState = HIGH;
unsigned long pairingDebounceAt = 0;

// ============================================================
// SERIAL LOGGING
// ============================================================

void printSection(const char* title) {
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.print(F("  "));
    Serial.println(title);
    Serial.println(F("=================================================="));
}

void info(const String& message) {
    Serial.print(F("[INFO] "));
    Serial.println(message);
}

void success(const String& message) {
    Serial.print(F("[SUCCESS] "));
    Serial.println(message);
}

void warning(const String& message) {
    Serial.print(F("[WARNING] "));
    Serial.println(message);
}

void errorLog(const String& message) {
    Serial.print(F("[ERROR] "));
    Serial.println(message);
}

void action(const String& message) {
    Serial.print(F("[ACTION] "));
    Serial.println(message);
}

// ============================================================
// PAIRING STATE
// ============================================================

const char* pairingStateName(PairingState state) {
    switch (state) {
        case PAIRING_PENDING:
            return "PENDING";

        case PAIRING_APPROVED:
            return "APPROVED";

        case PAIRING_REJECTED:
            return "REJECTED";

        default:
            return "UNVERIFIED";
    }
}

void updateLeds() {
    digitalWrite(
        LED_PAIRING,
        isPairedAndApproved ? HIGH : LOW
    );

    digitalWrite(
        LED_DATA,
        isMonitoringActive ? HIGH : LOW
    );
}

void applyPairingState(PairingState newState) {
    pairingState = newState;

    switch (newState) {
        case PAIRING_APPROVED:
            isPairedAndApproved = true;
            prefs.putBool("paired", true);
            digitalWrite(LED_PAIRING, HIGH);
            break;

        case PAIRING_PENDING:
        case PAIRING_REJECTED:
            isPairedAndApproved = false;
            prefs.putBool("paired", false);

            isMonitoringActive = false;
            prefs.putBool("active", false);

            digitalWrite(LED_PAIRING, LOW);
            digitalWrite(LED_DATA, LOW);
            break;

        case PAIRING_UNVERIFIED:
        default:
            // Cached approval may be retained for diagnostics, but it
            // is not authorization to send telemetry.
            digitalWrite(
                LED_PAIRING,
                isPairedAndApproved ? HIGH : LOW
            );
            break;
    }

    Serial.printf(
        "[PAIRING] Current state: %s\n",
        pairingStateName(pairingState)
    );
}

// ============================================================
// WIFI
// ============================================================

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    action(String("Connecting to Wi-Fi SSID: ") + WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < WIFI_TIMEOUT_MS
    ) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        errorLog(
            "Wi-Fi connection failed within the configured timeout."
        );

        info(
            "Check SSID, password, router availability, and 2.4 GHz support."
        );

        return false;
    }

    Serial.printf(
        "[WIFI] Connected. IP address: %s\n",
        WiFi.localIP().toString().c_str()
    );

    Serial.printf(
        "[WIFI] Signal strength: %d dBm\n",
        WiFi.RSSI()
    );

    return true;
}

// ============================================================
// HTTPS CLIENT
// ============================================================

bool beginSecureHttp(
    HTTPClient& http,
    WiFiClientSecure& client,
    const String& url
) {
    if (!connectWiFi()) {
        return false;
    }

    // Certificate verification is intentionally disabled.
    // Do not use this configuration for security-sensitive production use.
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, url)) {
        errorLog(
            String("Could not initialize HTTPS request to ") + url
        );

        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

    return true;
}

// ============================================================
// JSON STRING FIELD EXTRACTION
// Handles simple, unescaped JSON string fields.
// For arbitrary JSON, use ArduinoJson.
// ============================================================

bool extractJsonString(
    const String& body,
    const char* key,
    String& output
) {
    String searchKey = String("\"") + key + "\"";

    int keyPos = body.indexOf(searchKey);

    if (keyPos < 0) {
        return false;
    }

    int colonPos = body.indexOf(':', keyPos + searchKey.length());

    if (colonPos < 0) {
        return false;
    }

    int firstQuote = body.indexOf('"', colonPos + 1);

    if (firstQuote < 0) {
        return false;
    }

    int secondQuote = body.indexOf('"', firstQuote + 1);

    if (secondQuote < 0) {
        return false;
    }

    output = body.substring(firstQuote + 1, secondQuote);
    output.trim();

    return true;
}

// ============================================================
// AUTHORITATIVE BACKEND PAIRING STATUS
//
// Required endpoint:
// GET /api/v1/pairing/status/{nodeId}
//
// Expected JSON:
// {"nodeId":"NODE-A1B2","pairingStatus":"APPROVED"}
//
// Recognized statuses:
// APPROVED, PENDING, REJECTED, REVOKED
//
// Any error or unknown response blocks telemetry.
// ============================================================

bool verifyBackendPairingStatus() {
    printSection("AUTHORITATIVE BACKEND PAIRING CHECK");

    statusChecks++;

    Serial.printf(
        "[PAIRING CHECK] Check number: %lu\n",
        statusChecks
    );

    Serial.printf(
        "[PAIRING CHECK] Node ID: %s\n",
        nodeId.c_str()
    );

    String url =
        String(BACKEND_BASE_URL) +
        PAIRING_STATUS_PATH +
        nodeId;

    Serial.printf("[PAIRING CHECK] GET %s\n", url.c_str());

    WiFiClientSecure client;
    HTTPClient http;

    if (!beginSecureHttp(http, client, url)) {
        applyPairingState(PAIRING_UNVERIFIED);

        errorLog(
            "Cannot verify pairing status. Telemetry is blocked."
        );

        return false;
    }

    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();

    String response;
    String transportError;

    if (httpCode > 0) {
        response = http.getString();
    } else {
        transportError = http.errorToString(httpCode);
    }

    http.end();

    Serial.printf(
        "[PAIRING CHECK] HTTP status: %d\n",
        httpCode
    );

    if (response.length() > 0) {
        Serial.printf(
            "[PAIRING CHECK] Response: %s\n",
            response.c_str()
        );
    }

    if (httpCode < 0) {
        errorLog(
            String("Pairing status transport failure: ") +
            transportError
        );

        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (httpCode == 404) {
        errorLog(
            "HTTP 404: pairing-status endpoint or node was not found. "
            "Confirm that the backend route is implemented."
        );

        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        errorLog(
            String("Pairing status endpoint returned HTTP ") +
            String(httpCode) + "."
        );

        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    String responseNodeId;
    String status;

    if (
        !extractJsonString(response, "nodeId", responseNodeId) ||
        !extractJsonString(response, "pairingStatus", status)
    ) {
        errorLog(
            "Pairing status response is missing nodeId or pairingStatus."
        );

        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    responseNodeId.trim();
    status.trim();
    status.toUpperCase();

    if (responseNodeId != nodeId) {
        errorLog(
            "Backend returned a different node ID. Authorization denied."
        );

        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (status == "APPROVED") {
        applyPairingState(PAIRING_APPROVED);

        success(
            "Backend confirms that this node is currently APPROVED."
        );

        return true;
    }

    if (status == "PENDING") {
        warning(
            "Backend reports PENDING. Monitoring and telemetry are stopped."
        );

        applyPairingState(PAIRING_PENDING);
        return false;
    }

    if (status == "REJECTED" || status == "REVOKED") {
        warning(
            "Backend reports REJECTED or REVOKED. Monitoring and telemetry "
            "are stopped."
        );

        applyPairingState(PAIRING_REJECTED);
        return false;
    }

    errorLog(
        String("Unrecognized pairingStatus value: ") + status
    );

    applyPairingState(PAIRING_UNVERIFIED);
    return false;
}

// ============================================================
// LED FEEDBACK
// ============================================================

void flashPairingLed(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(LED_PAIRING, LOW);
        delay(180);

        digitalWrite(LED_PAIRING, HIGH);
        delay(220);
    }

    updateLeds();
}

// ============================================================
// SOIL MOISTURE SENSOR
// ============================================================

int readSoilADC() {
    uint32_t total = 0;
    int minimum = 4095;
    int maximum = 0;

    for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
        int reading = analogRead(SOIL_PIN);

        total += reading;

        if (reading < minimum) minimum = reading;
        if (reading > maximum) maximum = reading;

        delay(10);
    }

    int average = total / SOIL_SAMPLES;

    Serial.printf(
        "[SOIL] ADC average: %d; minimum: %d; maximum: %d\n",
        average,
        minimum,
        maximum
    );

    if (average <= 0 || average >= 4095) {
        warning(
            "Soil ADC is at a measurement boundary. Inspect sensor wiring."
        );
    }

    return average;
}

float soilPercentFromADC(int raw) {
    float denominator =
        (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE);

    if (denominator == 0.0f) {
        errorLog("Soil calibration values are invalid.");
        return 0.0f;
    }

    float percent =
        ((float)(SOIL_AIR_VALUE - raw) / denominator) * 100.0f;

    percent = constrain(percent, 0.0f, 100.0f);

    Serial.printf(
        "[SOIL] Calculated moisture: %.2f%%\n",
        percent
    );

    return percent;
}

// ============================================================
// ULTRASONIC SENSOR
// ============================================================

float readUltrasonicSingleCM() {
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

    return duration * SOUND_SPEED_CM_PER_US / 2.0f;
}

bool readUltrasonicAverage(float& result) {
    float total = 0.0f;
    uint8_t valid = 0;

    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++) {
        float cm = readUltrasonicSingleCM();

        if (cm >= MIN_DISTANCE_CM && cm <= MAX_DISTANCE_CM) {
            Serial.printf(
                "[ULTRASONIC] Sample %u: %.2f cm\n",
                i + 1,
                cm
            );

            total += cm;
            valid++;
        } else {
            Serial.printf(
                "[ULTRASONIC] Sample %u invalid or timed out.\n",
                i + 1
            );
        }

        delay(40);
    }

    if (valid == 0) {
        errorLog("No valid ultrasonic readings were obtained.");
        return false;
    }

    result = total / valid;

    Serial.printf(
        "[ULTRASONIC] Average distance: %.2f cm\n",
        result
    );

    return true;
}

// ============================================================
// ACTIVATION HARDWARE AUDIT
// ============================================================

void runActivationAudit() {
    auditCount++;

    printSection("ACTIVATION HARDWARE AUDIT");

    Serial.printf("[AUDIT] Audit number: %lu\n", auditCount);
    Serial.printf("[AUDIT] Node ID: %s\n", nodeId.c_str());
    Serial.printf("[AUDIT] MAC address: %s\n", macAddress.c_str());

    info("Checking Wi-Fi connectivity.");

    bool wifiOk = connectWiFi();

    Serial.printf(
        "[AUDIT] Wi-Fi: %s\n",
        wifiOk ? "CONNECTED" : "DISCONNECTED"
    );

    info("Testing soil moisture sensor.");

    int rawSoil = readSoilADC();
    float soil = soilPercentFromADC(rawSoil);

    info("Testing ultrasonic sensor.");

    float distance = 0.0f;
    bool distanceOk = readUltrasonicAverage(distance);

    printSection("HARDWARE AUDIT SUMMARY");

    Serial.printf("[AUDIT] Soil moisture: %.2f%%\n", soil);

    if (distanceOk) {
        Serial.printf(
            "[AUDIT] Ultrasonic distance: %.2f cm\n",
            distance
        );
    } else {
        warning("Ultrasonic sensor did not return a valid distance.");
    }

    Serial.printf(
        "[AUDIT] Wi-Fi: %s\n",
        wifiOk ? "PASS" : "FAIL"
    );

    success("Hardware audit routine completed.");
}

// ============================================================
// PAIRING REQUEST
// ============================================================

void requestPairing() {
    printSection("PAIRING REQUEST");

    if (!connectWiFi()) {
        pairingFailures++;
        errorLog("Pairing request cancelled: Wi-Fi is unavailable.");
        flashPairingLed(3);
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;

    String url =
        String(BACKEND_BASE_URL) + "/pairing/request";

    if (!beginSecureHttp(http, client, url)) {
        pairingFailures++;
        errorLog("Pairing request blocked: HTTPS initialization failed.");
        flashPairingLed(3);
        return;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    String payload = "{";
    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"macAddress\":\"" + macAddress + "\",";
    payload += "\"hardwareModel\":\"ESP32-WROOM-32\",";
    payload += "\"firmwareVersion\":\"3.1.0\"";
    payload += "}";

    pairingAttempts++;

    Serial.printf("[PAIRING] POST %s\n", url.c_str());
    Serial.printf("[PAIRING] Payload: %s\n", payload.c_str());

    int httpCode = http.POST(payload);
    String response = httpCode > 0 ? http.getString() : "";

    http.end();

    Serial.printf("[PAIRING] HTTP status: %d\n", httpCode);

    if (response.length()) {
        Serial.printf("[PAIRING] Response: %s\n", response.c_str());
    }

    if (httpCode < 200 || httpCode >= 300) {
        pairingFailures++;

        errorLog(
            String("Pairing request failed with HTTP status ") +
            String(httpCode) + "."
        );

        flashPairingLed(3);
        return;
    }

    String returnedNode;
    String status;

    if (
        !extractJsonString(response, "nodeId", returnedNode) ||
        !extractJsonString(response, "pairingStatus", status)
    ) {
        pairingFailures++;

        errorLog(
            "Pairing response is missing nodeId or pairingStatus."
        );

        applyPairingState(PAIRING_UNVERIFIED);
        flashPairingLed(3);
        return;
    }

    status.toUpperCase();

    if (returnedNode != nodeId) {
        pairingFailures++;

        errorLog("Pairing response node ID does not match this device.");

        applyPairingState(PAIRING_UNVERIFIED);
        flashPairingLed(3);
        return;
    }

    if (status == "APPROVED") {
        pairingSuccesses++;

        applyPairingState(PAIRING_APPROVED);

        success("Backend approved the node.");

        info(
            "Monitoring remains OFF. Press activation to run the hardware audit."
        );

        flashPairingLed(1);
        return;
    }

    if (status == "PENDING") {
        applyPairingState(PAIRING_PENDING);

        warning(
            "Pairing is pending. Wait for backend approval."
        );

        return;
    }

    if (status == "REJECTED" || status == "REVOKED") {
        pairingFailures++;

        applyPairingState(PAIRING_REJECTED);

        errorLog("Backend did not approve this node.");

        flashPairingLed(3);
        return;
    }

    pairingFailures++;

    applyPairingState(PAIRING_UNVERIFIED);

    errorLog(
        String("Unrecognized pairing status: ") + status
    );

    flashPairingLed(3);
}

// ============================================================
// TELEMETRY TRANSMISSION
// ============================================================

bool sendTelemetry(
    float soilPercent,
    float distanceCm,
    bool distanceValid
) {
    printSection("BACKEND TELEMETRY TRANSMISSION");

    if (!isMonitoringActive) {
        warning("Telemetry blocked: monitoring is OFF.");
        return false;
    }

    // Authoritative backend verification before sending.
    if (!verifyBackendPairingStatus()) {
        errorLog(
            "Telemetry blocked because current backend approval "
            "could not be confirmed."
        );

        return false;
    }

    if (!isMonitoringActive) {
        warning("Monitoring stopped during the status check.");
        return false;
    }

    WiFiClientSecure client;
    HTTPClient http;

    String url =
        String(BACKEND_BASE_URL) +
        "/telemetry/uplink/" +
        nodeId;

    if (!beginSecureHttp(http, client, url)) {
        telemetryFailures++;

        errorLog(
            "Telemetry blocked because the HTTPS request could not start."
        );

        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    telemetrySequence++;
    telemetryAttempts++;

    // These battery readings are placeholders, not real measurements.
    String payload = "{";

    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"seq\":" + String(telemetrySequence) + ",";
    payload += "\"batteryVoltage\":4.15,";
    payload += "\"batteryLevel\":95.0,";
    payload += "\"soilMoisturePercent\":" +
               String(soilPercent, 2) + ",";
    payload += "\"measurements\":{";
    payload += "\"soil_moisture\":" + String(soilPercent, 2);

    if (distanceValid) {
        payload += ",\"water_level_distance\":" +
                   String(distanceCm, 2);

        payload += ",\"waterLevelCm\":" +
                   String(SENSOR_DATUM_OFFSET_CM - distanceCm, 2);
    }

    payload += "}}";

    Serial.printf("[TELEMETRY] POST %s\n", url.c_str());
    Serial.printf("[TELEMETRY] Sequence: %lu\n", telemetrySequence);
    Serial.printf("[TELEMETRY] Payload: %s\n", payload.c_str());

    int httpCode = http.POST(payload);
    String response = httpCode > 0 ? http.getString() : "";
    String transportError =
        httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf("[TELEMETRY] HTTP status: %d\n", httpCode);

    if (httpCode >= 200 && httpCode < 300) {
        telemetrySuccesses++;

        success(
            String("Telemetry sequence ") +
            String(telemetrySequence) +
            " was accepted by the backend."
        );

        if (response.length()) {
            Serial.printf("[TELEMETRY] Response: %s\n", response.c_str());
        }

        return true;
    }

    telemetryFailures++;

    errorLog(
        String("Telemetry request failed with HTTP status ") +
        String(httpCode) + "."
    );

    if (httpCode < 0) {
        errorLog(String("Transport error: ") + transportError);
    }

    if (response.length()) {
        Serial.printf(
            "[TELEMETRY] Error response: %s\n",
            response.c_str()
        );
    }

    return false;
}

// ============================================================
// 30-SECOND MONITORING CYCLE
// ============================================================

void runMonitoringCycle() {
    printSection("30-SECOND MONITORING CYCLE");

    if (!isMonitoringActive) {
        info("Monitoring is OFF. Skipping cycle.");
        return;
    }

    Serial.printf("[CYCLE] Node ID: %s\n", nodeId.c_str());

    info("STEP 1/3: Verify current backend pairing authorization.");

    if (!verifyBackendPairingStatus()) {
        errorLog(
            "Cycle stopped: backend approval was not confirmed."
        );

        info(
            "No telemetry will be sent. A future cycle may retry "
            "if monitoring remains active."
        );

        return;
    }

    if (!isMonitoringActive) {
        warning("Monitoring was stopped during the authorization check.");
        return;
    }

    info("STEP 2/3: Read sensor measurements.");

    if (!connectWiFi()) {
        errorLog("Cycle stopped because Wi-Fi is unavailable.");
        return;
    }

    int soilADC = readSoilADC();
    float soilPercent = soilPercentFromADC(soilADC);

    float distanceCm = 0.0f;
    bool distanceValid = readUltrasonicAverage(distanceCm);

    info("STEP 3/3: Send telemetry.");

    sendTelemetry(soilPercent, distanceCm, distanceValid);

    printSection("MONITORING CYCLE SUMMARY");

    Serial.printf(
        "[SUMMARY] Pairing: %s\n",
        pairingStateName(pairingState)
    );

    Serial.printf(
        "[SUMMARY] Monitoring: %s\n",
        isMonitoringActive ? "ON" : "OFF"
    );

    Serial.printf(
        "[SUMMARY] Telemetry attempts: %lu\n",
        telemetryAttempts
    );

    Serial.printf(
        "[SUMMARY] Successful transmissions: %lu\n",
        telemetrySuccesses
    );

    Serial.printf(
        "[SUMMARY] Failed transmissions: %lu\n",
        telemetryFailures
    );
}

// ============================================================
// ACTIVATION BUTTON
// ============================================================

void handleActivationButton() {
    unsigned long now = millis();
    int reading = digitalRead(BTN_ACTIVATE);

    if (reading != lastActivateReading) {
        activateDebounceAt = now;
    }

    if (
        now - activateDebounceAt >= DEBOUNCE_MS &&
        reading != activateStableState
    ) {
        activateStableState = reading;

        if (activateStableState == LOW) {
            printSection("ACTIVATION BUTTON PRESSED");

            if (!isMonitoringActive) {
                if (!isPairedAndApproved) {
                    errorLog(
                        "Activation denied: no cached approval is available."
                    );

                    info(
                        "Press the pairing button and obtain approval first."
                    );

                    updateLeds();
                    return;
                }

                info(
                    "Checking backend approval before activating monitoring."
                );

                if (!verifyBackendPairingStatus()) {
                    errorLog(
                        "Activation denied because current backend "
                        "approval could not be verified."
                    );

                    updateLeds();
                    return;
                }

                runActivationAudit();

                isMonitoringActive = true;
                prefs.putBool("active", true);

                lastTransmissionTime = millis();

                updateLeds();

                success("Monitoring activated.");
                info("Blue LED is ON.");
                info("First monitoring cycle starts after 30 seconds.");

            } else {
                isMonitoringActive = false;
                prefs.putBool("active", false);

                updateLeds();

                success("Monitoring deactivated.");
                info("Periodic sensor acquisition and telemetry have stopped.");
            }
        }
    }

    lastActivateReading = reading;
}

// ============================================================
// PAIRING BUTTON
// ============================================================

void handlePairingButton() {
    unsigned long now = millis();
    int reading = digitalRead(BTN_PAIRING);

    if (reading != lastPairingReading) {
        pairingDebounceAt = now;
    }

    if (
        now - pairingDebounceAt >= DEBOUNCE_MS &&
        reading != pairingStableState
    ) {
        pairingStableState = reading;

        if (pairingStableState == LOW) {
            requestPairing();
        }
    }

    lastPairingReading = reading;
}

// ============================================================
// STARTUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(700);

    Serial.println();
    Serial.println(F("##################################################"));
    Serial.println(F("#       ARVACORE / ARVANODE FIRMWARE              #"));
    Serial.println(F("#       Backend-Verified Pairing                 #"));
    Serial.println(F("##################################################"));

    printSection("HARDWARE INITIALIZATION");

    pinMode(LED_PAIRING, OUTPUT);
    pinMode(LED_DATA, OUTPUT);

    digitalWrite(LED_PAIRING, LOW);
    digitalWrite(LED_DATA, LOW);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    digitalWrite(TRIG_PIN, LOW);

    pinMode(BTN_ACTIVATE, INPUT_PULLUP);
    pinMode(BTN_PAIRING, INPUT_PULLUP);

    analogReadResolution(12);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);

    success("GPIO initialization completed.");

    printSection("PREFERENCES STORAGE");

    if (!prefs.begin("arvanode", false)) {
        errorLog("Failed to initialize Preferences storage.");

        while (true) {
            digitalWrite(LED_DATA, LOW);
            delay(1000);
        }
    }

    isPairedAndApproved = prefs.getBool("paired", false);

    // Cached approval must be revalidated with the backend.
    pairingState = PAIRING_UNVERIFIED;

    // Monitoring deliberately starts OFF after every reboot.
    isMonitoringActive = false;
    prefs.putBool("active", false);

    info(
        "Cached approval is not treated as current authorization. "
        "Online verification is required before activation."
    );

    printSection("NODE IDENTITY");

    WiFi.mode(WIFI_STA);

    uint8_t mac[6] = {0};

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char buffer[18];

        snprintf(
            buffer,
            sizeof(buffer),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2],
            mac[3], mac[4], mac[5]
        );

        macAddress = String(buffer);
    } else {
        macAddress = WiFi.macAddress();
    }

    String compactMac = macAddress;
    compactMac.replace(":", "");

    nodeId = compactMac.length() >= 4
        ? "NODE-" + compactMac.substring(compactMac.length() - 4)
        : "NODE-01";

    Serial.printf("[BOOT] Node ID: %s\n", nodeId.c_str());
    Serial.printf("[BOOT] MAC address: %s\n", macAddress.c_str());

    Serial.printf(
        "[BOOT] Backend: %s\n",
        BACKEND_BASE_URL
    );

    Serial.printf(
        "[BOOT] Pairing status endpoint: %s%s{nodeId}\n",
        BACKEND_BASE_URL,
        PAIRING_STATUS_PATH
    );

    printSection("BUTTON INITIALIZATION");

    lastActivateReading = digitalRead(BTN_ACTIVATE);
    activateStableState = lastActivateReading;
    activateDebounceAt = millis();

    lastPairingReading = digitalRead(BTN_PAIRING);
    pairingStableState = lastPairingReading;
    pairingDebounceAt = millis();

    updateLeds();

    printSection("STARTUP SUMMARY");

    Serial.printf(
        "[STATUS] Cached approval: %s\n",
        isPairedAndApproved ? "YES" : "NO"
    );

    Serial.println(F("[STATUS] Live backend status: NOT YET VERIFIED"));
    Serial.println(F("[STATUS] Monitoring: OFF"));
    Serial.println(F("[STATUS] Telemetry interval: 30 seconds"));

    info("No audit or telemetry is performed at startup.");

    Serial.println(
        F("[READY] Press GPIO 23 to request pairing.")
    );

    Serial.println(
        F("[READY] Press GPIO 19 to activate after approval.")
    );

    Serial.println();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
    handleActivationButton();
    handlePairingButton();

    unsigned long now = millis();

    if (
        isMonitoringActive &&
        now - lastTransmissionTime >= TELEMETRY_INTERVAL_MS
    ) {
        lastTransmissionTime = now;
        runMonitoringCycle();
    }

    delay(2);
}
