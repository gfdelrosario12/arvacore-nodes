#include <Arduino.h>
#include <WiFi.h>

// =============================================================================
// TEST 10: WI-FI CONNECTIVITY AND RF ENVIRONMENT AUDIT
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Purpose: Scan 2.4 GHz RF environment, report detected SSIDs, RSSI, channels,
//          encryption protocols, and attempt station connection using configured
//          credentials.
// SECURITY NOTE: Wi-Fi passwords are NEVER printed to Serial Monitor.
// ISOLATION NOTE: This test isolates RF and IP layers; NO backend calls are made.
// =============================================================================

// Standard ArvaCore Wi-Fi Credentials (Primary & Fallback Priority)
const char* PRIMARY_SSID = "ArvaLink-2";
const char* PRIMARY_PASS = "ARVaLINK2_2026";
const char* FALLBACK_SSID = "ArvaLink-1";
const char* FALLBACK_PASS = "ARVaLINK1_2026";

const char* TARGET_SSID = PRIMARY_SSID;
const char* TARGET_PASS = PRIMARY_PASS;

constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;


void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

const char* getEncryptionTypeStr(wifi_auth_mode_t authType) {
    switch (authType) {
        case WIFI_AUTH_OPEN:            return "Open (No Security)";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        default:                        return "Unknown Security";
    }
}

void scanNetworks() {
    printHeader("1. SCANNING 2.4 GHz RF ENVIRONMENT");
    Serial.println(F("Scanning for available 802.11 b/g/n access points..."));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(200);

    int networkCount = WiFi.scanNetworks(false, true); // synchronous scan, show hidden

    if (networkCount == 0) {
        Serial.println(F("RESULT: No 2.4 GHz Wi-Fi networks found in range."));
        Serial.println(F("DIAGNOSIS: Check ESP32 antenna circuit, or ensure 2.4 GHz AP is within range."));
        return;
    }

    Serial.printf("RESULT: Found %d wireless networks.\n\n", networkCount);
    Serial.println(F("--------------------------------------------------------------------------------"));
    Serial.printf("%-4s | %-24s | %-6s | %-4s | %-18s\n", "No.", "SSID", "RSSI", "CH", "Security Protocol");
    Serial.println(F("--------------------------------------------------------------------------------"));

    bool targetFound = false;
    for (int i = 0; i < networkCount; i++) {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        int32_t channel = WiFi.channel(i);
        wifi_auth_mode_t enc = WiFi.encryptionType(i);

        if (ssid == TARGET_SSID) {
            targetFound = true;
        }

        Serial.printf("%-4d | %-24.24s | %4d dB | %-4d | %-18s\n",
            i + 1,
            ssid.length() > 0 ? ssid.c_str() : "<Hidden SSID>",
            rssi,
            channel,
            getEncryptionTypeStr(enc));
    }
    Serial.println(F("--------------------------------------------------------------------------------"));

    if (targetFound) {
        Serial.printf("TARGET AP CHECK   : Target SSID '%s' WAS DETECTED in RF scan.\n", TARGET_SSID);
    } else {
        Serial.printf("TARGET AP CHECK   : Target SSID '%s' WAS NOT FOUND in RF scan.\n", TARGET_SSID);
        Serial.println(F("                    Ensure the AP broadcasts on 2.4 GHz (ESP32 does NOT support 5 GHz)."));
    }
}

bool connectToTargetNetwork() {
    printHeader("2. ATTEMPTING STATION CONNECTION");

    if (strlen(TARGET_SSID) == 0) {
        Serial.println(F("ABORT: TARGET_SSID is empty. Configure credentials in sketch."));
        return false;
    }

    Serial.printf("CONNECTING TO     : %s\n", TARGET_SSID);
    Serial.println(F("PASSWORD          : [PROTECTED / NOT PRINTED]"));
    Serial.printf("TIMEOUT LIMIT     : %lu ms\n", WIFI_CONNECT_TIMEOUT_MS);

    WiFi.mode(WIFI_STA);
    WiFi.begin(TARGET_SSID, TARGET_PASS);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("CONNECTION STATUS : SUCCESSFUL (WL_CONNECTED)"));
        Serial.printf("TIME TO CONNECT   : %lu ms\n", millis() - start);
        Serial.printf("ASSIGNED IP       : %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("SUBNET MASK       : %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("GATEWAY IP        : %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("PRIMARY DNS       : %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("SIGNAL STRENGTH   : %d dBm (", WiFi.RSSI());

        int rssi = WiFi.RSSI();
        if (rssi > -60) Serial.println(F("Excellent)"));
        else if (rssi > -70) Serial.println(F("Good)"));
        else if (rssi > -80) Serial.println(F("Fair/Borderline)"));
        else Serial.println(F("Poor — High Packet Loss Risk)"));

        Serial.printf("ESP32 BSSID       : %s\n", WiFi.BSSIDstr().c_str());
        return true;
    } else {
        Serial.println(F("CONNECTION STATUS : FAILED (Connection Timed Out)"));
        Serial.printf("FINAL REASON CODE : ");
        switch (WiFi.status()) {
            case WL_NO_SSID_AVAIL:   Serial.println(F("WL_NO_SSID_AVAIL — Target SSID not found or out of range.")); break;
            case WL_CONNECT_FAILED:  Serial.println(F("WL_CONNECT_FAILED — Password rejected or handshake failed.")); break;
            case WL_DISCONNECTED:    Serial.println(F("WL_DISCONNECTED — AP dropped connection or DHCP failed.")); break;
            default:                 Serial.printf("Status code %d\n", WiFi.status()); break;
        }
        return false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    printHeader("TEST 10: WI-FI CONNECTIVITY AND RF ENVIRONMENT AUDIT");
    scanNetworks();
    Serial.println();
    connectToTargetNetwork();

    printHeader("TEST 10 COMPLETE");
    Serial.println(F("ISOLATION CONFIRMATION: Station link tested without issuing backend API requests."));
}

void loop() {
    // Keep connection active and monitor RSSI every 5 seconds if connected
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 5000) {
        lastCheck = millis();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI MONITOR] Status: CONNECTED | IP: %s | RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else {
            Serial.println(F("[WIFI MONITOR] Status: DISCONNECTED from Access Point."));
        }
    }
    delay(100);
}

