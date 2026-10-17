
#include <Arduino.h>
#include <math.h>

// ============================================
// ESP32-WROOM-32: HC-SR04 ULTRASONIC TEST
// ============================================

constexpr uint8_t TRIG_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;

constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;
constexpr float MIN_DISTANCE_CM = 2.0f;
constexpr float MAX_DISTANCE_CM = 300.0f;

constexpr unsigned long ECHO_TIMEOUT_US = 25000UL;
constexpr unsigned long TEST_INTERVAL_MS = 3000UL;
constexpr uint8_t SAMPLE_COUNT = 5;

unsigned long lastTestTime = 0;
uint32_t testNumber = 0;

bool readDistance(float &distanceCm, unsigned long &durationUs) {
    // Send a clean trigger pulse.
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG_PIN, LOW);

    // Measure the Echo pulse duration.
    durationUs = pulseIn(
        ECHO_PIN,
        HIGH,
        ECHO_TIMEOUT_US
    );

    // No Echo pulse received.
    if (durationUs == 0) {
        distanceCm = NAN;
        return false;
    }

    // Distance = time x speed of sound / 2.
    distanceCm =
        (durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;

    // Reject measurements outside the configured range.
    if (!isfinite(distanceCm) ||
        distanceCm < MIN_DISTANCE_CM ||
        distanceCm > MAX_DISTANCE_CM) {
        return false;
    }

    return true;
}

void runUltrasonicTest() {
    testNumber++;

    Serial.println();
    Serial.println("============================================");
    Serial.printf("ULTRASONIC TEST CYCLE: %lu\n",
                  (unsigned long)testNumber);
    Serial.println("============================================");

    float distances[SAMPLE_COUNT];
    uint8_t validReadings = 0;
    uint8_t timeoutCount = 0;
    uint8_t outOfRangeCount = 0;

    for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
        float distanceCm = NAN;
        unsigned long durationUs = 0;

        bool valid = readDistance(distanceCm, durationUs);

        if (durationUs == 0) {
            Serial.printf(
                "SAMPLE %u: NO ECHO - timeout\n",
                i + 1
            );
            timeoutCount++;
        } else if (!valid) {
            Serial.printf(
                "SAMPLE %u: OUT OF RANGE - %.2f cm\n",
                i + 1,
                distanceCm
            );
            outOfRangeCount++;
        } else {
            Serial.printf(
                "SAMPLE %u: %.2f cm | Echo: %lu us\n",
                i + 1,
                distanceCm,
                durationUs
            );

            distances[validReadings] = distanceCm;
            validReadings++;
        }

        delay(100);
    }

    Serial.println();
    Serial.println("------------- FINAL RESULTS ---------------");

    Serial.printf(
        "VALID READINGS: %u/%u\n",
        validReadings,
        SAMPLE_COUNT
    );

    Serial.printf("ECHO TIMEOUTS: %u\n", timeoutCount);
    Serial.printf("OUT-OF-RANGE READINGS: %u\n", outOfRangeCount);

    if (validReadings == 0) {
        Serial.println("ULTRASONIC STATUS: NOT WORKING");
        Serial.println("ULTRASONIC VALUE: No valid distance detected.");
        Serial.println("CHECK: VCC, GND, TRIG, ECHO, and voltage divider.");
        Serial.println("CHECK: Place a flat target within 2-300 cm.");
        return;
    }

    // Sort valid measurements to calculate the median.
    for (uint8_t i = 1; i < validReadings; i++) {
        float current = distances[i];
        int j = (int)i - 1;

        while (j >= 0 && distances[j] > current) {
            distances[j + 1] = distances[j];
            j--;
        }

        distances[j + 1] = current;
    }

    float medianDistance;

    if (validReadings % 2 == 1) {
        medianDistance = distances[validReadings / 2];
    } else {
        medianDistance =
            (distances[validReadings / 2 - 1] +
             distances[validReadings / 2]) / 2.0f;
    }

    Serial.println("ULTRASONIC STATUS: WORKING - Echo detected.");
    Serial.printf(
        "ULTRASONIC VALUE: %.2f cm\n",
        medianDistance
    );

    Serial.printf(
        "MINIMUM DISTANCE: %.2f cm\n",
        distances[0]
    );

    Serial.printf(
        "MAXIMUM DISTANCE: %.2f cm\n",
        distances[validReadings - 1]
    );

    if (validReadings == SAMPLE_COUNT) {
        Serial.println("READING STATUS: All 5 samples valid.");
    } else {
        Serial.println("READING STATUS: Partial readings; some samples failed.");
    }

    Serial.println("NOTE: Compare the measured value with a ruler to verify accuracy.");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    digitalWrite(TRIG_PIN, LOW);

    Serial.println();
    Serial.println("ESP32 HC-SR04 ULTRASONIC DIAGNOSTIC");
    Serial.println("--------------------------------------------");
    Serial.printf("TRIG PIN: GPIO %u\n", TRIG_PIN);
    Serial.printf("ECHO PIN: GPIO %u\n", ECHO_PIN);
    Serial.println("MEASUREMENT RANGE: 2-300 cm");
    Serial.println("TEST INTERVAL: 3 seconds");
    Serial.println("SAMPLES PER CYCLE: 5");
    Serial.println("BAUD RATE: 115200");
    Serial.println("--------------------------------------------");

    // Run the first test immediately.
    runUltrasonicTest();
    lastTestTime = millis();
}

void loop() {
    if (millis() - lastTestTime >= TEST_INTERVAL_MS) {
        lastTestTime = millis();
        runUltrasonicTest();
    }

    delay(10);
}
