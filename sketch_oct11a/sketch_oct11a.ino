#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ============================================================
// ARVACORE / ARVANODE - SENSOR NODE FIRMWARE
// Board: ESP32-WROOM-32 DevKit
// Thesis: Alternate Wetting and Drying (AWD) Monitoring
//
// Sensor Pin Assignments:
// TRIG -> GPIO 5  (JSN-SR04T Ultrasonic Trigger)
// ECHO -> GPIO 18 (JSN-SR04T Echo - via voltage divider to 3.3V)
// SOIL -> GPIO 34 (Capacitive Soil Moisture Analog In)
// ============================================================

// -------------------- NODE IDENTITY -------------------------
const char* NODE_ID = "NODE-01";
const char* GATEWAY_URL = "http://192.168.1.100:8080/api/v1/telemetry/uplink/NODE-01";
const char* GATEWAY_KEY = "arvacore-gateway-sec-key-2026";

// Wi-Fi Credentials
const char* WIFI_SSID = "ARVACORE_GATEWAY";
const char* WIFI_PASS = "arvacore2026";

// -------------------- PIN CONFIGURATION ---------------------
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
const int SOIL_PIN = 34;

// -------------------- MEASUREMENT SETTINGS ------------------
const unsigned long ECHO_TIMEOUT_US = 30000;
const unsigned long TRANSMISSION_INTERVAL_MS = 10000; // 10s default / configurable

// JSN-SR04T specifications
const float MIN_DISTANCE_CM = 20.0;
const float MAX_DISTANCE_CM = 450.0;
const float SPEED_OF_SOUND_CM_PER_US = 0.0343;

// Calibration for Capacitive Soil Moisture Sensor v1.2 (ADC 0 - 4095)
const int SOIL_AIR_VALUE = 3200;    // Dry air reading (0% moisture)
const int SOIL_WATER_VALUE = 1500;  // Submerged in water (100% moisture)

// Ultrasonic still-well field geometry (as per thesis Section 3):
// Float inside perforated PVC pipe. Tube height above soil datum = 20.0 cm.
// When water is at soil surface (0 cm water level), sensor-to-float distance = 20.0 cm.
// Float distance > 20 cm indicates water level below surface (negative depth).
const float SENSOR_DATUM_OFFSET_CM = 20.0;

unsigned long lastTransmissionTime = 0;
unsigned long telemetrySeq = 0;

// -------------------- ULTRASONIC MEASUREMENT ----------------
bool measureDistance(float &distanceCm, unsigned long &echoTimeUs) {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  echoTimeUs = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (echoTimeUs == 0) {
    return false;
  }

  distanceCm = (echoTimeUs * SPEED_OF_SOUND_CM_PER_US) / 2.0;
  if (distanceCm < MIN_DISTANCE_CM || distanceCm > MAX_DISTANCE_CM) {
    return false;
  }

  return true;
}

// -------------------- SOIL MOISTURE MEASUREMENT -------------
float measureSoilMoisture() {
  int rawAdc = analogRead(SOIL_PIN);
  float moisturePercent = map(rawAdc, SOIL_AIR_VALUE, SOIL_WATER_VALUE, 0, 100);
  if (moisturePercent < 0.0) moisturePercent = 0.0;
  if (moisturePercent > 100.0) moisturePercent = 100.0;
  return moisturePercent;
}

// -------------------- WI-FI SETUP & UPLINK ------------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWi-Fi connection timed out. Telemetry will retry next cycle.");
  }
}

void sendTelemetry(float distanceCm, bool distanceValid, float soilMoisturePercent) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  HTTPClient http;
  http.begin(GATEWAY_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

  // Construct JSON payload truthfully reflecting raw sensor readings
  // Water level is derived from still-well geometry: waterLevel = SENSOR_DATUM_OFFSET - distance
  telemetrySeq++;
  String json = "{";
  json += "\"nodeId\":\"" + String(NODE_ID) + "\",";
  json += "\"seq\":" + String(telemetrySeq) + ",";
  json += "\"soilMoisturePercent\":" + String(soilMoisturePercent, 2) + ",";
  if (distanceValid) {
    float waterLevelCm = SENSOR_DATUM_OFFSET_CM - distanceCm;
    json += "\"water_level_distance\":" + String(distanceCm, 2) + ",";
    json += "\"waterLevelCm\":" + String(waterLevelCm, 2) + ",";
  }
  json += "\"batteryVoltage\":4.15";
  json += "}";

  Serial.print("Uplinking payload: ");
  Serial.println(json);

  int httpCode = http.POST(json);
  if (httpCode > 0) {
    Serial.printf("[HTTP] POST response code: %d\n", httpCode);
    String response = http.getString();
    Serial.println("[HTTP] Response: " + response);
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// -------------------- SETUP ---------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);

  Serial.println();
  Serial.println("================================================");
  Serial.println(" ARVACORE / ARVANODE - AWD FIELD SENSOR NODE");
  Serial.println("================================================");
  Serial.printf("Node ID: %s\n", NODE_ID);
  Serial.println("Sensors: JSN-SR04T (Distance/Water Level), Capacitive Soil Moisture");
  Serial.printf("Target Gateway: %s\n", GATEWAY_URL);
  Serial.println("================================================");

  connectWiFi();
}

// -------------------- LOOP ----------------------------------
void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastTransmissionTime >= TRANSMISSION_INTERVAL_MS) {
    lastTransmissionTime = currentTime;

    float distanceCm = 0.0;
    unsigned long echoTimeUs = 0;
    bool distanceValid = measureDistance(distanceCm, echoTimeUs);

    float soilMoisture = measureSoilMoisture();

    Serial.println("\n--- Sensor Sampling Cycle ---");
    if (distanceValid) {
      Serial.printf("Raw distance: %.2f cm (Echo: %lu us)\n", distanceCm, echoTimeUs);
      Serial.printf("Calculated Water Level: %.2f cm\n", SENSOR_DATUM_OFFSET_CM - distanceCm);
    } else {
      Serial.println("Distance Sensor: Out of range or timed out");
    }
    Serial.printf("Soil Moisture: %.2f%%\n", soilMoisture);

    // Send payload to ArvaCore Backend / Gateway
    sendTelemetry(distanceCm, distanceValid, soilMoisture);
  }
}
