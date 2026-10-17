#include <Arduino.h>

// =============================================================================
// TEST 03: ANALOG SOIL MOISTURE SENSOR ELECTRICAL TEST
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Pin: GPIO 34 (ADC1 Channel 6)
// Purpose: Test raw ADC conversion, calibrated millivolt response, 20-sample
//          statistical spread (min, max, variance, mean), detect electrical
//          saturation (high/low), and compute provisional moisture percentage.
// =============================================================================

constexpr uint8_t PIN_SOIL_AO = 34;
constexpr uint8_t SAMPLE_COUNT = 20;
constexpr unsigned long SAMPLE_DELAY_MS = 15;
constexpr unsigned long TEST_INTERVAL_MS = 3000;

// Configurable Calibration Reference Values (Do not silently modify)
constexpr int SOIL_AIR_VALUE = 3200;    // Dry reference in air
constexpr int SOIL_WATER_VALUE = 1500;  // Wet reference in shallow tap water

// Electrical Thresholds
constexpr int ADC_SATURATED_LOW  = 50;   // Near 0V (Potential short to GND or dead sensor)
constexpr int ADC_SATURATED_HIGH = 4050; // Near 3.3V (Disconnected VCC, floating, or open circuit)

unsigned long lastTestTime = 0;
uint32_t cycleNumber = 0;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

float calculateMoisturePercent(float rawAdc) {
    float percent = (float)(SOIL_AIR_VALUE - rawAdc) * 100.0f / (float)(SOIL_AIR_VALUE - SOIL_WATER_VALUE);
    return constrain(percent, 0.0f, 100.0f);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_SOIL_AO, INPUT);
    analogReadResolution(12); // 12-bit: 0 - 4095
    analogSetPinAttenuation(PIN_SOIL_AO, ADC_11db); // Full scale up to ~3.1V

    printHeader("TEST 03: SOIL MOISTURE SENSOR ELECTRICAL TEST");
    Serial.printf("ANALOG PIN        : GPIO %u (ADC1_CH6)\n", PIN_SOIL_AO);
    Serial.printf("ADC RESOLUTION    : 12-bit (0 - 4095)\n");
    Serial.printf("ATTENUATION       : ADC_11db (0 - ~3100 mV input range)\n");
    Serial.printf("TEST INTERVAL     : %lu ms (Averaged across %u samples)\n", TEST_INTERVAL_MS, SAMPLE_COUNT);
    Serial.printf("CALIBRATION REFS  : DRY / AIR = %d | WET / WATER = %d\n", SOIL_AIR_VALUE, SOIL_WATER_VALUE);
    Serial.println(F("ELECTRICAL RULE   : GPIO 34 is an INPUT-ONLY pin without internal pull resistors."));
    Serial.println(F("                    Outputting 0V or >3.3V will damage or invalidate readings."));

    lastTestTime = millis() - TEST_INTERVAL_MS;
}

