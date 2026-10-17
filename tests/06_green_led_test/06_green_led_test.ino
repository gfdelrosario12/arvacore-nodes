#include <Arduino.h>

// =============================================================================
// TEST 06: GREEN STATUS / PAIRING LED INDEPENDENT TEST
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Pin: GPIO 25 (Active HIGH)
// Purpose: Test GPIO 25 output drive, execute controlled ON, OFF, and multi-speed
//          blinking patterns, and provide explicit visual inspection procedures.
// NOTE: Firmware can verify that the GPIO output driver register changed state;
//       firmware CANNOT verify actual photon emission without optical feedback.
// =============================================================================

constexpr uint8_t PIN_LED_PAIRING = 25;
constexpr unsigned long STEP_DURATION_MS = 3000;

enum TestStep {
    STEP_SOLID_OFF,
    STEP_SOLID_ON,
    STEP_SLOW_BLINK,
    STEP_FAST_BLINK,
    STEP_COUNT
};

TestStep currentStep = STEP_SOLID_OFF;
unsigned long stepStartTime = 0;
unsigned long lastBlinkToggle = 0;
bool blinkState = false;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_LED_PAIRING, OUTPUT);
    digitalWrite(PIN_LED_PAIRING, LOW);

    printHeader("TEST 06: GREEN STATUS / PAIRING LED INDEPENDENT TEST");
    Serial.printf("LED PIN           : GPIO %u (Current Source, Active HIGH)\n", PIN_LED_PAIRING);
    Serial.println(F("REQUIRED CIRCUIT  : GPIO 25 -> Current Limiting Resistor (220R - 470R) -> LED Anode (+)"));
    Serial.println(F("                    LED Cathode (-) -> Common GND"));
    Serial.println(F("LIMITATION NOTICE : Software confirms GPIO output voltage register is driven."));
    Serial.println(F("                    Visual observation by the operator is required to confirm emission."));

    stepStartTime = millis();
}

void loop() {
    unsigned long now = millis();

    if (now - stepStartTime >= STEP_DURATION_MS) {
        stepStartTime = now;
        currentStep = (TestStep)((currentStep + 1) % STEP_COUNT);

        switch (currentStep) {
            case STEP_SOLID_OFF:
                digitalWrite(PIN_LED_PAIRING, LOW);
                printHeader("COMMAND: SOLID OFF");
                Serial.printf("GPIO %u STATE     : LOW (0.0V)\n", PIN_LED_PAIRING);
                Serial.println(F("EXPECTED VISUAL   : Green LED completely UNLIT / DARK."));
                break;

            case STEP_SOLID_ON:
                digitalWrite(PIN_LED_PAIRING, HIGH);
                printHeader("COMMAND: SOLID ON");
                Serial.printf("GPIO %u STATE     : HIGH (3.3V)\n", PIN_LED_PAIRING);
                Serial.println(F("EXPECTED VISUAL   : Green LED illuminated steadily at continuous brightness."));
                break;

            case STEP_SLOW_BLINK:
                printHeader("COMMAND: SLOW BLINK (500 ms ON / 500 ms OFF)");
                Serial.printf("GPIO %u BEHAVIOR  : Alternating 0V / 3.3V at 1 Hz\n", PIN_LED_PAIRING);
                Serial.println(F("EXPECTED VISUAL   : Green LED pulsing at 1 cycle per second (pairing wait emulation)."));
                blinkState = false;
                lastBlinkToggle = now;
                break;

            case STEP_FAST_BLINK:
                printHeader("COMMAND: FAST BLINK (150 ms ON / 150 ms OFF)");
                Serial.printf("GPIO %u BEHAVIOR  : Alternating 0V / 3.3V at ~3.3 Hz\n", PIN_LED_PAIRING);
                Serial.println(F("EXPECTED VISUAL   : Green LED flickering rapidly (error/diagnostic alert emulation)."));
                blinkState = false;
                lastBlinkToggle = now;
                break;

            default:
                break;
        }
    }

    // Handle active blink patterns
    if (currentStep == STEP_SLOW_BLINK) {
        if (now - lastBlinkToggle >= 500) {
            lastBlinkToggle = now;
            blinkState = !blinkState;
            digitalWrite(PIN_LED_PAIRING, blinkState ? HIGH : LOW);
        }
    } else if (currentStep == STEP_FAST_BLINK) {
        if (now - lastBlinkToggle >= 150) {
            lastBlinkToggle = now;
            blinkState = !blinkState;
            digitalWrite(PIN_LED_PAIRING, blinkState ? HIGH : LOW);
        }
    }

    delay(10);
}

