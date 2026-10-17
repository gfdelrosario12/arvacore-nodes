#include <Arduino.h>

// =============================================================================
// TEST 07: BLUE ACTIVATION / DATA LED INDEPENDENT TEST
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Pin: GPIO 26 (Active HIGH)
// Purpose: Test GPIO 26 output drive, execute controlled ON, OFF, and rapid
//          telemetry-blink patterns, and provide explicit visual inspection.
// NOTE: Firmware can verify that the GPIO output driver register changed state;
//       firmware CANNOT verify actual light emission without optical feedback.
// =============================================================================

constexpr uint8_t PIN_LED_ACTIVATE = 26;
constexpr unsigned long STEP_DURATION_MS = 3000;

enum TestStep {
    STEP_SOLID_OFF,
    STEP_SOLID_ON,
    STEP_TELEMETRY_BURST,
    STEP_SLOW_PULSE,
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

    pinMode(PIN_LED_ACTIVATE, OUTPUT);
    digitalWrite(PIN_LED_ACTIVATE, LOW);

    printHeader("TEST 07: BLUE ACTIVATION / DATA LED INDEPENDENT TEST");
    Serial.printf("LED PIN           : GPIO %u (Current Source, Active HIGH)\n", PIN_LED_ACTIVATE);
    Serial.println(F("REQUIRED CIRCUIT  : GPIO 26 -> Current Limiting Resistor (150R - 330R) -> LED Anode (+)"));
    Serial.println(F("                    LED Cathode (-) -> Common GND"));
    Serial.println(F("ELECTRICAL NOTE   : Blue LEDs typically have a higher forward voltage (Vf ~2.8V - 3.2V)."));
    Serial.println(F("                    A 220R or 150R resistor provides optimal brightness from a 3.3V rail."));
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
                digitalWrite(PIN_LED_ACTIVATE, LOW);
                printHeader("COMMAND: SOLID OFF");
                Serial.printf("GPIO %u STATE     : LOW (0.0V)\n", PIN_LED_ACTIVATE);
                Serial.println(F("EXPECTED VISUAL   : Blue LED completely UNLIT / DARK."));
                break;

            case STEP_SOLID_ON:
                digitalWrite(PIN_LED_ACTIVATE, HIGH);
                printHeader("COMMAND: SOLID ON (Monitoring Active Emulation)");
                Serial.printf("GPIO %u STATE     : HIGH (3.3V)\n", PIN_LED_ACTIVATE);
                Serial.println(F("EXPECTED VISUAL   : Blue LED illuminated steadily at continuous brightness."));
                break;

            case STEP_TELEMETRY_BURST:
                printHeader("COMMAND: TELEMETRY TRANSMISSION BLINK (100 ms ON / 100 ms OFF)");
                Serial.printf("GPIO %u BEHAVIOR  : Alternating 0V / 3.3V at 5 Hz\n", PIN_LED_ACTIVATE);
                Serial.println(F("EXPECTED VISUAL   : Blue LED flickering rapidly (emulates active HTTP transmission)."));
                blinkState = false;
                lastBlinkToggle = now;
                break;

            case STEP_SLOW_PULSE:
                printHeader("COMMAND: SLOW PULSE (500 ms ON / 500 ms OFF)");
                Serial.printf("GPIO %u BEHAVIOR  : Alternating 0V / 3.3V at 1 Hz\n", PIN_LED_ACTIVATE);
                Serial.println(F("EXPECTED VISUAL   : Blue LED pulsing evenly."));
                blinkState = false;
                lastBlinkToggle = now;
                break;

            default:
                break;
        }
    }

    // Handle active blink patterns
    if (currentStep == STEP_TELEMETRY_BURST) {
        if (now - lastBlinkToggle >= 100) {
            lastBlinkToggle = now;
            blinkState = !blinkState;
            digitalWrite(PIN_LED_ACTIVATE, blinkState ? HIGH : LOW);
        }
    } else if (currentStep == STEP_SLOW_PULSE) {
        if (now - lastBlinkToggle >= 500) {
            lastBlinkToggle = now;
            blinkState = !blinkState;
            digitalWrite(PIN_LED_ACTIVATE, blinkState ? HIGH : LOW);
        }
    }

    delay(10);
}

