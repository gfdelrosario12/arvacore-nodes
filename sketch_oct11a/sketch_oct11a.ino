#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "esp_system.h"
#include "esp_mac.h"

// ============================================================
// ARVACORE / ARVANODE - AWD FIELD SENSOR NODE FIRMWARE
// Board: ESP32 Dev Module / ESP32-WROOM-32 (30-pin / 38-pin)
// Thesis: Alternate Wetting and Drying (AWD) Monitoring
//
// Sensor & Button Pin Assignments:
//   Capacitive Soil AO    -> GPIO 34 (Analog In, ADC1_CH6)
//   Soil VCC              -> 3.3V
//   Soil GND              -> GND
//
//   HC-SR04 / JSN-SR04T:
//     VCC                 -> VIN (5V external / USB)
//     GND                 -> GND
//     TRIG                -> GPIO 5  (Output, 3.3V)
//     ECHO                -> GPIO 18 (Input, via 1k/2k voltage divider to 3.3V)
//
//   Physical Momentary Push Buttons (Active-LOW with Internal Pull-Ups):
//     Activation Button   -> GPIO 19 (Toggles normal monitoring/telemetry ON/OFF)
//     Pairing Button      -> GPIO 23 (Initiates pairing request to backend)
//
// Serial Monitor: 115200 baud
// ============================================================

// -------------------- BACKEND CONFIGURATION -----------------
// Remote deployed API (used when available)
const char* REMOTE_BACKEND_URL = "https://api-aquaflow.gladwindr.me/api/v1";
// Local gateway / development fallback API (e.g. laptop or local server IP on field Wi-Fi)
const char* LOCAL_BACKEND_URL  = "http://192.168.1.88:8080/api/v1";

// Active backend URL (defaults to remote, falls back to local on network failure)
const char* BACKEND_BASE_URL = REMOTE_BACKEND_URL;

// Gateway authentication key configured on the backend
const char* GATEWAY_KEY = "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9";

// Wi-Fi Credentials (update to field gateway or access point)
const char* WIFI_SSID = "Cream";
const char* WIFI_PASS = "hatdog123";

// -------------------- PIN ASSIGNMENTS -----------------------
const uint8_t SOIL_PIN        = 34;
const uint8_t TRIG_PIN        = 5;
const uint8_t ECHO_PIN        = 18;
const uint8_t BTN_ACTIVATE    = 19;
const uint8_t BTN_PAIRING     = 23;

// -------------------- TIMING & DEBOUNCE ---------------------
const unsigned long DEBOUNCE_DELAY_MS        = 50UL;
const unsigned long ECHO_TIMEOUT_US          = 30000UL;
const unsigned long TRANSMISSION_INTERVAL_MS = 10000UL; // 10s monitoring cycle
const uint8_t SOIL_SAMPLES                   = 10;
const uint8_t ULTRASONIC_SAMPLES             = 5;

// Sensor specifications & physical limits (matching TelemetryValidator)
const float MIN_DISTANCE_CM            = 20.0f;
const float MAX_DISTANCE_CM            = 450.0f;
const float SPEED_OF_SOUND_CM_PER_US   = 0.0343f;

// Calibration for Capacitive Soil Moisture Sensor v1.2 (ADC 0 - 4095)
const int SOIL_AIR_VALUE   = 3200;
const int SOIL_WATER_VALUE = 1500;

// Still-well geometry: Sensor head is mounted 20.0 cm above the soil surface datum.
// Water Level (h) = 20.0 cm - measured distance (d).
const float SENSOR_DATUM_OFFSET_CM = 20.0f;

// -------------------- PERSISTENT STATE ----------------------
Preferences prefs;
String nodeId = "NODE-01";
String macAddress = "";
bool isPairedAndApproved = false;
bool isMonitoringActive = false;

// -------------------- BUTTON STATE TRACKERS -----------------
int lastActivateBtnReading = HIGH;
int activateBtnState = HIGH;
unsigned long lastActivateDebounceTime = 0;

int lastPairingBtnReading = HIGH;
int pairingBtnState = HIGH;
unsigned long lastPairingDebounceTime = 0;

// Runtime state trackers
unsigned long lastTransmissionTime = 0;
unsigned long telemetrySeq = 0;

