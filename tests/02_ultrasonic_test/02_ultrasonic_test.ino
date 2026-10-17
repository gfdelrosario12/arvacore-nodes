#include <Arduino.h>

// =============================================================================
// TEST 02: HC-SR04 ULTRASONIC SENSOR INDEPENDENT TEST
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Purpose: Trigger HC-SR04, measure Echo pulse duration, calculate median
//          distance from 5 samples, detect timeouts and out-of-range echoes.
// =============================================================================

constexpr uint8_t PIN_TRIG = 5;
constexpr uint8_t PIN_ECHO = 18;

constexpr uint8_t SAMPLES_PER_CYCLE = 5;
constexpr unsigned long ECHO_TIMEOUT_US = 25000; // ~430 cm theoretical limit
constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;
constexpr float MIN_RANGE_CM = 2.0f;
constexpr float MAX_RANGE_CM = 300.0f;
constexpr unsigned long CYCLE_INTERVAL_MS = 3000;

unsigned long lastCycleTime = 0;
uint32_t cycleNumber = 0;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

bool readSinglePing(float &distanceCm, unsigned long &durationUs) {
    // Ensure clean LOW pulse before triggering
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(4);

    // Send 10 microsecond HIGH pulse to initiate ultrasonic burst
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    // Measure duration of HIGH pulse on ECHO pin
    durationUs = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);

    if (durationUs == 0) {
        distanceCm = NAN;
        return false; // Timeout
    }

    // Distance = (time * speed_of_sound) / 2 (accounting for round-trip)
    distanceCm = ((float)durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;

    if (distanceCm < MIN_RANGE_CM || distanceCm > MAX_RANGE_CM) {
        return false; // Out of operational range
    }

    return true; // Valid reading
}

void sortDistances(float* arr, uint8_t count) {
    for (uint8_t i = 1; i < count; i++) {
        float key = arr[i];
        int j = (int)i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    digitalWrite(PIN_TRIG, LOW);

    printHeader("TEST 02: HC-SR04 ULTRASONIC SENSOR INDEPENDENT TEST");
    Serial.printf("TRIG PIN          : GPIO %u (OUTPUT, 3.3V logic)\n", PIN_TRIG);
    Serial.printf("ECHO PIN          : GPIO %u (INPUT, 3.3V logic via voltage divider)\n", PIN_ECHO);
    Serial.printf("TEST INTERVAL     : %lu ms\n", CYCLE_INTERVAL_MS);
    Serial.printf("SAMPLES PER CYCLE : %u\n", SAMPLES_PER_CYCLE);
    Serial.printf("RANGE BOUNDS      : %.1f cm to %.1f cm\n", MIN_RANGE_CM, MAX_RANGE_CM);
    Serial.println(F("CRITICAL NOTE     : Sensor ECHO pin outputs 5V. Voltage divider (1k/2k) IS MANDATORY."));
    Serial.println(F("                    Distance accuracy requires comparison against a physical ruler."));

    lastCycleTime = millis() - CYCLE_INTERVAL_MS; // Trigger immediately
}

void loop() {
    unsigned long now = millis();
    if (now - lastCycleTime >= CYCLE_INTERVAL_MS) {
        lastCycleTime = now;
        cycleNumber++;

        printHeader("ULTRASONIC MEASUREMENT CYCLE");
        Serial.printf("CYCLE NUMBER      : #%u\n", cycleNumber);
        Serial.printf("TIMESTAMP         : %lu ms\n", now);

        float validDistances[SAMPLES_PER_CYCLE];
        uint8_t validCount = 0;
        uint8_t timeoutCount = 0;
        uint8_t outOfRangeCount = 0;

        for (uint8_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
            float dist = 0.0f;
            unsigned long dur = 0;
            bool ok = readSinglePing(dist, dur);

            if (dur == 0) {
                timeoutCount++;
                Serial.printf("  [SAMPLE %u] ECHO TIMEOUT (No echo received within %lu us)\n", i + 1, ECHO_TIMEOUT_US);
            } else if (!ok) {
                outOfRangeCount++;
                Serial.printf("  [SAMPLE %u] OUT OF RANGE: %.2f cm (Echo duration: %lu us)\n", i + 1, dist, dur);
            } else {
                validDistances[validCount] = dist;
                validCount++;
                Serial.printf("  [SAMPLE %u] VALID: %.2f cm (Echo duration: %lu us)\n", i + 1, dist, dur);
            }

            // Settle reflections before the next pulse
            delay(60);
        }

        Serial.println();
        Serial.printf("VALID READINGS    : %u/%u\n", validCount, SAMPLES_PER_CYCLE);
        Serial.printf("ECHO TIMEOUTS     : %u\n", timeoutCount);
        Serial.printf("OUT-OF-RANGE      : %u\n", outOfRangeCount);

        if (validCount == 0) {
            Serial.println(F("ULTRASONIC STATUS : FAULT — No valid echo received."));
            Serial.println(F("ULTRASONIC VALUE  : UNAVAILABLE"));
            Serial.println(F("DIAGNOSTIC HINTS  :"));
            Serial.println(F("  1. Verify HC-SR04 VCC is connected to 5V (NOT 3.3V; standard HC-SR04 fails on 3.3V)."));
            Serial.println(F("  2. Check GND continuity between ESP32 and HC-SR04."));
            Serial.println(F("  3. Verify voltage divider: ECHO -> 1k -> GPIO 18 -> 2k -> GND."));
            Serial.println(F("  4. Check that no object is closer than 2 cm or further than 300 cm."));
            Serial.println(F("  5. Verify sensor is aimed perpendicularly at a flat, hard, acoustic-reflective surface."));
        } else {
            // Calculate median distance
            sortDistances(validDistances, validCount);
            float medianDistance = 0.0f;
            if (validCount % 2 == 1) {
                medianDistance = validDistances[validCount / 2];
            } else {
                medianDistance = (validDistances[validCount / 2 - 1] + validDistances[validCount / 2]) / 2.0f;
            }

            Serial.println(F("ULTRASONIC STATUS : WORKING — Echo response detected."));
            Serial.printf("ULTRASONIC VALUE  : %.2f cm\n", medianDistance);
            Serial.printf("RANGE MIN/MAX     : Min = %.2f cm | Max = %.2f cm\n", 
                validDistances[0], validDistances[validCount - 1]);
            Serial.println(F("DISTANCE ACCURACY : NOT YET VERIFIED — compare with a known physical distance."));
        }
    }

    delay(10);
}
