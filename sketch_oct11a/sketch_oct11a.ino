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
// GPIO 34: Soil moisture analog output
// GPIO 5 : Ultrasonic TRIG
// GPIO 18: Ultrasonic ECHO (voltage divider required for 5V)
// GPIO 19: Activation button (active LOW)
// GPIO 23: Pairing button (active LOW)
// GPIO 26: Activation / monitoring LED
// GPIO 25: Pairing LED
//
// DIAGNOSTICS:
// - Checks actual Wi-Fi connectivity.
// - Checks soil ADC electrical response.
// - Warns about a potentially disconnected or saturated ADC.
// - Checks actual ultrasonic echo pulses.
// - Does not require a 20 cm minimum distance.
// - Reports individual failures in Serial Monitor.
// - Checks pairing authorization before telemetry.
// - Telemetry interval: 15 minutes.
//
// NOTE:
// A stable soil reading is not automatically a failure.
// Definitive sensor validation may require changing the input.
// ============================================================

// ---------------- BUILD-TIME / PROVISIONED CONFIGURATION -----
#if __has_include("node_config.h")
#include "node_config.h"
#endif

// ---------------- NETWORK CONFIGURATION ----------------------

#ifdef ARVA_CONFIG_INJECTED
#ifdef NODE_PRIMARY_WIFI_SSID
const char* PRIMARY_WIFI_SSID = NODE_PRIMARY_WIFI_SSID;
const char* PRIMARY_WIFI_PASSWORD = NODE_PRIMARY_WIFI_PASS;
const char* FALLBACK_WIFI_SSID = NODE_FALLBACK_WIFI_SSID;
const char* FALLBACK_WIFI_PASSWORD = NODE_FALLBACK_WIFI_PASS;
#else
const char* PRIMARY_WIFI_SSID = NODE_WIFI_SSID;
const char* PRIMARY_WIFI_PASSWORD = NODE_WIFI_PASS;
const char* FALLBACK_WIFI_SSID = "ArvaLink-1";
const char* FALLBACK_WIFI_PASSWORD = "ARVaLINK1_2026";
#endif
const char* WIFI_SSID = PRIMARY_WIFI_SSID;
const char* WIFI_PASSWORD = PRIMARY_WIFI_PASSWORD;
const char* BACKEND_BASE_URL = NODE_BACKEND_URL;
const char* GATEWAY_KEY = NODE_GATEWAY_KEY;

const char* PROVISIONED_NODE_ID = NODE_LOGICAL_ID;
const char* PROVISIONED_DISPLAY_NAME = NODE_DISPLAY_NAME;
const char* PROVISIONED_FIRMWARE_VER = NODE_FIRMWARE_VERSION;
const char* PROVISIONED_BUILD_ID = NODE_BUILD_ID;
constexpr float SENSOR_DATUM_OFFSET_CM = NODE_DATUM_OFFSET_CM;
#else
const char* PRIMARY_WIFI_SSID = "ArvaLink-2";
const char* PRIMARY_WIFI_PASSWORD = "ARVaLINK2_2026";
const char* FALLBACK_WIFI_SSID = "ArvaLink-1";
const char* FALLBACK_WIFI_PASSWORD = "ARVaLINK1_2026";

const char* WIFI_SSID = PRIMARY_WIFI_SSID;
const char* WIFI_PASSWORD = PRIMARY_WIFI_PASSWORD;

const char* BACKEND_BASE_URL =
    "https://api-aquaflow.gladwindr.me/api/v1";

const char* GATEWAY_KEY =
    "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9";

const char* PROVISIONED_NODE_ID = "";
const char* PROVISIONED_DISPLAY_NAME = "ArvaNode Default";
const char* PROVISIONED_FIRMWARE_VER = "1.2.0";
const char* PROVISIONED_BUILD_ID = "std-20261018";
constexpr float SENSOR_DATUM_OFFSET_CM = 20.0f;
#endif


// ---------------- GPIO DEFINITIONS ---------------------------

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t TRIG_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;

constexpr uint8_t BTN_ACTIVATE = 19;
constexpr uint8_t BTN_PAIRING = 23;

constexpr uint8_t LED_PAIRING = 25;
constexpr uint8_t LED_ACTIVATE = 26;

// ---------------- TIMING -------------------------------------

constexpr unsigned long DEBOUNCE_MS = 50;
constexpr unsigned long WIFI_TIMEOUT_MS = 10000;
constexpr unsigned long HTTP_TIMEOUT_MS = 10000;

constexpr unsigned long TELEMETRY_INTERVAL_MS =
    3UL * 1000UL;
constexpr unsigned long INITIAL_TELEMETRY_DELAY_MS =
    3UL * 1000UL;
constexpr unsigned long TELEMETRY_BLINK_INTERVAL_MS = 100;
constexpr unsigned long PAIRING_POLL_INTERVAL_MS = 5000;
constexpr unsigned long PAIRING_WAIT_TIMEOUT_MS = 60000;

constexpr uint8_t SOIL_SAMPLES = 10;
constexpr uint8_t ULTRASONIC_SAMPLES = 5;

