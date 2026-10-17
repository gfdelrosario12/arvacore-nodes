#include <Arduino.h>

// =============================================================================
// TEST 05: PAIRING BUTTON INDEPENDENT TEST
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Pin: GPIO 23
// Configuration: INPUT_PULLUP (Active LOW)
// Purpose: Test raw electrical GPIO state, detect debounced transitions, count
//          press events, and verify pull-up resistor integrity.
// NOTE: This sketch tests only the physical switch. It does NOT connect to Wi-Fi
//       or submit pairing HTTP/MQTT requests to any backend.
// =============================================================================

constexpr uint8_t PIN_BTN_PAIRING = 23;
constexpr unsigned long DEBOUNCE_DELAY_MS = 50;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 5000;

int lastRawReading = HIGH;
int stableState = HIGH;
unsigned long lastDebounceTime = 0;
uint32_t pressCount = 0;
uint32_t releaseCount = 0;
unsigned long lastHeartbeat = 0;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_BTN_PAIRING, INPUT_PULLUP);

    printHeader("TEST 05: PAIRING BUTTON INDEPENDENT TEST");
    Serial.printf("BUTTON PIN        : GPIO %u\n", PIN_BTN_PAIRING);
    Serial.println(F("LOGIC POLARITY    : ACTIVE LOW (Internal Pull-Up enabled)"));
    Serial.printf("DEBOUNCE INTERVAL : %lu ms\n", DEBOUNCE_DELAY_MS);
    Serial.println(F("EXPECTED CIRCUIT  : One button pin to GPIO 23, opposing button pin to GND."));
    Serial.println(F("                    When released: 3.3V (HIGH). When pressed: 0V (LOW)."));
    Serial.println(F("SAFEGUARD         : No Wi-Fi or backend pairing requests are transmitted."));

    stableState = digitalRead(PIN_BTN_PAIRING);
    lastRawReading = stableState;

    Serial.println();
    Serial.printf("INITIAL STATE     : RAW = %s | INTERPRETED = %s\n",
        stableState == LOW ? "LOW" : "HIGH",
        stableState == LOW ? "PRESSED (Button held or short to GND)" : "RELEASED (Open/Pulled Up)");
}

void loop() {
    unsigned long now = millis();
    int currentRaw = digitalRead(PIN_BTN_PAIRING);

    // Debounce timer reset on raw pin change
    if (currentRaw != lastRawReading) {
        lastDebounceTime = now;
    }

    if ((now - lastDebounceTime) >= DEBOUNCE_DELAY_MS) {
        if (currentRaw != stableState) {
            stableState = currentRaw;

            printHeader("PAIRING BUTTON EVENT DETECTED");
            Serial.printf("TIMESTAMP         : %lu ms\n", now);
            Serial.printf("RAW GPIO STATE    : %s\n", stableState == LOW ? "LOW (0V)" : "HIGH (3.3V)");
            Serial.printf("PAIRING BUTTON    : %s\n", stableState == LOW ? "PRESSED" : "RELEASED");

            if (stableState == LOW) {
                pressCount++;
                Serial.printf("BUTTON EVENT      : PRESS DETECTED (Total presses: %u)\n", pressCount);
                Serial.println(F("ISOLATION CHECK   : Hardware switch closed. No network packet generated."));
            } else {
                releaseCount++;
                Serial.printf("BUTTON EVENT      : RELEASE DETECTED (Total releases: %u)\n", releaseCount);
            }
        }
    }

    lastRawReading = currentRaw;

    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        Serial.printf("[HEARTBEAT] Pairing Button GPIO %u State: %s | Press Count: %u\n",
            PIN_BTN_PAIRING,
            digitalRead(PIN_BTN_PAIRING) == LOW ? "LOW (HELD)" : "HIGH (IDLE)",
            pressCount);
    }

    delay(5);
}
