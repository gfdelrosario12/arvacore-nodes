// ============================================================================
// AUTO-GENERATED ARVANODE CONFIGURATION HEADER
// Node ID: ARVANODE-007 (ArvaNode Field 07)
// Firmware: 1.2.0 | Build: bld-20261017184705
// ============================================================================

#ifndef ARVA_NODE_CONFIG_H
#define ARVA_NODE_CONFIG_H

#define ARVA_CONFIG_INJECTED 1

// Node Identity
#define NODE_LOGICAL_ID "ARVANODE-007"
#define NODE_DISPLAY_NAME "ArvaNode Field 07"
#define NODE_FIRMWARE_VERSION "1.2.0"
#define NODE_BUILD_ID "bld-20261017184705"
#define NODE_DEVICE_KEY "dev_sec_7fd6eee6d715538406b2104103dcad259ae1ad583373c1485f1b3ead"
#define NODE_HARDWARE_MAC ""

// Network Configuration (Primary & Fallback Priority)
#define NODE_PRIMARY_WIFI_SSID "ZTE_2.4G_5gKub"
#define NODE_PRIMARY_WIFI_PASS "H0meDLR$2025"
#define NODE_FALLBACK_WIFI_SSID "ZTE_2.4G_5gKub"
#define NODE_FALLBACK_WIFI_PASS "H0meDLR$2025"
#define NODE_WIFI_SSID "ZTE_2.4G_5gKub"
#define NODE_WIFI_PASS "H0meDLR$2025"
#define NODE_BACKEND_URL "https://api-aquaflow.gladwindr.me/api/v1"
#define NODE_GATEWAY_KEY "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9"

// Sensor Geometry & Timing
#define NODE_DATUM_OFFSET_CM 20.00f
#define NODE_MIN_DISTANCE_CM 2.00f
#define NODE_MAX_DISTANCE_CM 450.00f
#define NODE_SOIL_AIR_VALUE 3200
#define NODE_SOIL_WATER_VALUE 1500
#define NODE_TELEMETRY_INTERVAL_MS 3000UL

// Hardware Pinout
#define PIN_SOIL_ANALOG 34
#define PIN_ULTRASONIC_TRIG 5
#define PIN_ULTRASONIC_ECHO 18
#define PIN_BTN_ACTIVATE 19
#define PIN_BTN_PAIRING 23
#define PIN_LED_PAIRING 25
#define PIN_LED_ACTIVATE 26

#endif // ARVA_NODE_CONFIG_H
