
#include <Arduino.h>
#include <math.h>

// ============================================================
// ARVANODE - CONSOLIDATED SENSOR DIAGNOSTICS
// Board: ESP32-WROOM-32
//
// BUTTON 1: GPIO 19 -> GND (Ultrasonic test)
// BUTTON 2: GPIO 23 -> GND (Soil moisture test)
//
// LED 1: GPIO 26 (Ultrasonic status)
// LED 2: GPIO 25 (Soil moisture status)
//
// SOIL SENSOR AO: GPIO 34
// HC-SR04 TRIG: GPIO 5
// HC-SR04 ECHO: GPIO 18 through voltage divider
//
// ULTRASONIC MAXIMUM RANGE: 300 cm (3 meters)
// LED timing: 800 ms ON, 800 ms OFF
//
// No Wi-Fi, backend, pairing, or buzzer.
// ============================================================

// ---------------- GPIO CONFIGURATION ------------------------

constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t TRIG_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;

constexpr uint8_t BUTTON_ULTRASONIC = 19;
constexpr uint8_t BUTTON_SOIL = 23;

constexpr uint8_t LED_ULTRASONIC = 26;
constexpr uint8_t LED_SOIL = 25;

// ---------------- SOIL CALIBRATION --------------------------

constexpr int SOIL_AIR_VALUE = 3200;
constexpr int SOIL_WATER_VALUE = 1500;

constexpr float DRY_LIMIT_PERCENT = 30.0f;
constexpr float WET_LIMIT_PERCENT = 70.0f;

// ---------------- ULTRASONIC SETTINGS -----------------------

constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;
constexpr float MIN_DISTANCE_CM = 2.0f;
constexpr float MAX_DISTANCE_CM = 300.0f;

constexpr unsigned long ECHO_TIMEOUT_US = 25000UL;

constexpr uint8_t ULTRASONIC_SAMPLES = 5;
constexpr uint8_t SOIL_SAMPLES = 20;

// ---------------- TIMING ------------------------------------

constexpr unsigned long DEBOUNCE_MS = 50;
constexpr unsigned long LED_FLASH_MS = 800;
constexpr unsigned long LED_PAUSE_MS = 800;

// ============================================================
// BUTTON STATE AND DEBOUNCE
// ============================================================

struct ButtonState {
    uint8_t pin;
    int lastReading;
    int stableState;
    unsigned long lastChange;
};

ButtonState ultrasonicButton = {
    BUTTON_ULTRASONIC, HIGH, HIGH, 0
};

ButtonState soilButton = {
    BUTTON_SOIL, HIGH, HIGH, 0
};

bool buttonPressed(ButtonState &button) {
    unsigned long now = millis();
    int reading = digitalRead(button.pin);

    if (reading != button.lastReading) {
        button.lastReading = reading;
        button.lastChange = now;
    }

    if (now - button.lastChange < DEBOUNCE_MS) {
        return false;
    }

    if (reading != button.stableState) {
        button.stableState = reading;

        // INPUT_PULLUP: LOW means the button is pressed.
        return button.stableState == LOW;
    }

    return false;
}

// ============================================================
// LED CONTROL
// ============================================================

void flashLED(uint8_t pin, uint8_t count) {
    digitalWrite(pin, LOW);

    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(pin, HIGH);
        delay(LED_FLASH_MS);

        digitalWrite(pin, LOW);
        delay(LED_PAUSE_MS);
    }
}

// ============================================================
// ULTRASONIC SENSOR
// ============================================================

bool readUltrasonic(float &distanceCm) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long durationUs = pulseIn(
        ECHO_PIN,
        HIGH,
        ECHO_TIMEOUT_US
    );

    if (durationUs == 0) {
        return false;
    }

    distanceCm =
        (durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;

    return isfinite(distanceCm) &&
           distanceCm >= MIN_DISTANCE_CM &&
           distanceCm <= MAX_DISTANCE_CM;
}

void sortDistances(float values[], uint8_t count) {
    for (uint8_t i = 1; i < count; i++) {
        float current = values[i];
        int j = i - 1;

        while (j >= 0 && values[j] > current) {
            values[j + 1] = values[j];
            j--;
        }

        values[j + 1] = current;
    }
}