// ------------------------------------------------------------
// Wi-Fi Connection
// ------------------------------------------------------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print(F("[WIFI] Connecting to SSID: "));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("[WIFI] Connected! IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println(F("[WIFI] Connection timed out. Will retry next cycle."));
  }
}

// ------------------------------------------------------------
// Sensor Readings & Validation
// ------------------------------------------------------------
int readRawSoilMoisture() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
    total += analogRead(SOIL_PIN);
    delay(10);
  }
  return total / SOIL_SAMPLES;
}

float calculateSoilMoisturePercent(int rawAdc) {
  float pct = ((float)(SOIL_AIR_VALUE - rawAdc) / (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE)) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

float readSingleUltrasonicCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
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
    if (d >= MIN_DISTANCE_CM && d <= MAX_DISTANCE_CM) {
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

// ------------------------------------------------------------
// Send Pairing Request to Backend
// ------------------------------------------------------------
void sendPairingRequest() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("[PAIRING] Pairing Button Pressed"));
  Serial.printf("[PAIRING] Initiating request for node %s (%s)...\n", nodeId.c_str(), macAddress.c_str());
  Serial.println(F("========================================"));

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[PAIRING] Cannot send pairing request: Wi-Fi offline"));
      return;
    }
  }

  WiFiClientSecure client;
  client.setInsecure(); // Field node HTTPS TLS

  HTTPClient http;
  String pairingUrl = String(BACKEND_BASE_URL) + "/pairing/request";

  if (!http.begin(client, pairingUrl)) {
    Serial.println(F("[PAIRING] HTTP client initialization failed"));
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

  String payload = "{";
  payload += "\"nodeId\":\"" + nodeId + "\",";
  payload += "\"macAddress\":\"" + macAddress + "\",";
  payload += "\"hardwareModel\":\"ESP32-WROOM-32\",";
  payload += "\"firmwareVersion\":\"2.0.0\"";
  payload += "}";

  Serial.printf("[PAIRING] POST %s\n", pairingUrl.c_str());
  Serial.printf("[PAIRING] Payload: %s\n", payload.c_str());

  int httpCode = http.POST(payload);
  if (httpCode == 201 || httpCode == 200) {
    String response = http.getString();
    Serial.printf("[PAIRING] SUCCESS (HTTP %d). Server Response: %s\n", httpCode, response.c_str());

    // Interpret response
    if (response.indexOf("\"pairingStatus\":\"APPROVED\"") > 0) {
      isPairedAndApproved = true;
      prefs.putBool("paired", true);
      Serial.println(F("[PAIRING] Node is confirmed APPROVED by backend!"));
    } else if (response.indexOf("\"pairingStatus\":\"PENDING\"") > 0) {
      Serial.println(F("[PAIRING] Node is PENDING administrator approval on ArvaNet frontend."));
    } else if (response.indexOf("\"pairingStatus\":\"REJECTED\"") > 0) {
      isPairedAndApproved = false;
      prefs.putBool("paired", false);
      Serial.println(F("[PAIRING] Pairing request was REJECTED by administrator."));
    }
  } else {
    Serial.printf("[PAIRING] Request failed with HTTP %d, error: %s\n", httpCode, http.errorToString(httpCode).c_str());
    String errResp = http.getString();
    if (errResp.length() > 0) {
      Serial.printf("[PAIRING] Response: %s\n", errResp.c_str());
    }

    if (httpCode == 401) {
      Serial.println(F("[PAIRING_ERROR] 401 Unauthorized: Gateway key missing or backend needs updated security rules."));
      Serial.println(F("[PAIRING_ERROR] Ensure ARVACORE_GATEWAY_INGEST_KEY matches GATEWAY_KEY on server."));
    } else if (httpCode == 400 && errResp.indexOf("MASTER_OFF") >= 0) {
      Serial.println(F("[PAIRING_ERROR] Master system is currently turned OFF. Turn ON Master System in ArvaNet dashboard first."));
    } else if (httpCode <= 0) {
      Serial.println(F("[PAIRING_ERROR] Connection timeout or server unreachable."));
      // Automatic backend failover between remote and local
      if (BACKEND_BASE_URL == REMOTE_BACKEND_URL) {
        Serial.printf("[FAILOVER] Switching active backend from Remote to Local: %s\n", LOCAL_BACKEND_URL);
        BACKEND_BASE_URL = LOCAL_BACKEND_URL;
      } else {
        Serial.printf("[FAILOVER] Switching active backend from Local to Remote: %s\n", REMOTE_BACKEND_URL);
        BACKEND_BASE_URL = REMOTE_BACKEND_URL;
      }
    }
  }
  http.end();
}

