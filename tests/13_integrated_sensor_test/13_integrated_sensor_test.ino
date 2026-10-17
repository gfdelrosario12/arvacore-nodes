#include <Arduino.h>

// =============================================================================
// TEST 13: INTEGRATED SENSOR SUBSYSTEM TEST (ULTRASONIC + SOIL MOISTURE)
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Purpose: Concurrently acquire, validate, and report readings from both
//          primary transducers (HC-SR04 and Soil Moisture Sensor) every 3 seconds.
// FAULT ISOLATION: A failure in one transducer does NOT suppress, halt, or distort
//                  readings from the other transducer.
// SAFEGUARD: No relays or pump actuators are energized in this test.
// =============================================================================

// Pin Definitions
constexpr uint8_t PIN_SOIL_AO       = 34; // Analog Input (ADC1_CH6)
constexpr uint8_t PIN_ULTRASONIC_TRIG = 5;  // Digital Output
constexpr uint8_t PIN_ULTRASONIC_ECHO = 18; // Digital Input (Via Voltage Divider)

// Timing Configuration
constexpr unsigned long REPORT_INTERVAL_MS = 3000;
constexpr uint8_t ULTRASONIC_SAMPLES       = 5;
constexpr unsigned long ECHO_TIMEOUT_US    = 25000;
constexpr float SOUND_SPEED_CM_PER_US      = 0.0343f;
constexpr float MIN_RANGE_CM               = 2.0f;
constexpr float MAX_RANGE_CM               = 300.0f;

constexpr uint8_t SOIL_SAMPLES             = 10;
constexpr int SOIL_AIR_VALUE               = 3200;
constexpr int SOIL_WATER_VALUE             = 1500;

unsigned long lastReportTime = 0;
uint32_t cycleCount = 0;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

// ---------------- ULTRASONIC ACQUISITION ---------------------
bool readSingleUltrasonicPing(float &distanceCm, unsigned long &durationUs) {
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    delayMicroseconds(4);
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    durationUs = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, ECHO_TIMEOUT_US);
    if (durationUs == 0) {
        distanceCm = NAN;
        return false;
    }

    distanceCm = ((float)durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;
    if (distanceCm < MIN_RANGE_CM || distanceCm > MAX_RANGE_CM) {
        return false;
    }
    return true;
}

bool acquireUltrasonicMedian(float &medianDistance, uint8_t &validCount) {
    float validReadings[ULTRASONIC_SAMPLES];
    validCount = 0;

    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++) {
        float dist = 0.0f;
        unsigned long dur = 0;
        if (readSingleUltrasonicPing(dist, dur)) {
            validReadings[validCount] = dist;
            validCount++;
        }
        delay(50);
    }

    if (validCount == 0) {
        medianDistance = NAN;
        return false;
    }

    // Sort to compute median
    for (uint8_t i = 1; i < validCount; i++) {
        float key = validReadings[i];
        int j = (int)i - 1;
        while (j >= 0 && validReadings[j] > key) {
            validReadings[j + 1] = validReadings[j];
            j--;
        }
        validReadings[j + 1] = key;
    }

    if (validCount % 2 == 1) {
        medianDistance = validReadings[validCount / 2];
    } else {
        medianDistance = (validReadings[validCount / 2 - 1] + validReadings[validCount / 2]) / 2.0f;
    }

    return true;
}

