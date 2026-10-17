#include <Arduino.h>
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_mac.h"
#include "esp_chip_info.h"

// =============================================================================
// TEST 01: ESP32 BOARD AND GPIO AUDIT
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Purpose: Audit chip silicon, revision, memory, clocks, reset reason, MAC,
//          and configure ArvaNode GPIO pin states safely for hardware audit.
// NOTE: Software initialization proves only that the firmware configured the 
//       register; it does NOT prove physical trace or pad continuity.
// =============================================================================

// ArvaNode Defined Pin Assignments
constexpr uint8_t PIN_SOIL_AO       = 34; // Analog Input (Input-only pin on ESP32)
constexpr uint8_t PIN_ULTRASONIC_TRIG = 5;  // Digital Output
constexpr uint8_t PIN_ULTRASONIC_ECHO = 18; // Digital Input (Via Voltage Divider)
constexpr uint8_t PIN_BTN_ACTIVATE  = 19; // Digital Input (Active LOW with internal pull-up)
constexpr uint8_t PIN_BTN_PAIRING   = 23; // Digital Input (Active LOW with internal pull-up)
constexpr uint8_t PIN_LED_PAIRING   = 25; // Digital Output (Green LED)
constexpr uint8_t PIN_LED_ACTIVATE  = 26; // Digital Output (Blue LED)

const char* getResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "Vbat Power-on reset";
        case ESP_RST_EXT:       return "External pin reset";
        case ESP_RST_SW:        return "Software reset digital core";
        case ESP_RST_PANIC:     return "Software panic/exception reset";
        case ESP_RST_INT_WDT:   return "Interrupt Watchdog reset";
        case ESP_RST_TASK_WDT:  return "Task Watchdog reset";
        case ESP_RST_WDT:       return "Other Watchdog reset";
        case ESP_RST_DEEPSLEEP: return "Wakeup from deep sleep";
        case ESP_RST_BROWNOUT:  return "Brownout reset (VDD voltage dropped below threshold)";
        case ESP_RST_SDIO:      return "Reset over SDIO";
        default:                return "Unknown reset reason";
    }
}

void printSectionHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    printSectionHeader("ARVANODE TEST 01: ESP32 BOARD AND GPIO AUDIT");
    Serial.println(F("ELECTRICAL SAFETY REMINDER:"));
    Serial.println(F("- ESP32 GPIOs operate at 3.3V logic. Never expose pins directly to 5V."));
    Serial.println(F("- GPIO 34 is an INPUT-ONLY pin. No internal pull-up or pull-down is available."));
    Serial.println(F("- HC-SR04 ECHO (5V) must pass through a 1k/2k resistor voltage divider to GPIO 18."));

    // 1. CHIP INFORMATION
    printSectionHeader("1. SYSTEM SILICON & REVISION");
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    Serial.printf("CHIP MODEL          : ESP32 (Features: %s%s%s)\n",
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi 802.11bgn " : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
        (chip_info.features & CHIP_FEATURE_BT) ? "BT Classic" : "");
    Serial.printf("SILICON REVISION    : Revision %d\n", chip_info.revision);
    Serial.printf("CPU CORES           : %d cores @ %u MHz\n", chip_info.cores, getCpuFrequencyMhz());
    Serial.printf("XTAL FREQUENCY      : %u MHz\n", getXtalFrequencyMhz());
    Serial.printf("APB BUS FREQUENCY   : %u Hz\n", getApbFrequency());
    Serial.printf("SDK VERSION         : %s\n", esp_get_idf_version());
    Serial.printf("RESET REASON        : %s\n", getResetReason(esp_reset_reason()));

    // 2. MEMORY AND STORAGE
    printSectionHeader("2. MEMORY & FLASH STORAGE AUDIT");
    Serial.printf("INTERNAL FREE HEAP  : %u bytes\n", ESP.getFreeHeap());
    Serial.printf("MIN FREE HEAP EVER  : %u bytes\n", ESP.getMinFreeHeap());
    Serial.printf("HEAP ALLOC SIZE MAX : %u bytes\n", ESP.getMaxAllocHeap());
    Serial.printf("FLASH CHIP SIZE     : %u bytes (%u MB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("FLASH CHIP SPEED    : %u Hz\n", ESP.getFlashChipSpeed());
    Serial.printf("FLASH CHIP MODE     : %s\n", 
        (ESP.getFlashChipMode() == FM_QIO ? "QIO" :
         ESP.getFlashChipMode() == FM_QOUT ? "QOUT" :
         ESP.getFlashChipMode() == FM_DIO ? "DIO" :
         ESP.getFlashChipMode() == FM_DOUT ? "DOUT" : "UNKNOWN"));

    // 3. NETWORK IDENTITY (MAC ADDRESS)
    printSectionHeader("3. HARDWARE IDENTITY (MAC ADDRESSES)");
    uint8_t mac_sta[6];
    if (esp_read_mac(mac_sta, ESP_MAC_WIFI_STA) == ESP_OK) {
        Serial.printf("WIFI STATION MAC    : %02X:%02X:%02X:%02X:%02X:%02X\n",
            mac_sta[0], mac_sta[1], mac_sta[2], mac_sta[3], mac_sta[4], mac_sta[5]);
        Serial.printf("DERIVED NODE ID     : NODE-%02X%02X\n", mac_sta[4], mac_sta[5]);
    } else {
        Serial.println(F("WIFI STATION MAC    : FAILED to read from eFuse."));
    }

    // 4. GPIO SUBSYSTEM INITIALIZATION
    printSectionHeader("4. ARVANODE GPIO CONFIGURATION AUDIT");
    Serial.println(F("[AUDIT NOTE] Software configuration sets the internal registers."));
    Serial.println(F("             Physical connectivity must be verified with multimeter or scope."));

    // Configure Outputs
    pinMode(PIN_LED_PAIRING, OUTPUT);
    digitalWrite(PIN_LED_PAIRING, LOW);
    Serial.printf("GPIO %2d [LED_PAIRING]   : OUTPUT configured. Driven LOW.\n", PIN_LED_PAIRING);

    pinMode(PIN_LED_ACTIVATE, OUTPUT);
    digitalWrite(PIN_LED_ACTIVATE, LOW);
    Serial.printf("GPIO %2d [LED_ACTIVATE]  : OUTPUT configured. Driven LOW.\n", PIN_LED_ACTIVATE);

    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    Serial.printf("GPIO %2d [HC-SR04 TRIG]  : OUTPUT configured. Driven LOW.\n", PIN_ULTRASONIC_TRIG);

    // Configure Inputs
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);
    Serial.printf("GPIO %2d [HC-SR04 ECHO]  : INPUT configured (Requires external level shifter to 3.3V).\n", PIN_ULTRASONIC_ECHO);

    pinMode(PIN_BTN_ACTIVATE, INPUT_PULLUP);
    Serial.printf("GPIO %2d [BTN_ACTIVATE]  : INPUT_PULLUP configured. Logic state: %s\n", 
        PIN_BTN_ACTIVATE, digitalRead(PIN_BTN_ACTIVATE) == LOW ? "LOW (Pressed/Short to GND)" : "HIGH (Released/Pulled up)");

    pinMode(PIN_BTN_PAIRING, INPUT_PULLUP);
    Serial.printf("GPIO %2d [BTN_PAIRING]   : INPUT_PULLUP configured. Logic state: %s\n", 
        PIN_BTN_PAIRING, digitalRead(PIN_BTN_PAIRING) == LOW ? "LOW (Pressed/Short to GND)" : "HIGH (Released/Pulled up)");

    // Configure ADC
    pinMode(PIN_SOIL_AO, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_SOIL_AO, ADC_11db);
    Serial.printf("GPIO %2d [SOIL_PIN AO]   : ANALOG INPUT configured (12-bit, 11dB attenuation, ~0-3100mV range).\n", PIN_SOIL_AO);
    Serial.printf("                     Initial raw ADC sample: %d / 4095\n", analogRead(PIN_SOIL_AO));

    printSectionHeader("AUDIT SUMMARY");
    Serial.println(F("BOARD HARDWARE AUDIT: COMPLETED"));
    Serial.println(F("EXECUTION STABILITY : Board operational. Loop heartbeat running every 5 seconds."));
    Serial.println(F("NEXT STEP           : Execute Test 02 through Test 08 to test individual transducers."));
}

unsigned long lastHeartbeat = 0;
uint32_t heartbeatCount = 0;

void loop() {
    unsigned long now = millis();
    if (now - lastHeartbeat >= 5000) {
        lastHeartbeat = now;
        heartbeatCount++;
        Serial.printf("[HEARTBEAT #%u] Uptime: %lu ms | Free Heap: %u bytes | Min Heap: %u bytes\n",
            heartbeatCount, now, ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }
    delay(10);
}

