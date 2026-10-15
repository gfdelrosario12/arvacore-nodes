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
// HARDWARE CONNECTIONS:
//   GPIO 34 : Soil moisture AO (Capacitive/Analog)
//   GPIO 5  : Ultrasonic TRIG
//   GPIO 18 : Ultrasonic ECHO (via voltage divider)
//   GPIO 19 : Activation button (Active LOW, internal pull-up)
//   GPIO 23 : Pairing button (Active LOW, internal pull-up)
//   GPIO 26 : LED 1 - Activation & Monitoring LED (Blue)
//   GPIO 25 : LED 2 - Pairing & Status LED (Green)
//
// OPERATIONAL SPECIFICATION:
// 1. Standby / Power State:
//    - On boot, device starts in STANDBY / POWERED-OFF mode.
//    - The Pairing button is DISABLED until the Activation button is pressed.
// 2. Activation Button (GPIO 19):
//    - Powers on the device and enables the Pairing button.
//    - Queries the backend to verify if pairing is valid and not expired.
//    - If pairing is expired, invalid, or not approved:
//        -> Signals ERROR on LED 1 (3 blinks). Monitoring remains OFF.
//    - If pairing is valid & approved:
//        -> Executes comprehensive hardware audit (Wi-Fi, Soil ADC, Ultrasonic).
//        -> If all components PASS:
//             -> Signals SUCCESS on LED 1 (1 blink), then turns LED 1 solid ON.
//             -> Activates monitoring: gathers and transmits telemetry after 15 minutes.
//        -> If any component FAILS:
//             -> Signals ERROR on LED 1 (3 blinks). Monitoring remains OFF.
// 3. Pairing Button (GPIO 23):
//    - Only active if device has been powered on by the Activation button.
//    - Sends a pairing request to the ArvaCore backend API.
//    - On successful backend approval:
//        -> Signals SUCCESS on LED 2 (1 blink), then turns LED 2 solid ON.
//    - On error, rejection, pending, or connection failure:
//        -> Signals ERROR on LED 2 (3 blinks), turns LED 2 OFF.
// 4. Telemetry Cycle:
//    - Periodic telemetry acquisition every 15 minutes (900,000 ms).
// ============================================================

// ---------------- NETWORK CONFIGURATION ----------------------

const char* WIFI_SSID = "Cream";
const char* WIFI_PASSWORD = "hatdog123";

const char* BACKEND_BASE_URL =
    "https://api-aquaflow.gladwindr.me/api/v1";

const char* GATEWAY_KEY =
    "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9";

const char* PAIRING_STATUS_PATH = "/pairing/status/";

// ---------------- GPIO PIN DEFINITIONS -----------------------

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t TRIG_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;

constexpr uint8_t BTN_ACTIVATE = 19;
constexpr uint8_t BTN_PAIRING = 23;

constexpr uint8_t LED_PAIRING = 25;    // LED 2: Pairing responses
constexpr uint8_t LED_ACTIVATE = 26;   // LED 1: Activation errors & monitoring
constexpr uint8_t LED_DATA = LED_ACTIVATE;

// ---------------- TIMING CONFIGURATION -----------------------

constexpr unsigned long DEBOUNCE_MS = 50;
constexpr unsigned long WIFI_TIMEOUT_MS = 10000;
constexpr unsigned long HTTP_TIMEOUT_MS = 10000;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 15UL * 60UL * 1000UL; // 15 minutes

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

// ---------------- STATE VARIABLES ----------------------------

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

bool isDevicePoweredOn = false;     // Set TRUE once Activation button is pressed
bool isPairedAndApproved = false;   // Backend confirmed approved pairing
bool isMonitoringActive = false;    // 15-minute telemetry monitoring is active

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
// FORWARD DECLARATIONS
// ============================================================

void printSection(const char* title);
void info(const String& message);
void success(const String& message);
void warning(const String& message);
void errorLog(const String& message);
void action(const String& message);

