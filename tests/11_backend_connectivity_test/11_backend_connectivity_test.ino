#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_mac.h"

// =============================================================================
// TEST 11: ARVACORE BACKEND CONNECTIVITY AND HTTPS/TLS INTEGRATION TEST
// Target: ESP32-WROOM-32 / ESP32 Dev Module
// Baud Rate: 115200
// Purpose: Test end-to-end network integration against the real ArvaCore API:
//          1. DNS resolution of api-aquaflow.gladwindr.me
//          2. TLS handshake latency (HTTPS port 443)
//          3. Gateway Key authentication header (X-ArvaCore-Gateway-Key)
//          4. Real endpoint test: GET /api/v1/pairing/status/{nodeId}
// =============================================================================

// Network Credentials
const char* WIFI_SSID = "Cream";
const char* WIFI_PASSWORD = "hatdog123";

// ArvaCore Real API Configuration
const char* BACKEND_BASE_URL = "https://api-aquaflow.gladwindr.me/api/v1";
const char* GATEWAY_KEY      = "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9";
const char* HOSTNAME         = "api-aquaflow.gladwindr.me";

constexpr unsigned long HTTP_TIMEOUT_MS = 10000;
String nodeId;

void printHeader(const char* title) {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.printf("  %s\n", title);
    Serial.println(F("================================================================================"));
}

void deriveNodeId() {
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
        nodeId = "NODE-" + String(buf);
    } else {
        nodeId = "NODE-01";
    }
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.printf("Connecting to Wi-Fi SSID '%s'...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected. IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    } else {
        Serial.println(F("ERROR: Wi-Fi connection failed. Ensure Test 10 passes first."));
        return false;
    }
}

void testDnsResolution() {
    printHeader("STEP 1: DNS RESOLUTION AUDIT");
    Serial.printf("Resolving target hostname: %s\n", HOSTNAME);

    IPAddress resolvedIp;
    unsigned long startDns = millis();
    int err = WiFi.hostByName(HOSTNAME, resolvedIp);
    unsigned long duration = millis() - startDns;

    if (err == 1) {
        Serial.println(F("DNS STATUS        : RESOLVED SUCCESSFULLY"));
        Serial.printf("RESOLVED IP       : %s\n", resolvedIp.toString().c_str());
        Serial.printf("RESOLUTION TIME   : %lu ms\n", duration);
    } else {
        Serial.println(F("DNS STATUS        : FAILED — Hostname could not be resolved."));
        Serial.println(F("DIAGNOSIS         : Primary DNS server is unreachable or hostname does not exist."));
        Serial.printf("CONFIGURED DNS IP : %s\n", WiFi.dnsIP().toString().c_str());
    }
}

void testBackendPairingStatusEndpoint() {
    printHeader("STEP 2: HTTPS / TLS HANDSHAKE AND API QUERY");

    String url = String(BACKEND_BASE_URL) + "/pairing/status/" + nodeId;
    Serial.printf("TARGET ENDPOINT   : %s\n", url.c_str());
    Serial.printf("NODE ID TESTED    : %s\n", nodeId.c_str());
    Serial.println(F("AUTH HEADER       : X-ArvaCore-Gateway-Key: [CONFIGURED / PROTECTED]"));

    WiFiClientSecure client;
    client.setInsecure(); // Skip X.509 cert validation for IoT test flexibility
    client.setTimeout(HTTP_TIMEOUT_MS);

    HTTPClient http;
    unsigned long tStart = millis();

    if (!http.begin(client, url)) {
        Serial.println(F("TLS/HTTP STATUS   : FAILED to initialize HTTPClient transport."));
        return;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Accept", "application/json");
    http.addHeader("X-ArvaCore-Gateway-Key", GATEWAY_KEY);

    Serial.println(F("Sending HTTP GET request over TLS..."));
    int httpCode = http.GET();
    unsigned long latency = millis() - tStart;

    Serial.printf("ROUNDTRIP TIME    : %lu ms\n", latency);
    Serial.printf("HTTP STATUS CODE  : %d\n", httpCode);

    if (httpCode > 0) {
        String payload = http.getString();
        Serial.printf("RESPONSE BODY     : %s\n", payload.c_str());

        if (httpCode == 200) {
            Serial.println(F("DIAGNOSTIC RESULT : SUCCESS — Backend is reachable and responded with HTTP 200 OK."));
        } else if (httpCode == 404) {
            Serial.println(F("DIAGNOSTIC RESULT : API REACHABLE (HTTP 404) — Node ID is not yet registered in backend database."));
            Serial.println(F("                    This confirms network, DNS, and TLS work; submit pairing to register."));
        } else if (httpCode == 401 || httpCode == 403) {
            Serial.println(F("DIAGNOSTIC RESULT : AUTHENTICATION REJECTED (HTTP 401/403) — Verify GATEWAY_KEY in sketch."));
        } else {
            Serial.printf("DIAGNOSTIC RESULT : UNEXPECTED STATUS CODE HTTP %d.\n", httpCode);
        }
    } else {
        Serial.printf("TRANSPORT FAILURE : Code %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
        Serial.println(F("DIAGNOSIS         : Handshake timeout, TLS cipher mismatch, or server port 443 refused."));
    }

    http.end();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    deriveNodeId();
    printHeader("TEST 11: ARVACORE BACKEND CONNECTIVITY AND HTTPS/TLS INTEGRATION TEST");
    Serial.printf("DERIVED NODE ID   : %s\n", nodeId.c_str());
    Serial.printf("API BASE URL      : %s\n", BACKEND_BASE_URL);

    if (connectWiFi()) {
        testDnsResolution();
        testBackendPairingStatusEndpoint();
    }

    printHeader("TEST 11 COMPLETE");
}

void loop() {
    delay(1000);
}