constexpr unsigned long ECHO_TIMEOUT_US = 30000;

// Broad operational bounds, not target values.
// No 20 cm minimum distance requirement.
constexpr float MIN_DISTANCE_CM = 2.0f;
constexpr float MAX_DISTANCE_CM = 450.0f;

constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;

constexpr int SOIL_AIR_VALUE = 3200;
constexpr int SOIL_WATER_VALUE = 1500;

// ---------------- STATE --------------------------------------

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

bool isDevicePoweredOn = false;
bool isPairedAndApproved = false;
bool isMonitoringActive = false;
bool isInitialTelemetryPending = false;
unsigned long activationTimestamp = 0;

bool isSendingTelemetry = false;
unsigned long telemetryBlinkLastMs = 0;
bool telemetryBlinkState = false;

bool isWaitingForPairing = false;
unsigned long pairingWaitStartMs = 0;
unsigned long lastPairingPollMs = 0;
unsigned long pairingBlinkLastMs = 0;
bool pairingBlinkState = false;

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

// Button debounce state
int lastActivateReading = HIGH;
int activateStableState = HIGH;
unsigned long activateDebounceAt = 0;

int lastPairingReading = HIGH;
int pairingStableState = HIGH;
unsigned long pairingDebounceAt = 0;

// ============================================================
// LOGGING
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
        case PAIRING_PENDING:  return "PENDING";
        case PAIRING_APPROVED: return "APPROVED";
        case PAIRING_REJECTED: return "REJECTED";
        default:               return "UNVERIFIED";
    }
}

// ============================================================
// LED CONTROL
// ============================================================

void updateLeds() {
    unsigned long now = millis();

    // LED 1 (Activation): Blinks rapidly during active telemetry transmission;
    // otherwise stays solid HIGH if monitoring is active, or LOW if off.
    if (isSendingTelemetry) {
        if (now - telemetryBlinkLastMs >= TELEMETRY_BLINK_INTERVAL_MS) {
            telemetryBlinkLastMs = now;
            telemetryBlinkState = !telemetryBlinkState;
            digitalWrite(LED_ACTIVATE, telemetryBlinkState ? HIGH : LOW);
        }
    } else {
        digitalWrite(
            LED_ACTIVATE,
            isMonitoringActive ? HIGH : LOW
        );
    }

    // LED 2 (Pairing): Blinks while waiting for pairing approval (e.g. 500ms cycle);
    // stays solid HIGH when approved, or LOW when unverified/rejected.
    if (isWaitingForPairing) {
        if (now - pairingBlinkLastMs >= 500) {
            pairingBlinkLastMs = now;
            pairingBlinkState = !pairingBlinkState;
            digitalWrite(LED_PAIRING, pairingBlinkState ? HIGH : LOW);
        }
    } else {
        digitalWrite(
            LED_PAIRING,
            isPairedAndApproved ? HIGH : LOW
        );
    }
}

void flashActivationLed(uint8_t count) {
    Serial.printf(
        "[LED 1] Flashing %u time(s), GPIO %u\n",
        count, LED_ACTIVATE
    );

    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(LED_ACTIVATE, HIGH);
        delay(250);
        digitalWrite(LED_ACTIVATE, LOW);
        delay(250);
    }

    updateLeds();
}

void flashPairingLed(uint8_t count) {
    Serial.printf(
        "[LED 2] Flashing %u time(s), GPIO %u\n",
        count, LED_PAIRING
    );

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
            isWaitingForPairing = false;
            prefs.putBool("paired", true);
            break;

        case PAIRING_PENDING:
            isPairedAndApproved = false;
            prefs.putBool("paired", false);

            isMonitoringActive = false;
            isInitialTelemetryPending = false;
            prefs.putBool("active", false);
            break;

        case PAIRING_REJECTED:
            isPairedAndApproved = false;
            isWaitingForPairing = false;
            prefs.putBool("paired", false);

            isMonitoringActive = false;
            isInitialTelemetryPending = false;
            prefs.putBool("active", false);
            break;

        case PAIRING_UNVERIFIED:
        default:
            // Preserve the last known local approval, but do not
            // treat it as proof of current backend authorization.
            break;
    }

    updateLeds();

    Serial.printf(
        "[PAIRING] State: %s\n",
        pairingStateName(pairingState)
    );
}

// ============================================================
// WI-FI CONNECTIVITY (PRIMARY & FALLBACK PRIORITY)
//
// Priority Policy:
// 1. Always attempt Primary Wi-Fi (ArvaLink-2) first.
// 2. If Primary is unavailable or fails, attempt Fallback (ArvaLink-1).
// 3. Backoff retry tracking to avoid blocking loops.
// 4. Periodically re-check availability of Primary if connected to Fallback.
// 5. Passwords are NEVER printed or logged.
// ============================================================

const char* currentConnectedSsid = "";
unsigned long lastPrimaryRetryMs = 0;
constexpr unsigned long PRIMARY_RETRY_INTERVAL_MS = 60000; // Check for primary every 60s if on fallback
constexpr unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 7000;     // 7s per network attempt