void updateLeds();
void flashActivationLed(uint8_t count);
void flashPairingLed(uint8_t count);

bool connectWiFi();
bool verifyBackendPairingStatus();
bool runActivationAudit();
void requestPairing();
bool sendTelemetry(float soilPercent, float distanceCm, bool distanceValid);
void runMonitoringCycle();

// ============================================================
// SERIAL LOGGING HELPERS
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

// ============================================================
// LED CONTROL & BLINK FEEDBACK
// LED 1 (GPIO 26): Activation feedback (1 = Success, 3 = Error)
// LED 2 (GPIO 25): Pairing feedback (1 = Success, 3 = Error)
// ============================================================

void updateLeds() {
    // LED 2: Solid ON if paired and approved
    digitalWrite(
        LED_PAIRING,
        isPairedAndApproved ? HIGH : LOW
    );

    // LED 1: Solid ON if monitoring is active
    digitalWrite(
        LED_ACTIVATE,
        isMonitoringActive ? HIGH : LOW
    );
}

void flashActivationLed(uint8_t count) {
    Serial.printf("[LED 1] Flashing %d time(s) on GPIO %d\n", count, LED_ACTIVATE);
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(LED_ACTIVATE, HIGH);
        delay(250);
        digitalWrite(LED_ACTIVATE, LOW);
        delay(250);
    }
    updateLeds();
}

void flashPairingLed(uint8_t count) {
    Serial.printf("[LED 2] Flashing %d time(s) on GPIO %d\n", count, LED_PAIRING);
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(LED_PAIRING, HIGH);
        delay(250);
        digitalWrite(LED_PAIRING, LOW);
        delay(250);
    }
    updateLeds();
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
            digitalWrite(LED_ACTIVATE, LOW);
            break;

        case PAIRING_UNVERIFIED:
        default:
            digitalWrite(
                LED_PAIRING,
                isPairedAndApproved ? HIGH : LOW
            );
            break;
    }

    Serial.printf(
        "[PAIRING] State updated: %s\n",
        pairingStateName(pairingState)
    );
}

// ============================================================
// WI-FI CONNECTIVITY
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
        Serial.print(F("."));
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        success(String("Wi-Fi connected. Local IP: ") + WiFi.localIP().toString());
        return true;
    }

    errorLog("Failed to establish Wi-Fi connection within timeout.");
    return false;
}

// ============================================================
// SECURE HTTP INITIALIZATION
// ============================================================

bool beginSecureHttp(
    HTTPClient& http,
    WiFiClientSecure& client,
    const String& url
) {
    if (!connectWiFi()) {
        errorLog("HTTPS request aborted: Wi-Fi disconnected.");
        return false;
    }

    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, url)) {
        errorLog(String("Could not initialize HTTPS request to ") + url);
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

    return true;
}

// ============================================================
// JSON STRING FIELD EXTRACTION
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
    return true;
}

// ============================================================
// SENSOR ACQUISITION HELPERS
// ============================================================

int readSoilADC() {
    long total = 0;
    int validSamples = 0;

    for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
        int val = analogRead(SOIL_PIN);
        total += val;
        validSamples++;
        delay(15);
    }

    if (validSamples == 0) {
        return -1;
    }

    return (int)(total / validSamples);
}

float soilPercentFromADC(int adcValue) {
    if (adcValue <= 0) {
        return 0.0f;
    }

    float percentage =
        (float)(SOIL_AIR_VALUE - adcValue) * 100.0f /
        (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE);

    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;

    return percentage;
}

bool measureUltrasonicSingle(float& distanceCm) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(4);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long duration = pulseIn(
        ECHO_PIN,
        HIGH,
        ECHO_TIMEOUT_US
    );

    if (duration == 0) {
        return false;
    }

    float calculated = (duration * SOUND_SPEED_CM_PER_US) / 2.0f;

    if (calculated < MIN_DISTANCE_CM || calculated > MAX_DISTANCE_CM) {
        return false;
    }

    distanceCm = calculated;
    return true;
}