void loop() {
    unsigned long now = millis();
    if (now - lastTestTime >= TEST_INTERVAL_MS) {
        lastTestTime = now;
        cycleNumber++;

        printHeader("SOIL SENSOR ELECTRICAL AUDIT CYCLE");
        Serial.printf("CYCLE NUMBER      : #%u\n", cycleNumber);
        Serial.printf("TIMESTAMP         : %lu ms\n", now);

        // Discard initial conversion to clear sample-and-hold circuit
        analogRead(PIN_SOIL_AO);
        delay(5);

        uint32_t rawSum = 0;
        uint32_t milliVoltSum = 0;
        int rawMin = 4096;
        int rawMax = -1;
        uint32_t samples[SAMPLE_COUNT];

        for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
            int r = analogRead(PIN_SOIL_AO);
            uint32_t mv = analogReadMilliVolts(PIN_SOIL_AO);

            samples[i] = r;
            rawSum += r;
            milliVoltSum += mv;

            if (r < rawMin) rawMin = r;
            if (r > rawMax) rawMax = r;

            delay(SAMPLE_DELAY_MS);
        }

        float rawAvg = (float)rawSum / SAMPLE_COUNT;
        float mvAvg = (float)milliVoltSum / SAMPLE_COUNT;
        int spread = rawMax - rawMin;

        // Calculate variance
        float variance = 0.0f;
        for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
            variance += ((float)samples[i] - rawAvg) * ((float)samples[i] - rawAvg);
        }
        variance /= SAMPLE_COUNT;

        Serial.printf("RAW ADC AVERAGE   : %.1f / 4095\n", rawAvg);
        Serial.printf("RAW MIN / MAX     : Min = %d | Max = %d (Spread: %d counts)\n", rawMin, rawMax, spread);
        Serial.printf("SIGNAL VARIANCE   : %.2f counts^2\n", variance);
        Serial.printf("MEASURED VOLTAGE  : %.1f mV (~%.3f V)\n", mvAvg, mvAvg / 1000.0f);

        // Signal Classification
        if (rawAvg >= ADC_SATURATED_HIGH) {
            Serial.println(F("SOIL SENSOR STATUS: ELECTRICAL READING SATURATED HIGH"));
            Serial.printf("SOIL ADC VALUE    : %d/4095\n", (int)rawAvg);
            Serial.printf("SOIL ADC VOLTAGE  : %.1f mV\n", mvAvg);
            Serial.println(F("SOIL MOISTURE VALUE: UNAVAILABLE — electrical reading invalid."));
            Serial.println(F("DIAGNOSIS         : Signal line is floating, VCC disconnected, or open circuit on AO line."));
            Serial.println(F("ACTION REQUIRED   : Inspect AO jumper wire, verify sensor power (3.3V or 5V), verify common GND."));
        } else if (rawAvg <= ADC_SATURATED_LOW) {
            Serial.println(F("SOIL SENSOR STATUS: ELECTRICAL READING SATURATED LOW"));
            Serial.printf("SOIL ADC VALUE    : %d/4095\n", (int)rawAvg);
            Serial.printf("SOIL ADC VOLTAGE  : %.1f mV\n", mvAvg);
            Serial.println(F("SOIL MOISTURE VALUE: UNAVAILABLE — electrical reading invalid."));
            Serial.println(F("DIAGNOSIS         : Signal line is shorted to GND or sensor amplifier is defective."));
            Serial.println(F("ACTION REQUIRED   : Disconnect AO pin and test voltage at sensor probe terminal with DMM."));
        } else if (spread > 350) {
            Serial.println(F("SOIL SENSOR STATUS: UNSTABLE ANALOG NOISE DETECTED"));
            Serial.printf("SOIL ADC VALUE    : %d/4095\n", (int)rawAvg);
            Serial.printf("SOIL ADC VOLTAGE  : %.1f mV\n", mvAvg);
            Serial.println(F("SOIL MOISTURE VALUE: UNAVAILABLE — excessive electrical instability."));
            Serial.println(F("DIAGNOSIS         : High electromagnetic interference, loose breadboard contact, or floating pin."));
            Serial.println(F("ACTION REQUIRED   : Add a 0.1 uF ceramic bypass capacitor between GPIO 34 and GND close to ESP32."));
        } else {
            float moisture = calculateMoisturePercent(rawAvg);
            Serial.println(F("SOIL SENSOR STATUS: ANALOG SIGNAL DETECTED"));
            Serial.printf("SOIL ADC VALUE    : %d/4095\n", (int)rawAvg);
            Serial.printf("SOIL ADC VOLTAGE  : %.1f mV\n", mvAvg);
            Serial.printf("SOIL MOISTURE VALUE: %.2f%% (Calculated relative to Air: %d, Water: %d)\n", 
                moisture, SOIL_AIR_VALUE, SOIL_WATER_VALUE);
            Serial.println(F("CALIBRATION NOTE  : Moisture percentage is a provisional estimate. True volumetric moisture"));
            Serial.println(F("                    content requires multi-point soil sample calibration."));
        }
    }

    delay(10);
}