bool attemptConnectNetwork(const char* ssid, const char* pass, const char* label) {
    if (!ssid || strlen(ssid) == 0) {
        return false;
    }

    action(String("Connecting to ") + label + " (" + ssid + ")...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_ATTEMPT_TIMEOUT_MS) {
        delay(200);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        currentConnectedSsid = ssid;
        success(String(label) + " connected: " + ssid + " (IP: " + WiFi.localIP().toString() + ")");
        Serial.printf("[WIFI] Active SSID: %s | RSSI: %d dBm | Channel: %d\n", ssid, WiFi.RSSI(), WiFi.channel());
        return true;
    }

    Serial.printf("[WIFI] %s (%s) connection attempt timed out or failed (status: %d).\n", label, ssid, (int)WiFi.status());
    WiFi.disconnect(true);
    delay(100);
    return false;
}

bool connectWiFi() {
    unsigned long now = millis();

    // If currently connected to Fallback, periodically test if Primary has recovered
    if (WiFi.status() == WL_CONNECTED) {
        if (strcmp(currentConnectedSsid, PRIMARY_WIFI_SSID) != 0) {
            if (now - lastPrimaryRetryMs >= PRIMARY_RETRY_INTERVAL_MS) {
                lastPrimaryRetryMs = now;
                info("Currently on Fallback network. Checking if Primary Wi-Fi (" + String(PRIMARY_WIFI_SSID) + ") has become available...");
                WiFi.disconnect(false);
                delay(100);
                if (attemptConnectNetwork(PRIMARY_WIFI_SSID, PRIMARY_WIFI_PASSWORD, "Primary Wi-Fi (P1)")) {
                    return true;
                }
                // If primary still unavailable, reconnect to fallback
                info("Primary still unavailable. Reconnecting to Fallback Wi-Fi...");
                return attemptConnectNetwork(FALLBACK_WIFI_SSID, FALLBACK_WIFI_PASSWORD, "Fallback Wi-Fi (P2)");
            }
        }
        return true;
    }

    // Step 1: Always attempt Primary Wi-Fi first
    if (attemptConnectNetwork(PRIMARY_WIFI_SSID, PRIMARY_WIFI_PASSWORD, "Primary Wi-Fi (P1)")) {
        return true;
    }

    // Step 2: Fallback to secondary Wi-Fi
    warning("Primary Wi-Fi unavailable. Attempting Fallback Wi-Fi (P2)...");
    if (attemptConnectNetwork(FALLBACK_WIFI_SSID, FALLBACK_WIFI_PASSWORD, "Fallback Wi-Fi (P2)")) {
        lastPrimaryRetryMs = now;
        return true;
    }

    errorLog("All Wi-Fi connection attempts failed (Primary: " + String(PRIMARY_WIFI_SSID) + ", Fallback: " + String(FALLBACK_WIFI_SSID) + ").");
    return false;
}


// ============================================================
// HTTPS INITIALIZATION
//
// setInsecure() disables certificate verification.
// Retained here as requested; use verified TLS certificates
// before deploying on an untrusted production network.
// ============================================================

bool beginSecureHttp(
    HTTPClient& http,
    WiFiClientSecure& client,
    const String& url
) {
    if (!connectWiFi()) {
        errorLog("HTTP aborted: Wi-Fi unavailable.");
        return false;
    }

    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, url)) {
        errorLog(String("HTTP initialization failed: ") + url);
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

    return true;
}

// ============================================================
// SIMPLE JSON FIELD EXTRACTION
// Suitable only for simple string fields without escaped quotes.
// Use ArduinoJson for general JSON parsing.
// ============================================================

bool extractJsonString(
    const String& body,
    const char* key,
    String& output
) {
    String searchKey = String("\"") + key + "\"";

    int keyPos = body.indexOf(searchKey);
    if (keyPos < 0) return false;

    int colonPos = body.indexOf(':', keyPos + searchKey.length());
    if (colonPos < 0) return false;

    int firstQuote = body.indexOf('"', colonPos + 1);
    if (firstQuote < 0) return false;

    int secondQuote = body.indexOf('"', firstQuote + 1);
    if (secondQuote < 0) return false;

    output = body.substring(firstQuote + 1, secondQuote);
    return true;
}

// ============================================================
// SOIL MOISTURE SENSOR DIAGNOSTICS
//
// Tests whether ADC samples are available and whether the input
// is electrically plausible.
//
// A single constant ADC value is not proof of failure.
// A disconnected analog input can float, and a real sensor can
// produce stable readings. This function reports suspicious
// conditions instead of claiming more than it can prove.
// ============================================================

