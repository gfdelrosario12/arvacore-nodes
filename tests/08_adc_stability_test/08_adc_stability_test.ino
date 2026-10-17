#include <Arduino.h>

// =============================================================================
// TEST 08: ESP32 ADC STABILITY AND NOISE CHARACTERIZATION
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Pin: GPIO 34 (ADC1 Channel 6)
// Purpose: Dedicated high-resolution characterization of ESP32 Successive
//          Approximation Register (SAR) ADC. Evaluates thermal noise, baseline
//          drift, quantization spread, millivolt calibration, and floating input
//          vulnerabilities on high-impedance analog sensor traces.
// =============================================================================

constexpr uint8_t ADC_PIN = 34;
constexpr uint16_t BURST_SAMPLES = 100;
constexpr unsigned long TEST_INTERVAL_MS = 2000;

unsigned long lastTestTime = 0;
uint32_t reportCycle = 0;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(ADC_PIN, ADC_11db);

    printHeader("TEST 08: ESP32 ADC NOISE AND STABILITY CHARACTERIZATION");
    Serial.printf("ANALOG CHANNEL    : GPIO %u (ADC1_CH6)\n", ADC_PIN);
    Serial.printf("SAMPLING DEPTH    : %u continuous samples per evaluation window\n", BURST_SAMPLES);
    Serial.printf("ATTENUATION       : ADC_11db (Full-scale range: ~150 mV to ~3100 mV)\n");
    Serial.printf("CYCLE INTERVAL    : %lu ms\n", TEST_INTERVAL_MS);
    Serial.println(F("TECHNICAL CONTEXT : The ESP32 SAR ADC has known non-linearities at extremes (<100mV"));
    Serial.println(F("                    and >3100mV). Floating input pins will pick up mains 50/60Hz hum"));
    Serial.println(F("                    and ambient radio frequencies, producing erratic readings."));

    lastTestTime = millis() - TEST_INTERVAL_MS;
}

void loop() {
    unsigned long now = millis();

    if (now - lastTestTime >= TEST_INTERVAL_MS) {
        lastTestTime = now;
        reportCycle++;

        printHeader("ADC STABILITY EVALUATION WINDOW");
        Serial.printf("CYCLE NUMBER      : #%u\n", reportCycle);
        Serial.printf("TIMESTAMP         : %lu ms\n", now);

        // Pre-read conversion to flush sample/hold capacitor
        analogRead(ADC_PIN);
        delayMicroseconds(200);

        uint32_t rawSum = 0;
        uint32_t mvSum = 0;
        int rawMin = 4096;
        int rawMax = -1;
        uint16_t rawSamples[BURST_SAMPLES];

        for (uint16_t i = 0; i < BURST_SAMPLES; i++) {
            int raw = analogRead(ADC_PIN);
            uint32_t mv = analogReadMilliVolts(ADC_PIN);

            rawSamples[i] = raw;
            rawSum += raw;
            mvSum += mv;

            if (raw < rawMin) rawMin = raw;
            if (raw > rawMax) rawMax = raw;

            delayMicroseconds(250); // High-rate burst sampling
        }

        float rawMean = (float)rawSum / BURST_SAMPLES;
        float mvMean = (float)mvSum / BURST_SAMPLES;
        int rawSpread = rawMax - rawMin;

        // Compute Variance & Standard Deviation
        float varianceSum = 0.0f;
        for (uint16_t i = 0; i < BURST_SAMPLES; i++) {
            float diff = (float)rawSamples[i] - rawMean;
            varianceSum += (diff * diff);
        }
        float variance = varianceSum / BURST_SAMPLES;
        float stdDev = sqrt(variance);

        // Compute peak-to-peak millivolt ripple approximation
        float mvSpread = (float)rawSpread * (3100.0f / 4095.0f);

        Serial.printf("RAW ADC MEAN      : %.2f / 4095\n", rawMean);
        Serial.printf("MIN / MAX RAW     : Min = %d | Max = %d\n", rawMin, rawMax);
        Serial.printf("PEAK-TO-PEAK JITTER: %d ADC counts (~%.1f mV equivalent ripple)\n", rawSpread, mvSpread);
        Serial.printf("STANDARD DEVIATION: %.2f counts (Variance: %.2f)\n", stdDev, variance);
        Serial.printf("CALIBRATED VOLTAGE: %.1f mV (~%.3f V)\n", mvMean, mvMean / 1000.0f);

        // Interpretation
        Serial.println();
        Serial.print(F("ELECTRICAL STATE  : "));
        if (rawMean >= 4050) {
            Serial.println(F("SATURATED HIGH (Rail 3.3V or Open Circuit)"));
            Serial.println(F("ANALYSIS          : Pin is pinned to VDD or completely disconnected/floating."));
        } else if (rawMean <= 40) {
            Serial.println(F("SATURATED LOW (0.0V / Tied to GND)"));
            Serial.println(F("ANALYSIS          : Pin is shorted directly to Ground or driver is off."));
        } else if (rawSpread <= 40) {
            Serial.println(F("EXCELLENT STABILITY — Low noise, clean impedance."));
            Serial.println(F("ANALYSIS          : Signal source impedance is within spec (<10k) with minimal ripple."));
        } else if (rawSpread <= 150) {
            Serial.println(F("MODERATE NOISE — Typical ESP32 SAR ADC baseline ripple."));
            Serial.println(F("ANALYSIS          : Normal for unshielded jumper wires. Software multisampling will filter it."));
        } else {
            Serial.println(F("HIGH JITTER / SUSPECTED FLOATING PIN."));
            Serial.printf("ANALYSIS          : Spread is %d counts! Pin is likely floating without sensor attached\n", rawSpread);
            Serial.println(F("                    or power supply ground is noisy. Add 100nF filter capacitor."));
        }
    }

    delay(10);
}