// ------------------------------------------------------------
// Transmit Telemetry Uplink to Backend
// ------------------------------------------------------------
void sendTelemetry(float distanceCm, bool distanceValid, float soilMoisturePercent) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String uplinkUrl = String(BACKEND_BASE_URL) + "/telemetry/uplink/" + nodeId;

  if (!http.begin(client, uplinkUrl)) {
    Serial.println(F("[HTTP] Failed to initialize connection to backend"));
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

  telemetrySeq++;

  float waterLevelCm = 0.0f;
  if (distanceValid) {
    waterLevelCm = SENSOR_DATUM_OFFSET_CM - distanceCm;
  }

  String json = "{";
  json += "\"nodeId\":\"" + nodeId + "\",";
  json += "\"seq\":" + String(telemetrySeq) + ",";
  json += "\"batteryVoltage\":4.15,";
  json += "\"batteryLevel\":95.0,";
  json += "\"soilMoisturePercent\":" + String(soilMoisturePercent, 2) + ",";

  if (distanceValid) {
    json += "\"water_level_distance\":" + String(distanceCm, 2) + ",";
    json += "\"waterLevelCm\":" + String(waterLevelCm, 2) + ",";
  }

  json += "\"measurements\":{";
  json += "\"soil_moisture\":" + String(soilMoisturePercent, 2);
  if (distanceValid) {
    json += ",\"water_level_distance\":" + String(distanceCm, 2);
  }
  json += "}";
  json += "}";

  Serial.println();
  Serial.printf("[HTTP] Uplinking telemetry to %s\n", uplinkUrl.c_str());
  Serial.printf("[HTTP] Payload: %s\n", json.c_str());

  int httpCode = http.POST(json);
  if (httpCode > 0) {
    Serial.printf("[HTTP] Uplink HTTP %d\n", httpCode);
    String response = http.getString();
    Serial.printf("[HTTP] Server Response: %s\n", response.c_str());

    // If backend returns MASTER_OFF, notify console
    if (response.indexOf("MASTER_OFF") >= 0) {
      Serial.println(F("[SYSTEM] Warning: Master system is turned OFF on server. Telemetry was suspended."));
    }
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
    if (httpCode <= 0) {
      if (BACKEND_BASE_URL == REMOTE_BACKEND_URL) {
        Serial.printf("[FAILOVER] Switching telemetry backend to Local: %s\n", LOCAL_BACKEND_URL);
        BACKEND_BASE_URL = LOCAL_BACKEND_URL;
      } else {
        Serial.printf("[FAILOVER] Switching telemetry backend to Remote: %s\n", REMOTE_BACKEND_URL);
        BACKEND_BASE_URL = REMOTE_BACKEND_URL;
      }
    }
  }
  http.end();
}