// ---------------- SOIL ACQUISITION ---------------------------
bool acquireSoilMoisture(float &rawAvg, float &mvAvg, float &moisturePercent, int &spread) {
    analogRead(PIN_SOIL_AO);
    delay(5);

    uint32_t rawSum = 0;
    uint32_t mvSum = 0;
    int rawMin = 4096;
    int rawMax = -1;

    for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
        int r = analogRead(PIN_SOIL_AO);
        uint32_t mv = analogReadMilliVolts(PIN_SOIL_AO);

        rawSum += r;
        mvSum += mv;
        if (r < rawMin) rawMin = r;
        if (r > rawMax) rawMax = r;
        delay(10);
    }

    rawAvg = (float)rawSum / SOIL_SAMPLES;
    mvAvg = (float)mvSum / SOIL_SAMPLES;
    spread = rawMax - rawMin;

    if (rawAvg >= 4050 || rawAvg <= 50) {
        moisturePercent = NAN;
        return false; // Electrical saturation or disconnect
    }

    float pct = (float)(SOIL_AIR_VALUE - rawAvg) * 100.0f / (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE);
    moisturePercent = constrain(pct, 0.0f, 100.0f);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Pin setups
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    pinMode(PIN_SOIL_AO, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_SOIL_AO, ADC_11db);

    printHeader("TEST 13: INTEGRATED SENSOR SUBSYSTEM TEST");
    Serial.println(F("CONFIGURATION:"));
    Serial.printf("  - Ultrasonic: TRIG = GPIO %u, ECHO = GPIO %u (Divider required)\n", PIN_ULTRASONIC_TRIG, PIN_ULTRASONIC_ECHO);
    Serial.printf("  - Soil AO   : GPIO %u (12-bit ADC1_CH6)\n", PIN_SOIL_AO);
    Serial.printf("  - Cycle Rate: Every %lu ms\n", REPORT_INTERVAL_MS);
    Serial.println(F("FAULT RESILIENCE: Independent transducer pipelines; neither blocks the other."));

    lastReportTime = millis() - REPORT_INTERVAL_MS;
}

void loop() {
    unsigned long now = millis();

    if (now - lastReportTime >= REPORT_INTERVAL_MS) {
        lastReportTime = now;
        cycleCount++;

        printHeader("INTEGRATED TRANSDUCER CYCLE");
        Serial.printf("CYCLE NUMBER      : #%u | TIMESTAMP: %lu ms\n", cycleCount, now);

        // 1. Acquire Soil Sensor
        float soilRaw = 0.0f, soilMv = 0.0f, soilPct = 0.0f;
        int soilSpread = 0;
        bool soilOk = acquireSoilMoisture(soilRaw, soilMv, soilPct, soilSpread);

        // 2. Acquire Ultrasonic Sensor
        float distanceCm = 0.0f;
        uint8_t validPings = 0;
        bool ultrasonicOk = acquireUltrasonicMedian(distanceCm, validPings);

        // 3. Independent Reporting - Soil Moisture
        Serial.println(F("--- [TRANSDUCER 1: SOIL MOISTURE SENSOR (GPIO 34)] ---"));
        if (soilOk) {
            Serial.println(F("SOIL SENSOR STATUS: WORKING — Analog signal verified."));
            Serial.printf("SOIL ADC VALUE    : %d/4095 (Spread: %d counts)\n", (int)soilRaw, soilSpread);
            Serial.printf("SOIL ADC VOLTAGE  : %.1f mV\n", soilMv);
            Serial.printf("SOIL MOISTURE     : %.2f%%\n", soilPct);
        } else {
            Serial.println(F("SOIL SENSOR STATUS: ELECTRICAL FAULT / SATURATION"));
            Serial.printf("SOIL ADC VALUE    : %d/4095\n", (int)soilRaw);
            Serial.printf("SOIL ADC VOLTAGE  : %.1f mV\n", soilMv);
            Serial.println(F("SOIL MOISTURE     : UNAVAILABLE (Check connection, power, or open circuit)"));
        }

        // 4. Independent Reporting - Ultrasonic Sensor
        Serial.println(F("--- [TRANSDUCER 2: HC-SR04 ULTRASONIC (GPIO 5 / 18)] ---"));
        if (ultrasonicOk) {
            Serial.println(F("ULTRASONIC STATUS : WORKING — Valid echo responses detected."));
            Serial.printf("ULTRASONIC VALUE  : %.2f cm (Median of %u/%u valid samples)\n", distanceCm, validPings, ULTRASONIC_SAMPLES);
        } else {
            Serial.println(F("ULTRASONIC STATUS : FAULT — No echo responses detected (0/5 pings)."));
            Serial.println(F("ULTRASONIC VALUE  : UNAVAILABLE (Check 5V VCC, GND, and 1k/2k divider)"));
        }

        // 5. Overall Subsystem Summary
        Serial.println(F("--------------------------------------------------------------------------------"));
        Serial.printf("SUBSYSTEM HEALTH  : Soil = [%s] | Ultrasonic = [%s]\n",
            soilOk ? "PASS" : "FAIL",
            ultrasonicOk ? "PASS" : "FAIL");
    }

    delay(10);
}