bool readUltrasonicAverage(float& result) {
    float total = 0.0f;
    uint8_t valid = 0;

    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++) {
        float sample = 0.0f;
        if (measureUltrasonicSingle(sample)) {
            total += sample;
            valid++;
        }
        delay(25);
    }

    if (valid == 0) {
        errorLog("No valid ultrasonic echo received (timeout or out of range).");
        return false;
    }

    result = total / valid;
    Serial.printf("[ULTRASONIC] Filtered average: %.2f cm\n", result);
    return true;
}

// ============================================================
// BACKEND PAIRING STATUS VERIFICATION
// Queries GET /api/v1/pairing/status/{nodeId}
// Returns TRUE only if backend status is APPROVED.
// ============================================================

bool verifyBackendPairingStatus() {
    statusChecks++;

    printSection("BACKEND PAIRING STATUS CHECK");
    Serial.printf("[PAIRING CHECK] Querying status for Node: %s\n", nodeId.c_str());

    String url = String(BACKEND_BASE_URL) + PAIRING_STATUS_PATH + nodeId;
    Serial.printf("[PAIRING CHECK] GET %s\n", url.c_str());

    WiFiClientSecure client;
    HTTPClient http;

    if (!beginSecureHttp(http, client, url)) {
        applyPairingState(PAIRING_UNVERIFIED);
        errorLog("Cannot verify pairing status: secure HTTP init failed.");
        return false;
    }

    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();
    String response = httpCode > 0 ? http.getString() : "";
    String transportError = httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf("[PAIRING CHECK] HTTP response code: %d\n", httpCode);

    if (response.length() > 0) {
        Serial.printf("[PAIRING CHECK] Response body: %s\n", response.c_str());
    }

    if (httpCode < 0) {
        errorLog(String("Transport failure during status check: ") + transportError);
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (httpCode == 404) {
        errorLog("HTTP 404: Node is not found in registry. Pairing is EXPIRED or UNREGISTERED.");
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        errorLog(String("Pairing status check returned HTTP error ") + String(httpCode));
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    String responseNodeId;
    String status;

    if (
        !extractJsonString(response, "nodeId", responseNodeId) ||
        !extractJsonString(response, "pairingStatus", status)
    ) {
        errorLog("Response missing 'nodeId' or 'pairingStatus'.");
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    responseNodeId.trim();
    status.trim();
    status.toUpperCase();

    if (responseNodeId != nodeId) {
        errorLog("Mismatched node ID in response. Authorization denied.");
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (status == "APPROVED") {
        applyPairingState(PAIRING_APPROVED);
        success("Backend confirms this node is APPROVED.");
        return true;
    }

    if (status == "PENDING") {
        warning("Backend reports pairing is PENDING administrator approval.");
        applyPairingState(PAIRING_PENDING);
        return false;
    }

    if (status == "REJECTED" || status == "REVOKED") {
        warning("Backend reports pairing is REJECTED or REVOKED.");
        applyPairingState(PAIRING_REJECTED);
        return false;
    }

    errorLog(String("Unrecognized pairing status value: ") + status);
    applyPairingState(PAIRING_UNVERIFIED);
    return false;
}

// ============================================================
// HARDWARE AUDIT CHECKS
// Tests Wi-Fi, Soil Sensor ADC bounds, and Ultrasonic sensor.
// Returns TRUE only if all components pass.
// ============================================================

bool runActivationAudit() {
    auditCount++;

    printSection("HARDWARE AUDIT CHECK");

    Serial.printf("[AUDIT #%lu] Target Node: %s (MAC: %s)\n", auditCount, nodeId.c_str(), macAddress.c_str());

    // 1. Wi-Fi check
    info("1. Testing Wi-Fi connectivity...");
    bool wifiOk = connectWiFi();
    Serial.printf("[AUDIT] Wi-Fi link: %s\n", wifiOk ? "PASS (Connected)" : "FAIL (Disconnected)");

    // 2. Soil moisture sensor check
    info("2. Testing soil moisture sensor (GPIO 34)...");
    int rawSoil = readSoilADC();
    // Valid analog reading on ESP32 12-bit ADC is within realistic range
    bool soilOk = (rawSoil >= 50 && rawSoil <= 4090);
    float soilPercent = soilPercentFromADC(rawSoil);
    Serial.printf(
        "[AUDIT] Soil Sensor ADC: %d (%.2f%%) | Result: %s\n",
        rawSoil,
        soilPercent,
        soilOk ? "PASS" : "FAIL (Sensor reading out of expected bounds)"
    );

    // 3. Ultrasonic distance sensor check
    info("3. Testing ultrasonic distance sensor (TRIG: 5, ECHO: 18)...");
    float distanceCm = 0.0f;
    bool distanceOk = readUltrasonicAverage(distanceCm);
    Serial.printf(
        "[AUDIT] Ultrasonic Sensor: %.2f cm | Result: %s\n",
        distanceCm,
        distanceOk ? "PASS" : "FAIL (Echo timeout or out of range)"
    );

    bool allGood = wifiOk && soilOk && distanceOk;

    printSection("AUDIT SUMMARY");
    Serial.printf("[AUDIT SUMMARY] Wi-Fi:             %s\n", wifiOk ? "PASS" : "FAIL");
    Serial.printf("[AUDIT SUMMARY] Soil Sensor:       %s\n", soilOk ? "PASS" : "FAIL");
    Serial.printf("[AUDIT SUMMARY] Ultrasonic Sensor: %s\n", distanceOk ? "PASS" : "FAIL");
    Serial.printf("[AUDIT SUMMARY] OVERALL:           %s\n", allGood ? "PASS (All components operational)" : "FAIL (Hardware error)");

    return allGood;
}

// ============================================================
// PAIRING REQUEST (GPIO 23)
// Transmits pairing request to POST /api/v1/pairing/request
// LED 2 feedback: 1 blink for success, 3 blinks for error
// ============================================================

void requestPairing() {
    printSection("PAIRING REQUEST");

    if (!connectWiFi()) {
        pairingFailures++;
        errorLog("Pairing request aborted: Wi-Fi unavailable.");
        flashPairingLed(3); // 3 for error
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;

    String url = String(BACKEND_BASE_URL) + "/pairing/request";

    if (!beginSecureHttp(http, client, url)) {
        pairingFailures++;
        errorLog("Pairing request aborted: HTTPS initialization failed.");
        flashPairingLed(3); // 3 for error
        return;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    String payload = "{";
    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"macAddress\":\"" + macAddress + "\",";
    payload += "\"hardwareModel\":\"ESP32-WROOM-32\",";
    payload += "\"firmwareVersion\":\"2.0.0\"";
    payload += "}";

    Serial.printf("[PAIRING] POST %s\n", url.c_str());
    Serial.printf("[PAIRING] Payload: %s\n", payload.c_str());

    pairingAttempts++;

    int httpCode = http.POST(payload);
    String response = httpCode > 0 ? http.getString() : "";
    String transportError = httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf("[PAIRING] HTTP response code: %d\n", httpCode);

    if (response.length() > 0) {
        Serial.printf("[PAIRING] Response: %s\n", response.c_str());
    }

    if (httpCode < 200 || httpCode >= 300) {
        pairingFailures++;
        errorLog(String("Pairing request failed with HTTP ") + String(httpCode));
        if (httpCode < 0) {
            errorLog(String("Transport error: ") + transportError);
        }
        flashPairingLed(3); // 3 for error
        return;
    }

    String returnedNode;
    String status;

    if (
        !extractJsonString(response, "nodeId", returnedNode) ||
        !extractJsonString(response, "pairingStatus", status)
    ) {
        pairingFailures++;
        errorLog("Pairing response is missing required fields.");
        applyPairingState(PAIRING_UNVERIFIED);
        flashPairingLed(3); // 3 for error
        return;
    }

    status.toUpperCase();

    if (returnedNode != nodeId) {
        pairingFailures++;
        errorLog("Pairing response node ID mismatch.");
        applyPairingState(PAIRING_UNVERIFIED);
        flashPairingLed(3); // 3 for error
        return;
    }

    if (status == "APPROVED") {
        pairingSuccesses++;
        applyPairingState(PAIRING_APPROVED);
        success("Node pairing APPROVED by backend!");
        info("Press Activation Button (GPIO 19) to run hardware audit and activate monitoring.");
        flashPairingLed(1); // 1 for successful
        return;
    }

    if (status == "PENDING") {
        applyPairingState(PAIRING_PENDING);
        warning("Pairing request is PENDING approval in the ArvaCore dashboard.");
        flashPairingLed(3); // 3 for error / pending
        return;
    }

    if (status == "REJECTED" || status == "REVOKED") {
        pairingFailures++;
        applyPairingState(PAIRING_REJECTED);
        errorLog("Backend REJECTED the pairing request.");
        flashPairingLed(3); // 3 for error
        return;
    }

    pairingFailures++;
    applyPairingState(PAIRING_UNVERIFIED);
    errorLog(String("Unrecognized pairing status: ") + status);
    flashPairingLed(3); // 3 for error
}

// ============================================================
// TELEMETRY TRANSMISSION (15-MINUTE CYCLE)
// Transmits to POST /api/v1/telemetry/uplink/{nodeId}
// ============================================================

bool sendTelemetry(
    float soilPercent,
    float distanceCm,
    bool distanceValid
) {
    printSection("TELEMETRY TRANSMISSION");

    if (!isMonitoringActive) {
        warning("Telemetry blocked: monitoring is inactive.");
        return false;
    }

    WiFiClientSecure client;
    HTTPClient http;

    String url = String(BACKEND_BASE_URL) + "/telemetry/uplink/" + nodeId;

    if (!beginSecureHttp(http, client, url)) {
        telemetryFailures++;
        errorLog("Telemetry failed: could not initialize secure HTTP.");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    telemetrySequence++;
    telemetryAttempts++;

    String payload = "{";
    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"seq\":" + String(telemetrySequence) + ",";
    payload += "\"batteryVoltage\":4.15,";
    payload += "\"batteryLevel\":95.0,";
    payload += "\"soilMoisturePercent\":" + String(soilPercent, 2) + ",";
    payload += "\"measurements\":{";
    payload += "\"soil_moisture\":" + String(soilPercent, 2);

    if (distanceValid) {
        payload += ",\"water_level_distance\":" + String(distanceCm, 2);
        payload += ",\"waterLevelCm\":" + String(SENSOR_DATUM_OFFSET_CM - distanceCm, 2);
    }

    payload += "}}";

    Serial.printf("[TELEMETRY] Sequence #%lu -> POST %s\n", telemetrySequence, url.c_str());
    Serial.printf("[TELEMETRY] Payload: %s\n", payload.c_str());

    int httpCode = http.POST(payload);
    String response = httpCode > 0 ? http.getString() : "";
    String transportError = httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf("[TELEMETRY] HTTP status: %d\n", httpCode);

    if (httpCode >= 200 && httpCode < 300) {
        telemetrySuccesses++;
        success(String("Telemetry sequence #") + String(telemetrySequence) + " accepted by backend.");
        if (response.length() > 0) {
            Serial.printf("[TELEMETRY] Response: %s\n", response.c_str());
        }
        return true;
    }

    telemetryFailures++;
    errorLog(String("Telemetry transmission failed with HTTP ") + String(httpCode));
    if (httpCode < 0) {
        errorLog(String("Transport error: ") + transportError);
    }
    return false;
}

// ============================================================
// 15-MINUTE MONITORING CYCLE
// ============================================================

void runMonitoringCycle() {
    printSection("15-MINUTE MONITORING CYCLE EXECUTION");

    if (!isMonitoringActive) {
        info("Monitoring is OFF. Skipping cycle.");
        return;
    }

    Serial.printf("[CYCLE] Node ID: %s\n", nodeId.c_str());

    info("STEP 1/3: Verifying pairing status with backend...");
    if (!verifyBackendPairingStatus()) {
        errorLog("Monitoring cycle aborted: Pairing expired, revoked, or invalid.");
        isMonitoringActive = false;
        prefs.putBool("active", false);
        flashActivationLed(3); // 3 for error
        updateLeds();
        return;
    }

    info("STEP 2/3: Reading sensor measurements...");
    int soilADC = readSoilADC();
    float soilPercent = soilPercentFromADC(soilADC);

    float distanceCm = 0.0f;
    bool distanceValid = readUltrasonicAverage(distanceCm);

    info("STEP 3/3: Transmitting telemetry...");
    bool sent = sendTelemetry(soilPercent, distanceCm, distanceValid);

    if (!sent) {
        flashActivationLed(3); // 3 for error
    } else {
        // Brief pulse on LED 1 to indicate transmission activity
        digitalWrite(LED_ACTIVATE, LOW);
        delay(100);
        digitalWrite(LED_ACTIVATE, HIGH);
    }

    printSection("CYCLE COMPLETE");
    Serial.printf("[SUMMARY] Next 15-minute telemetry cycle in %lu ms\n", TELEMETRY_INTERVAL_MS);
}

// ============================================================
// ACTIVATION BUTTON HANDLER (GPIO 19)
// Powers on device, verifies pairing, runs audit, starts 15-min monitoring
// LED 1 feedback: 1 blink for success, 3 blinks for error
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

            // 1. If not yet powered on, power on and enable pairing button
            if (!isDevicePoweredOn) {
                isDevicePoweredOn = true;
                success("Device powered ON. Pairing button is now ENABLED.");
            }

            // 2. If monitoring is currently active, pressing activation toggles it OFF
            if (isMonitoringActive) {
                isMonitoringActive = false;
                prefs.putBool("active", false);
                updateLeds();
                success("Monitoring deactivated. Device remains powered ON in standby mode.");
                return;
            }

            // 3. Verify if pairing is still valid or not expired
            info("Checking pairing authorization with backend...");
            bool isPairingValid = verifyBackendPairingStatus();

            if (!isPairingValid) {
                errorLog("Activation ERROR: Pairing is expired, not found, or not approved.");
                flashActivationLed(3); // 3 blinks for error
                info("Pairing required. Press GPIO 23 (Pairing Button) to submit pairing request.");
                return;
            }

            // 4. Run component audit checks
            info("Pairing is valid and APPROVED. Running component audit checks...");
            bool allComponentsGood = runActivationAudit();

            if (!allComponentsGood) {
                errorLog("Activation ERROR: Component audit failed. One or more components are not good.");
                flashActivationLed(3); // 3 blinks for error
                isMonitoringActive = false;
                prefs.putBool("active", false);
                updateLeds();
                return;
            }

            // 5. All components pass audit and pairing is valid!
            success("Activation SUCCESS: All components operational and pairing is valid!");
            flashActivationLed(1); // 1 blink for success

            isMonitoringActive = true;
            prefs.putBool("active", true);
            lastTransmissionTime = millis();
            updateLeds(); // LED 1 solid ON

            info("15-minute monitoring is now ACTIVE. First telemetry cycle will execute in 15 minutes.");
        }
    }

    lastActivateReading = reading;
}

// ============================================================
// PAIRING BUTTON HANDLER (GPIO 23)
// Can only be pressed if device was first powered on by activation
// LED 2 feedback: 1 blink for success, 3 blinks for error
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
            printSection("PAIRING BUTTON PRESSED");

            if (!isDevicePoweredOn) {
                warning("Pairing button DISABLED. Activation button must first be pressed to power on device.");
                return;
            }

            requestPairing();
        }
    }

    lastPairingReading = reading;
}

// ============================================================
// STARTUP / SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(700);

    Serial.println();
    Serial.println(F("##################################################"));
    Serial.println(F("#       ARVACORE / ARVANODE SENSOR NODE          #"));
    Serial.println(F("#       AWD Precision Irrigation Firmware        #"));
    Serial.println(F("##################################################"));

    printSection("GPIO INITIALIZATION");

    pinMode(LED_PAIRING, OUTPUT);
    pinMode(LED_ACTIVATE, OUTPUT);

    digitalWrite(LED_PAIRING, LOW);
    digitalWrite(LED_ACTIVATE, LOW);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    pinMode(BTN_ACTIVATE, INPUT_PULLUP);
    pinMode(BTN_PAIRING, INPUT_PULLUP);

    analogReadResolution(12);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);

    success("GPIO initialization complete.");
    Serial.println(F("  - GPIO 19 : Activation button (Active LOW)"));
    Serial.println(F("  - GPIO 23 : Pairing button (Active LOW)"));
    Serial.println(F("  - GPIO 26 : LED 1 - Activation (1 blink=OK, 3=Error)"));
    Serial.println(F("  - GPIO 25 : LED 2 - Pairing (1 blink=OK, 3=Error)"));
    Serial.println(F("  - GPIO 34 : Soil moisture sensor AO"));
    Serial.println(F("  - GPIO 5  : Ultrasonic TRIG"));
    Serial.println(F("  - GPIO 18 : Ultrasonic ECHO"));

    printSection("PREFERENCES STORAGE");

    if (!prefs.begin("arvanode", false)) {
        errorLog("Failed to initialize Preferences storage.");
        while (true) {
            digitalWrite(LED_ACTIVATE, LOW);
            delay(1000);
        }
    }

    isPairedAndApproved = prefs.getBool("paired", false);
    pairingState = PAIRING_UNVERIFIED;

    // Device deliberately starts in STANDBY / UNPOWERED mode
    isDevicePoweredOn = false;
    isMonitoringActive = false;
    prefs.putBool("active", false);

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

    Serial.printf("[BOOT] Node ID:      %s\n", nodeId.c_str());
    Serial.printf("[BOOT] MAC address:  %s\n", macAddress.c_str());
    Serial.printf("[BOOT] Backend URL:  %s\n", BACKEND_BASE_URL);

    printSection("BUTTON INITIALIZATION");

    lastActivateReading = digitalRead(BTN_ACTIVATE);
    activateStableState = lastActivateReading;
    activateDebounceAt = millis();

    lastPairingReading = digitalRead(BTN_PAIRING);
    pairingStableState = lastPairingReading;
    pairingDebounceAt = millis();

    updateLeds();

    printSection("SYSTEM READY - STANDBY MODE");

    Serial.println(F("[STATUS] Device state: STANDBY (Unpowered)"));
    Serial.println(F("[STATUS] Pairing button: DISABLED until Activation is pressed"));
    Serial.println(F("[STATUS] Monitoring: OFF"));
    Serial.println(F("[STATUS] Telemetry interval: 15 minutes"));
    Serial.println();
    Serial.println(F("[READY] Step 1: Press GPIO 19 (Activation Button) to power on."));
    Serial.println(F("[READY] Step 2: If unpaired, press GPIO 23 (Pairing Button)."));
    Serial.println(F("[READY] Step 3: Once approved, press GPIO 19 to run audit and start 15-min monitoring."));
    Serial.println();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
    handleActivationButton();
    handlePairingButton();

    unsigned long now = millis();

    // Acquire and transmit telemetry every 15 minutes when monitoring is active
    if (
        isMonitoringActive &&
        now - lastTransmissionTime >= TELEMETRY_INTERVAL_MS
    ) {
        lastTransmissionTime = now;
        runMonitoringCycle();
    }

    delay(2);
}