void testUltrasonic() {
    float readings[ULTRASONIC_SAMPLES];
    uint8_t validCount = 0;

    // Collect readings without printing individual samples.
    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++) {
        float distanceCm = 0.0f;

        if (readUltrasonic(distanceCm)) {
            readings[validCount++] = distanceCm;
        }

        // Allow echoes and reflections to settle between pings.
        delay(100);
    }

    Serial.println();

    if (validCount == 0) {
        Serial.println(
            "[ULTRASONIC] ERROR: No valid reading within 2-300 cm."
        );

        flashLED(LED_ULTRASONIC, 5);
        return;
    }

    // Median reduces the effect of anomalous readings.
    sortDistances(readings, validCount);

    float finalDistance;

    if (validCount % 2 == 1) {
        finalDistance = readings[validCount / 2];
    } else {
        finalDistance = (
            readings[validCount / 2 - 1] +
            readings[validCount / 2]
        ) / 2.0f;
    }

    const char *status;
    uint8_t flashes;

    // Distance bands for a maximum 3-meter sensing range.
    if (finalDistance < 100.0f) {
        status = "NEAR";
        flashes = 1;
    } else if (finalDistance < 200.0f) {
        status = "MID-RANGE";
        flashes = 2;
    } else {
        status = "FAR";
        flashes = 3;
    }

    Serial.printf(
        "[ULTRASONIC] Final distance: %.2f cm | Status: %s | Valid readings: %u/%u | Range: 2-300 cm\n",
        finalDistance,
        status,
        validCount,
        ULTRASONIC_SAMPLES
    );

    flashLED(LED_ULTRASONIC, flashes);
}

// ============================================================
// SOIL MOISTURE SENSOR
// ============================================================

int readSoilADC() {
    unsigned long total = 0;

    // Discard the initial ADC conversion.
    analogRead(SOIL_PIN);
    delay(5);

    for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
        total += analogRead(SOIL_PIN);
        delay(10);
    }

    return total / SOIL_SAMPLES;
}

float calculateSoilMoisture(int adc) {
    float percentage =
        (float)(SOIL_AIR_VALUE - adc) * 100.0f /
        (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE);

    return constrain(percentage, 0.0f, 100.0f);
}

void testSoilMoisture() {
    int averageADC = readSoilADC();

    Serial.println();

    if (averageADC <= 10 || averageADC >= 4085) {
        Serial.printf(
            "[SOIL] ERROR: ADC reading outside expected range (%d/4095).\n",
            averageADC
        );

        flashLED(LED_SOIL, 5);
        return;
    }

    float moisture = calculateSoilMoisture(averageADC);

    const char *status;
    uint8_t flashes;

    if (moisture < DRY_LIMIT_PERCENT) {
        status = "DRY";
        flashes = 1;
    } else if (moisture <= WET_LIMIT_PERCENT) {
        status = "MODERATE";
        flashes = 2;
    } else {
        status = "WET";
        flashes = 3;
    }

    Serial.printf(
        "[SOIL] Final ADC: %d/4095 | Estimated moisture: %.2f%% | Status: %s\n",
        averageADC,
        moisture,
        status
    );

    flashLED(LED_SOIL, flashes);
}

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(300);

    pinMode(SOIL_PIN, INPUT);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    pinMode(BUTTON_ULTRASONIC, INPUT_PULLUP);
    pinMode(BUTTON_SOIL, INPUT_PULLUP);

    pinMode(LED_ULTRASONIC, OUTPUT);
    pinMode(LED_SOIL, OUTPUT);

    digitalWrite(TRIG_PIN, LOW);
    digitalWrite(LED_ULTRASONIC, LOW);
    digitalWrite(LED_SOIL, LOW);

    analogReadResolution(12);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);

    // Initialize button debounce states.
    ultrasonicButton.lastReading =
        digitalRead(BUTTON_ULTRASONIC);
    ultrasonicButton.stableState =
        ultrasonicButton.lastReading;
    ultrasonicButton.lastChange = millis();

    soilButton.lastReading =
        digitalRead(BUTTON_SOIL);
    soilButton.stableState =
        soilButton.lastReading;
    soilButton.lastChange = millis();

    Serial.println("[ARVANODE] Ready.");
    Serial.println("[ARVANODE] Ultrasonic maximum range: 300 cm.");
    Serial.println("[ARVANODE] LED flashes: 800 ms ON, 800 ms OFF.");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
    if (buttonPressed(ultrasonicButton)) {
        testUltrasonic();
    }

    if (buttonPressed(soilButton)) {
        testSoilMoisture();
    }

    delay(2);
}