int readSoilADC() {
    uint32_t total = 0;
    int minimum = 4095;
    int maximum = 0;
    uint8_t validSamples = 0;

    // Discard the first conversion after selecting the channel.
    analogRead(SOIL_PIN);
    delay(5);

    for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
        int value = analogRead(SOIL_PIN);

        if (value >= 0 && value <= 4095) {
            total += value;

            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;

            validSamples++;
        }

        delay(20);
    }

    if (validSamples == 0) {
        errorLog("SOIL: No usable ADC samples.");
        return -1;
    }

    int average = total / validSamples;

    Serial.printf(
        "[SOIL] Samples: %u/%u | Min: %d | Max: %d | "
        "Average ADC: %d | Spread: %d\n",
        validSamples,
        SOIL_SAMPLES,
        minimum,
        maximum,
        average,
        maximum - minimum
    );

    if (average <= 10) {
        errorLog(
            "SOIL: ADC is near zero. Check sensor power, GND, "
            "AO wiring, and GPIO 34."
        );
    } else if (average >= 4085) {
        errorLog(
            "SOIL: ADC is near full scale. Check wiring, "
            "sensor output voltage, and GPIO 34."
        );
    } else {
        success(
            "SOIL: ADC is producing a non-railed reading."
        );
    }

    if (minimum == maximum) {
        warning(
            "SOIL: All sampled values are identical. "
            "This may be normal, but verify the sensor by "
            "changing its input and observing ADC changes."
        );
    }

    return average;
}

float soilPercentFromADC(int adcValue) {
    if (adcValue < 0) return -1.0f;

    float percentage =
        (float)(SOIL_AIR_VALUE - adcValue) * 100.0f /
        (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE);

    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;

    return percentage;
}

bool checkSoilSensor(int& rawADC, float& percentage) {
    printSection("SOIL SENSOR FUNCTIONAL CHECK");

    rawADC = readSoilADC();

    if (rawADC < 0) {
        percentage = -1.0f;
        errorLog("SOIL RESULT: FAIL - No ADC samples.");
        return false;
    }

    percentage = soilPercentFromADC(rawADC);

    // Do not use moisture percentage as a hardware health test.
    // Only report obvious electrical rail conditions as failures.
    bool electricalResponse =
        rawADC > 10 && rawADC < 4085;

    Serial.printf(
        "[SOIL RESULT] ADC=%d, estimated moisture=%.2f%%, "
        "electrical check=%s\n",
        rawADC,
        percentage,
        electricalResponse ? "PASS" : "FAIL"
    );

    return electricalResponse;
}

// ============================================================
// ULTRASONIC SENSOR DIAGNOSTICS
//
// Checks for an actual echo pulse. Does not require a minimum
// distance of 20 cm.
//
// A valid echo confirms a response, not necessarily the
// correctness of the physical installation or target surface.
// ============================================================

bool measureUltrasonicSingle(
    float& distanceCm,
    unsigned long& durationUs
) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG_PIN, LOW);

    durationUs = pulseIn(
        ECHO_PIN,
        HIGH,
        ECHO_TIMEOUT_US
    );

    if (durationUs == 0) {
        return false;
    }

    distanceCm =
        (durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;

    // Broad sanity limits for pulse measurements.
    // In-range readings are not proof of correct targeting.
    if (
        !isfinite(distanceCm) ||
        distanceCm < MIN_DISTANCE_CM ||
        distanceCm > MAX_DISTANCE_CM
    ) {
        return false;
    }

    return true;
}

bool readUltrasonicAverage(float& result) {
    float total = 0.0f;
    uint8_t valid = 0;
    uint8_t timeouts = 0;

    float minimum = MAX_DISTANCE_CM;
    float maximum = MIN_DISTANCE_CM;

    printSection("ULTRASONIC FUNCTIONAL CHECK");

    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++) {
        float distance = 0.0f;
        unsigned long duration = 0;

        bool ok = measureUltrasonicSingle(
            distance,
            duration
        );

        if (ok) {
            total += distance;
            valid++;

            if (distance < minimum) minimum = distance;
            if (distance > maximum) maximum = distance;

            Serial.printf(
                "[ULTRASONIC] Sample %u: echo=%lu us, "
                "distance=%.2f cm, PASS\n",
                i + 1,
                duration,
                distance
            );
        } else {
            timeouts++;

            Serial.printf(
                "[ULTRASONIC] Sample %u: FAIL - "
                "no valid echo or out-of-range pulse\n",
                i + 1
            );
        }

        delay(60);
    }

    Serial.printf(
        "[ULTRASONIC] Valid samples: %u/%u, "
        "timeouts/invalid: %u\n",
        valid,
        ULTRASONIC_SAMPLES,
        timeouts
    );

    if (valid == 0) {
        errorLog(
            "ULTRASONIC RESULT: FAIL. Check sensor power, "
            "GND, TRIG GPIO 5, ECHO GPIO 18, voltage divider, "
            "and target placement."
        );
        return false;
    }

    result = total / valid;

    Serial.printf(
        "[ULTRASONIC] Average=%.2f cm, spread=%.2f cm\n",
        result,
        maximum - minimum
    );

    if (valid < ULTRASONIC_SAMPLES) {
        warning(
            "ULTRASONIC: Intermittent echoes detected. "
            "Inspect wiring, power stability, and target surface."
        );
    } else {
        success(
            "ULTRASONIC: Echo responses received for every sample."
        );
    }

    return true;
}

