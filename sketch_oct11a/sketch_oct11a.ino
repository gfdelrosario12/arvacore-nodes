#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ============================================================
// ARVACORE / ARVANODE - AWD FIELD SENSOR NODE FIRMWARE
// Board: ESP32 Dev Module / ESP32-WROOM-32
// Thesis: Alternate Wetting and Drying (AWD) Monitoring
//
// Sensor Pin Assignments:
//   Soil AO      -> GPIO 34 (Capacitive Soil Moisture Analog In)
//   Soil VCC     -> 3V3
//   Soil GND     -> GND
//
//   HC-SR04/JSN  -> VIN / 5V
//   HC-SR04 GND  -> GND
//   HC-SR04 TRIG -> GPIO 5
//   HC-SR04 ECHO -> GPIO 18 (via voltage divider to 3.3V)
//
// Serial Monitor: 115200 baud
// ============================================================

// -------------------- NODE IDENTITY & BACKEND ----------------
// Node ID in the AquaFlow/ArvaCore ecosystem (e.g. NODE-01, NODE-Q1)
const char* NODE_ID = "NODE-01";

// Production backend deployed endpoint
const char* BACKEND_URL = "https://api-aquaflow.gladwindr.me/api/v1/telemetry/uplink/NODE-01";

// Gateway authentication key configured on the backend
const char* GATEWAY_KEY = "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9";

// Wi-Fi Credentials (update to your field/local Wi-Fi or gateway AP)
const char* WIFI_SSID = "ARVACORE_GATEWAY";
const char* WIFI_PASS = "arvacore2026";

// -------------------- PIN CONFIGURATION ---------------------
const uint8_t SOIL_PIN = 34;
const uint8_t TRIG_PIN = 5;
const uint8_t ECHO_PIN = 18;

// -------------------- TIMING & SAMPLES -----------------------
const unsigned long ECHO_TIMEOUT_US = 30000UL;
const unsigned long TRANSMISSION_INTERVAL_MS = 10000UL; // 10s default
const uint8_t SOIL_SAMPLES = 10;
const uint8_t ULTRASONIC_SAMPLES = 5;

// Sensor specifications & physical limits (matching TelemetryValidator)
const float MIN_DISTANCE_CM = 20.0f;
const float MAX_DISTANCE_CM = 450.0f;
const float SPEED_OF_SOUND_CM_PER_US = 0.0343f;

// Calibration for Capacitive Soil Moisture Sensor v1.2 (ADC 0 - 4095)
// Dry air reading (~0% moisture) and submerged reading (~100% moisture)
const int SOIL_AIR_VALUE = 3200;
const int SOIL_WATER_VALUE = 1500;

// Still-well geometry: Sensor head is mounted 20.0 cm above the soil surface datum.
// Water Level (h) = 20.0 cm - measured distance (d).
// When d = 20.0 cm -> water level = 0.0 cm (soil surface).
// When d = 35.0 cm -> water level = -15.0 cm (AWD reflood trigger).
// When d = 15.0 cm -> water level = +5.0 cm (ponded water layer).
const float SENSOR_DATUM_OFFSET_CM = 20.0f;

// State trackers
unsigned long lastTransmissionTime = 0;
unsigned long telemetrySeq = 0;

// ------------------------------------------------------------
// Read soil moisture ADC (averaged over SOIL_SAMPLES)
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
  // Constrain to known calibration bounds
  float pct = ((float)(SOIL_AIR_VALUE - rawAdc) / (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE)) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

// ------------------------------------------------------------
// Read one ultrasonic pulse distance (cm)
// ------------------------------------------------------------
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

// Averaged multi-sample ultrasonic distance
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
// Wi-Fi Connection
// ------------------------------------------------------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print(F("Connecting to Wi-Fi SSID: "));
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
    Serial.print(F("Wi-Fi Connected! IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println(F("Wi-Fi connection timed out. Will retry next cycle."));
  }
}

// ------------------------------------------------------------
// Transmit Telemetry Uplink to Backend
// ------------------------------------------------------------
void sendTelemetry(float distanceCm, bool distanceValid, float soilMoisturePercent) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  // Use WiFiClientSecure for HTTPS
  WiFiClientSecure client;
  client.setInsecure(); // Skip TLS cert validation for embedded field node

  HTTPClient http;
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println(F("[HTTP] Failed to initialize connection to backend"));
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

  telemetrySeq++;

  // Derive water level from still-well offset: waterLevel = 20.0 - distance
  float waterLevelCm = 0.0f;
  if (distanceValid) {
    waterLevelCm = SENSOR_DATUM_OFFSET_CM - distanceCm;
  }

  // Construct JSON payload with:
  // 1) Backend DecodedTelemetryPayload fields ('measurements' map)
  // 2) Top-level flat keys for backward compatibility and direct mapping
  String json = "{";
  json += "\"nodeId\":\"" + String(NODE_ID) + "\",";
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
  Serial.print(F("[HTTP] Uplinking to "));
  Serial.println(BACKEND_URL);
  Serial.print(F("[HTTP] Payload: "));
  Serial.println(json);

  int httpCode = http.POST(json);
  if (httpCode > 0) {
    Serial.printf("[HTTP] Uplink HTTP %d\n", httpCode);
    String response = http.getString();
    Serial.println("[HTTP] Server Response: " + response);
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F(" ARVACORE / ARVANODE - FIELD TELEMETRY"));
  Serial.println(F(" ESP32 Dev Module + Soil + HC-SR04/JSN"));
  Serial.println(F("========================================"));
  Serial.printf("Node ID: %s\n", NODE_ID);
  Serial.printf("Backend: %s\n", BACKEND_URL);
  Serial.println(F("Soil Pin: GPIO 34"));
  Serial.println(F("Ultrasonic TRIG: GPIO 5, ECHO: GPIO 18"));
  Serial.println(F("========================================"));

  connectWiFi();
}

// ------------------------------------------------------------
// Main Loop
// ------------------------------------------------------------
void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastTransmissionTime >= TRANSMISSION_INTERVAL_MS) {
    lastTransmissionTime = currentTime;

    // 1. Read Soil Moisture
    int rawSoilAdc = readRawSoilMoisture();
    float soilMoisturePercent = calculateSoilMoisturePercent(rawSoilAdc);

    // 2. Read Ultrasonic Distance & Calculate Water Level
    float distanceCm = 0.0f;
    bool distanceValid = measureUltrasonicDistance(distanceCm);

    Serial.println();
    Serial.println(F("--- Sensor Acquisition Cycle ---"));
    Serial.print(F("Soil ADC: "));
    Serial.print(rawSoilAdc);
    Serial.print(F(" | Moisture: "));
    Serial.print(soilMoisturePercent, 2);
    Serial.println(F("%"));

    if (distanceValid) {
      float waterLevelCm = SENSOR_DATUM_OFFSET_CM - distanceCm;
      Serial.print(F("Ultrasonic Distance: "));
      Serial.print(distanceCm, 2);
      Serial.print(F(" cm | Calculated Water Level: "));
      Serial.print(waterLevelCm, 2);
      Serial.println(F(" cm"));
    } else {
      Serial.println(F("Ultrasonic Distance: Out of range (20-450cm) or timed out"));
    }

    // 3. Send Telemetry to Backend
    sendTelemetry(distanceCm, distanceValid, soilMoisturePercent);
  }
}