// ------------------------------------------------------------
// Non-Blocking Button Handling with Debouncing
// ------------------------------------------------------------
void handleButtons() {
  unsigned long now = millis();

  // 1. Activation Button (GPIO 19)
  int readingActivate = digitalRead(BTN_ACTIVATE);
  if (readingActivate != lastActivateBtnReading) {
    lastActivateDebounceTime = now;
  }
  if ((now - lastActivateDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (readingActivate != activateBtnState) {
      activateBtnState = readingActivate;
      if (activateBtnState == LOW) { // Button pressed
        if (!isPairedAndApproved) {
          Serial.println();
          Serial.println(F("[ACTIVATION] REJECTED: Node is not yet paired or approved by an administrator!"));
          Serial.println(F("[ACTIVATION] Please press the PAIRING button (GPIO 23) first and approve on ArvaNet."));
        } else {
          isMonitoringActive = !isMonitoringActive;
          prefs.putBool("active", isMonitoringActive);
          Serial.println();
          Serial.printf("[ACTIVATION] Normal monitoring toggled: %s\n", isMonitoringActive ? "ENABLED" : "DISABLED");
        }
      }
    }
  }
  lastActivateBtnReading = readingActivate;

  // 2. Pairing Button (GPIO 23)
  int readingPairing = digitalRead(BTN_PAIRING);
  if (readingPairing != lastPairingBtnReading) {
    lastPairingDebounceTime = now;
  }
  if ((now - lastPairingDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (readingPairing != pairingBtnState) {
      pairingBtnState = readingPairing;
      if (pairingBtnState == LOW) { // Button pressed
        sendPairingRequest();
      }
    }
  }
  lastPairingBtnReading = readingPairing;
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize GPIO Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Buttons configured with internal pull-ups (Active LOW)
  pinMode(BTN_ACTIVATE, INPUT_PULLUP);
  pinMode(BTN_PAIRING, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  // Persistent NVS Storage
  prefs.begin("arvanode", false);
  isPairedAndApproved = prefs.getBool("paired", false);
  isMonitoringActive  = prefs.getBool("active", false);

  // Connect Wi-Fi first to ensure radio is ready and MAC is active
  connectWiFi();

  // Re-read hardware MAC once Wi-Fi hardware is active
  uint8_t baseMac[6] = {0};
  esp_err_t ret = esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  if (ret == ESP_OK && (baseMac[0] != 0 || baseMac[1] != 0 || baseMac[2] != 0)) {
    char macBuf[18];
    snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
             baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    macAddress = String(macBuf);
  } else {
    String wifiMac = WiFi.macAddress();
    if (wifiMac.length() > 0 && wifiMac != "00:00:00:00:00:00") {
      macAddress = wifiMac;
    }
  }

  String macSuffix = macAddress;
  macSuffix.replace(":", "");
  if (macSuffix.length() >= 4 && macSuffix != "000000000000") {
    nodeId = "NODE-" + macSuffix.substring(macSuffix.length() - 4);
  } else {
    nodeId = "NODE-01";
  }

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F(" ARVACORE / ARVANODE - AWD SENSOR NODE"));
  Serial.println(F(" Dual Physical Buttons: Pairing & Activation"));
  Serial.println(F("========================================"));
  Serial.printf("Stable Node ID: %s\n", nodeId.c_str());
  Serial.printf("Hardware MAC:   %s\n", macAddress.c_str());
  Serial.printf("Paired Status:  %s\n", isPairedAndApproved ? "APPROVED" : "UNPAIRED / PENDING");
  Serial.printf("Telemetry Mode: %s\n", isMonitoringActive ? "ACTIVE" : "STANDBY");
  Serial.println(F("Pins:"));
  Serial.println(F("  Soil AO:           GPIO 34"));
  Serial.println(F("  Ultrasonic TRIG:   GPIO 5,  ECHO: GPIO 18"));
  Serial.println(F("  Activation Button: GPIO 19"));
  Serial.println(F("  Pairing Button:    GPIO 23"));
  Serial.println(F("========================================"));

  // If node is currently unpaired, automatically send an initial pairing request on boot
  if (!isPairedAndApproved && WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[BOOT] Node is unpaired. Automatically submitting pairing request to backend..."));
    sendPairingRequest();
  }
}

// ------------------------------------------------------------
// Main Loop
// ------------------------------------------------------------
void loop() {
  // Non-blocking button polling
  handleButtons();

  unsigned long currentTime = millis();

  // Telemetry acquisition & transmission when monitoring is ACTIVE
  if (isMonitoringActive && (currentTime - lastTransmissionTime >= TRANSMISSION_INTERVAL_MS)) {
    lastTransmissionTime = currentTime;

    // 1. Read Soil Moisture
    int rawSoilAdc = readRawSoilMoisture();
    float soilMoisturePercent = calculateSoilMoisturePercent(rawSoilAdc);

    // 2. Read Ultrasonic Distance & Calculate Water Level
    float distanceCm = 0.0f;
    bool distanceValid = measureUltrasonicDistance(distanceCm);

    Serial.println();
    Serial.println(F("--- Sensor Acquisition Cycle ---"));
    Serial.printf("Soil ADC: %d | Moisture: %.2f%%\n", rawSoilAdc, soilMoisturePercent);

    if (distanceValid) {
      float waterLevelCm = SENSOR_DATUM_OFFSET_CM - distanceCm;
      Serial.printf("Ultrasonic Distance: %.2f cm | Calculated Water Level: %.2f cm\n", distanceCm, waterLevelCm);
    } else {
      Serial.println(F("Ultrasonic Distance: Out of range (20-450cm) or timed out"));
    }

    // 3. Send Telemetry to Backend
    sendTelemetry(distanceCm, distanceValid, soilMoisturePercent);
  }
}