// ============================================================
// BACKEND PAIRING STATUS
//
// GET /api/v1/pairing/status/{nodeId}
//
// The response must include nodeId and pairingStatus.
// Confirm these field names match the backend implementation.
// ============================================================

bool verifyBackendPairingStatus() {
    statusChecks++;

    printSection("BACKEND PAIRING STATUS CHECK");

    String url =
        String(BACKEND_BASE_URL) +
        "/pairing/status/" + nodeId;

    Serial.printf("[PAIRING CHECK] GET %s\n", url.c_str());

    WiFiClientSecure client;
    HTTPClient http;

    if (!beginSecureHttp(http, client, url)) {
        errorLog("Cannot initialize backend status request.");
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();

    String response =
        httpCode > 0 ? http.getString() : "";

    String transportError =
        httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf(
        "[PAIRING CHECK] HTTP status: %d\n",
        httpCode
    );

    if (response.length() > 0) {
        Serial.printf(
            "[PAIRING CHECK] Body: %s\n",
            response.c_str()
        );
    }

    if (httpCode < 0) {
        errorLog(
            String("Transport failure: ") + transportError
        );
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (httpCode == 404) {
        errorLog(
            "Node not found (HTTP 404). Verify registration "
            "and the status endpoint."
        );
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        errorLog(
            String("Status endpoint returned HTTP ") +
            String(httpCode)
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
            "Missing nodeId or pairingStatus in JSON response."
        );
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    responseNodeId.trim();
    status.trim();
    status.toUpperCase();

    if (responseNodeId != nodeId) {
        errorLog("Node ID mismatch. Authorization denied.");
        applyPairingState(PAIRING_UNVERIFIED);
        return false;
    }

    if (status == "APPROVED") {
        applyPairingState(PAIRING_APPROVED);
        success("Backend confirms pairing APPROVED.");
        return true;
    }

    if (status == "PENDING") {
        warning("Pairing is pending administrator approval.");
        applyPairingState(PAIRING_PENDING);
        return false;
    }

    if (status == "REJECTED" || status == "REVOKED" || status == "EXPIRED") {
        warning("Pairing was rejected, revoked, or expired.");
        applyPairingState(PAIRING_REJECTED);
        return false;
    }

    errorLog(
        String("Unknown pairing status: ") + status
    );

    applyPairingState(PAIRING_UNVERIFIED);
    return false;
}

// ============================================================
// COMPREHENSIVE ACTIVATION AUDIT
// ============================================================

bool runActivationAudit() {
    auditCount++;

    printSection("HARDWARE AND CONNECTIVITY AUDIT");

    Serial.printf(
        "[AUDIT #%lu] Node=%s, MAC=%s\n",
        auditCount,
        nodeId.c_str(),
        macAddress.c_str()
    );

    info("Checking Wi-Fi connection.");
    bool wifiOk = connectWiFi();

    Serial.printf(
        "[AUDIT] Wi-Fi: %s\n",
        wifiOk ? "PASS" : "FAIL"
    );

    int soilADC = -1;
    float soilPercent = -1.0f;

    bool soilOk = checkSoilSensor(
        soilADC,
        soilPercent
    );

    float distanceCm = 0.0f;
    bool ultrasonicOk = readUltrasonicAverage(
        distanceCm
    );

    printSection("AUDIT SUMMARY");

    Serial.printf(
        "[AUDIT] Wi-Fi:             %s\n",
        wifiOk ? "PASS" : "FAIL"
    );

    Serial.printf(
        "[AUDIT] Soil ADC:          %s\n",
        soilOk ? "PASS" : "FAIL"
    );

    Serial.printf(
        "[AUDIT] Ultrasonic echo:   %s\n",
        ultrasonicOk ? "PASS" : "FAIL"
    );

    bool allGood = wifiOk && soilOk && ultrasonicOk;

    Serial.printf(
        "[AUDIT] OVERALL:           %s\n",
        allGood ? "PASS" : "FAIL"
    );

    if (!soilOk) {
        warning(
            "Soil sensor did not pass the electrical check."
        );
    }

    if (!ultrasonicOk) {
        warning(
            "Ultrasonic sensor did not return a valid echo."
        );
    }

    return allGood;
}

// ============================================================
// PAIRING REQUEST
//
// POST /api/v1/pairing/request
// ============================================================

void requestPairing() {
    printSection("PAIRING REQUEST");

    if (!connectWiFi()) {
        pairingFailures++;
        errorLog("Pairing request aborted: Wi-Fi unavailable.");
        flashPairingLed(3);
        return;
    }

    WiFiClientSecure client;
    HTTPClient http;

    String url =
        String(BACKEND_BASE_URL) + "/pairing/request";

    if (!beginSecureHttp(http, client, url)) {
        pairingFailures++;
        errorLog("Pairing HTTP initialization failed.");
        flashPairingLed(3);
        return;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    String payload = "{";
    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"macAddress\":\"" + macAddress + "\",";
    payload += "\"hardwareModel\":\"ESP32-WROOM-32\",";
    payload += "\"firmwareVersion\":\"" + String(PROVISIONED_FIRMWARE_VER) + "\"";
    payload += "}";

    Serial.printf("[PAIRING] POST %s\n", url.c_str());
    Serial.printf("[PAIRING] Payload: %s\n", payload.c_str());

    pairingAttempts++;

    int httpCode = http.POST(payload);

    String response =
        httpCode > 0 ? http.getString() : "";

    String transportError =
        httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf(
        "[PAIRING] HTTP status: %d\n",
        httpCode
    );

    if (response.length() > 0) {
        Serial.printf(
            "[PAIRING] Response: %s\n",
            response.c_str()
        );
    }

    if (httpCode < 200 || httpCode >= 300) {
        pairingFailures++;

        errorLog(
            String("Pairing failed with HTTP ") +
            String(httpCode)
        );

        if (httpCode < 0) {
            errorLog(
                String("Transport error: ") + transportError
            );
        }

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
            "Pairing response missing nodeId or pairingStatus."
        );
        flashPairingLed(3);
        return;
    }

    returnedNode.trim();
    status.trim();
    status.toUpperCase();

    if (returnedNode != nodeId) {
        pairingFailures++;
        errorLog("Pairing response node ID mismatch.");
        flashPairingLed(3);
        return;
    }

    if (status == "APPROVED") {
        pairingSuccesses++;
        applyPairingState(PAIRING_APPROVED);

        success("Backend approved this node.");
        flashPairingLed(1);

        info(
            "Press Activation to run the audit and start monitoring."
        );
        return;
    }

    if (status == "PENDING") {
        applyPairingState(PAIRING_PENDING);

        isWaitingForPairing = true;
        pairingWaitStartMs = millis();
        lastPairingPollMs = millis();

        warning(
            "Pairing is pending administrator approval. Waiting and checking automatically..."
        );
        return;
    }

    if (status == "REJECTED" || status == "REVOKED" || status == "EXPIRED") {
        pairingFailures++;
        applyPairingState(PAIRING_REJECTED);

        errorLog("Backend rejected, revoked, or expired pairing.");
        flashPairingLed(3);
        return;
    }

    pairingFailures++;
    applyPairingState(PAIRING_UNVERIFIED);

    errorLog(
        String("Unknown pairing response status: ") + status
    );

    flashPairingLed(3);
}

// ============================================================
// TELEMETRY TRANSMISSION
//
// POST /api/v1/telemetry/uplink/{nodeId}
//
// Battery values remain placeholders because this firmware
// does not measure battery voltage using a voltage divider.
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

    if (soilPercent < 0.0f) {
        errorLog(
            "Telemetry blocked: no usable soil measurement."
        );
        telemetryFailures++;
        return false;
    }

    isSendingTelemetry = true;
    updateLeds();

    WiFiClientSecure client;
    HTTPClient http;

    String url =
        String(BACKEND_BASE_URL) +
        "/telemetry/uplink/" + nodeId;

    if (!beginSecureHttp(http, client, url)) {
        isSendingTelemetry = false;
        updateLeds();
        telemetryFailures++;
        errorLog("Telemetry HTTP initialization failed.");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    telemetrySequence++;
    telemetryAttempts++;

    String payload = "{";
    payload += "\"nodeId\":\"" + nodeId + "\",";
    payload += "\"seq\":" + String(telemetrySequence) + ",";

    // PLACEHOLDERS: replace with actual battery measurements
    // before using these values for battery monitoring.
    payload += "\"batteryVoltage\":4.15,";
    payload += "\"batteryLevel\":95.0,";

    payload +=
        "\"soilMoisturePercent\":" +
        String(soilPercent, 2) + ",";

    payload += "\"measurements\":{";
    payload +=
        "\"soil_moisture\":" +
        String(soilPercent, 2);

    if (distanceValid) {
        payload +=
            ",\"water_level_distance\":" +
            String(distanceCm, 2);

        float calculatedWaterLevel = SENSOR_DATUM_OFFSET_CM - distanceCm;
        if (calculatedWaterLevel >= -30.0f && calculatedWaterLevel <= 30.0f) {
            payload +=
                ",\"waterLevelCm\":" +
                String(calculatedWaterLevel, 2);
        }
    }

    payload += "}}";

    Serial.printf(
        "[TELEMETRY] Sequence #%lu\n",
        telemetrySequence
    );

    Serial.printf(
        "[TELEMETRY] POST %s\n",
        url.c_str()
    );

    Serial.printf(
        "[TELEMETRY] Payload: %s\n",
        payload.c_str()
    );

    int httpCode = http.POST(payload);

    String response =
        httpCode > 0 ? http.getString() : "";

    String transportError =
        httpCode < 0 ? http.errorToString(httpCode) : "";

    http.end();

    Serial.printf(
        "[TELEMETRY] HTTP status: %d\n",
        httpCode
    );

    if (response.length() > 0) {
        Serial.printf(
            "[TELEMETRY] Response: %s\n",
            response.c_str()
        );
    }

    isSendingTelemetry = false;
    updateLeds();

    if (httpCode >= 200 && httpCode < 300) {
        telemetrySuccesses++;

        success(
            String("Telemetry accepted. Sequence #") +
            String(telemetrySequence)
        );

        return true;
    }

    telemetryFailures++;

    errorLog(
        String("Telemetry rejected or failed. HTTP ") +
        String(httpCode)
    );

    if (httpCode == 401 || httpCode == 403 ||
        (httpCode == 400 && (response.indexOf("NODE_NOT_ACTIVE") >= 0 || response.indexOf("REVOKED") >= 0 || response.indexOf("UNREGISTERED") >= 0))) {
        warning("Node has been revoked or unpaired by backend. Clearing pairing state.");
        applyPairingState(PAIRING_REJECTED);
        flashPairingLed(3);
    }

    if (httpCode < 0) {
        errorLog(
            String("Transport error: ") + transportError
        );
    }

    return false;
}

// ============================================================
// MONITORING CYCLE
// ============================================================

void runMonitoringCycle() {
    printSection("MONITORING CYCLE");

    if (!isMonitoringActive) {
        info("Monitoring inactive.");
        return;
    }

    Serial.printf(
        "[CYCLE] Node ID: %s\n",
        nodeId.c_str()
    );

    info("Checking current backend authorization.");

    if (!verifyBackendPairingStatus()) {
        errorLog(
            "Monitoring stopped: current pairing authorization "
            "could not be confirmed."
        );

        isMonitoringActive = false;
        isInitialTelemetryPending = false;
        prefs.putBool("active", false);

        flashActivationLed(3);
        updateLeds();
        return;
    }

    info("Checking soil sensor response.");

    int soilADC = -1;
    float soilPercent = -1.0f;

    bool soilOk = checkSoilSensor(
        soilADC,
        soilPercent
    );

    info("Checking ultrasonic echo response.");

    float distanceCm = 0.0f;

    bool distanceOk = readUltrasonicAverage(
        distanceCm
    );

    printSection("SENSOR HEALTH SUMMARY");

    Serial.printf(
        "[HEALTH] Soil sensor: %s\n",
        soilOk ? "PASS" : "FAIL"
    );

    Serial.printf(
        "[HEALTH] Ultrasonic: %s\n",
        distanceOk ? "PASS" : "FAIL"
    );

    if (!soilOk) {
        errorLog(
            "Soil sensor failure detected; telemetry will "
            "not report a fabricated soil measurement."
        );
    }

    if (!distanceOk) {
        warning(
            "No valid ultrasonic distance. Telemetry will "
            "omit ultrasonic measurements for this cycle."
        );
    }

    if (soilOk) {
        info("Sending valid soil measurement to backend.");

        bool sent = sendTelemetry(
            soilPercent,
            distanceCm,
            distanceOk
        );

        if (!sent) {
            warning("Telemetry transmission failed.");
        }
    } else {
        telemetryFailures++;
        errorLog(
            "Telemetry skipped because the soil reading "
            "failed the electrical check."
        );
    }

    printSection("CYCLE COMPLETE");

    Serial.printf(
        "[SUMMARY] Telemetry successes: %lu\n",
        telemetrySuccesses
    );

    Serial.printf(
        "[SUMMARY] Telemetry failures: %lu\n",
        telemetryFailures
    );

    Serial.printf(
        "[SUMMARY] Next cycle in %lu ms\n",
        TELEMETRY_INTERVAL_MS
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

            if (!isDevicePoweredOn) {
                isDevicePoweredOn = true;

                success(
                    "Device logically activated. "
                    "Pairing button is now enabled."
                );
            }

            if (isMonitoringActive) {
                isMonitoringActive = false;
                isInitialTelemetryPending = false;
                prefs.putBool("active", false);

                updateLeds();

                success(
                    "Monitoring stopped. Device remains powered."
                );
                return;
            }

            info("Checking backend pairing authorization.");

            if (!verifyBackendPairingStatus()) {
                errorLog(
                    "Activation denied: pairing is not confirmed "
                    "as approved."
                );

                flashActivationLed(3);

                info(
                    "If necessary, press GPIO 23 to request pairing."
                );
                return;
            }

            info("Running hardware functional checks.");

            bool hardwareOk = runActivationAudit();

            if (!hardwareOk) {
                errorLog(
                    "Activation denied: one or more hardware "
                    "checks failed."
                );

                isMonitoringActive = false;
                isInitialTelemetryPending = false;
                prefs.putBool("active", false);

                flashActivationLed(3);
                updateLeds();
                return;
            }

            success(
                "Activation passed: pairing and hardware checks OK."
            );

            flashActivationLed(1);

            isMonitoringActive = true;
            isInitialTelemetryPending = true;
            activationTimestamp = millis();
            prefs.putBool("active", true);

            updateLeds();

            info(
                "Monitoring active. First telemetry cycle "
                "will run in 3 seconds (repeats every 1 second)."
            );
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
            printSection("PAIRING BUTTON PRESSED");

            if (!isDevicePoweredOn) {
                warning(
                    "Pairing disabled. Press Activation first."
                );
                return;
            }

            if (isPairedAndApproved) {
                info("Device is already paired and approved.");
                flashPairingLed(1);
                return;
            }

            if (isWaitingForPairing) {
                info("Already waiting for pairing approval. Checking status now...");
                lastPairingPollMs = now;
                verifyBackendPairingStatus();
                return;
            }

            requestPairing();
        }
    }

    lastPairingReading = reading;
}

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(700);

    Serial.println();
    Serial.println(
        F("==============================================")
    );
    Serial.println(
        F("       ARVACORE / ARVANODE SENSOR NODE")
    );
    Serial.println(
        F("       Hardware Functional Diagnostics")
    );
    Serial.println(
        F("==============================================")
    );

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

    success("GPIO initialized.");

    Serial.println(F("GPIO 34: Soil sensor AO"));
    Serial.println(F("GPIO 5 : Ultrasonic TRIG"));
    Serial.println(F("GPIO 18: Ultrasonic ECHO"));
    Serial.println(F("GPIO 19: Activation button"));
    Serial.println(F("GPIO 23: Pairing button"));
    Serial.println(F("GPIO 25: Pairing LED"));
    Serial.println(F("GPIO 26: Activation LED"));

    printSection("PREFERENCES STORAGE");

    if (!prefs.begin("arvanode", false)) {
        errorLog("Could not initialize Preferences.");

        while (true) {
            digitalWrite(LED_ACTIVATE, LOW);
            delay(1000);
        }
    }

    // Start in standby regardless of previously stored state.
    isDevicePoweredOn = false;
    isMonitoringActive = false;

    pairingState = PAIRING_UNVERIFIED;
    isPairedAndApproved = prefs.getBool("paired", false);

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

    if (strlen(PROVISIONED_NODE_ID) > 0) {
        nodeId = String(PROVISIONED_NODE_ID);
    } else {
        nodeId = compactMac.length() >= 4
            ? "NODE-" +
              compactMac.substring(compactMac.length() - 4)
            : "NODE-01";
    }

    Serial.printf(
        "[BOOT] Node ID: %s\n",
        nodeId.c_str()
    );

    Serial.printf(
        "[BOOT] Display Name: %s\n",
        PROVISIONED_DISPLAY_NAME
    );

    Serial.printf(
        "[BOOT] Firmware Ver: %s\n",
        PROVISIONED_FIRMWARE_VER
    );

    Serial.printf(
        "[BOOT] Build ID: %s\n",
        PROVISIONED_BUILD_ID
    );

    Serial.printf(
        "[BOOT] MAC: %s\n",
        macAddress.c_str()
    );

    Serial.printf(
        "[BOOT] Backend: %s\n",
        BACKEND_BASE_URL
    );

    printSection("BUTTON INITIALIZATION");

    lastActivateReading = digitalRead(BTN_ACTIVATE);
    activateStableState = lastActivateReading;
    activateDebounceAt = millis();

    lastPairingReading = digitalRead(BTN_PAIRING);
    pairingStableState = lastPairingReading;
    pairingDebounceAt = millis();

    updateLeds();

    printSection("SYSTEM READY");

    Serial.println(F("[STATUS] Mode: STANDBY"));
    Serial.println(F("[STATUS] Monitoring: OFF"));
    Serial.println(F("[STATUS] Pairing button: disabled until activation"));
    Serial.println(F("[STATUS] Telemetry interval: 1 second (initial: 3 seconds)"));

    Serial.println();
    Serial.println(F("1. Open Serial Monitor at 115200 baud."));
    Serial.println(F("2. Press GPIO 19 to activate."));
    Serial.println(F("3. Check sensor diagnostic results."));
    Serial.println(F("4. If unpaired, press GPIO 23."));
    Serial.println(F("5. After approval, press GPIO 19 again."));
    Serial.println();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
    handleActivationButton();
    handlePairingButton();

    unsigned long now = millis();

    // Check pairing authorization status periodically while waiting for admin approval
    if (isWaitingForPairing) {
        if (now - pairingWaitStartMs >= PAIRING_WAIT_TIMEOUT_MS) {
            isWaitingForPairing = false;
            warning("Pairing wait timeout reached (60s). Press GPIO 23 to retry.");
            flashPairingLed(3);
        } else if (now - lastPairingPollMs >= PAIRING_POLL_INTERVAL_MS) {
            lastPairingPollMs = now;
            info("Checking pairing authorization status...");
            if (verifyBackendPairingStatus()) {
                info("Pairing approved! Press GPIO 19 to activate monitoring.");
                flashPairingLed(1);
            }
        }
    }

    // Telemetry cycle timing
    if (isMonitoringActive) {
        if (isInitialTelemetryPending) {
            if (now - activationTimestamp >= INITIAL_TELEMETRY_DELAY_MS) {
                isInitialTelemetryPending = false;
                lastTransmissionTime = now;
                info("Initial activation timer elapsed (3s). Triggering telemetry.");
                runMonitoringCycle();
            }
        } else if (now - lastTransmissionTime >= TELEMETRY_INTERVAL_MS) {
            lastTransmissionTime = now;
            runMonitoringCycle();
        }
    }

    // Continuously service LED state changes and non-blocking blinking
    updateLeds();

    delay(2);
}