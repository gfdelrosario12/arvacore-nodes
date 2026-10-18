# Test 10 — Wi-Fi Connectivity and RF Environment Audit

## Purpose
Audit the 2.4 GHz RF environment around the ESP32 node and confirm station network association. The sketch first performs an unsynchronized 2.4 GHz channel scan, cataloging available SSIDs, RSSI power levels, channels, and security suites. It then attempts connection to the configured access point without issuing any HTTP or backend transactions.

## Hardware Components Required
- ESP32-WROOM-32 development board (on-board PCB trace antenna)
- 2.4 GHz Wi-Fi 802.11b/g/n Access Point / Router

## Configuration
Open `10_wifi_test.ino` and configure your local Wi-Fi credentials:
```cpp
const char* PRIMARY_SSID = "ArvaLink-2";
const char* PRIMARY_PASS = "ARVaLINK2_2026";
const char* FALLBACK_SSID = "ArvaLink-1";
const char* FALLBACK_PASS = "ARVaLINK1_2026";
```
*(Default configured to the ArvaCore deployment APs: Primary `ArvaLink-2` / Fallback `ArvaLink-1`)*

## Critical RF & Security Principles
- **2.4 GHz Only**: The ESP32 hardware radio supports only 2.4 GHz ISM band (Channels 1–13/14). It **cannot** detect or connect to 5 GHz or 6 GHz Wi-Fi networks. Ensure dual-band routers have 2.4 GHz enabled with a distinct or compatible SSID.
- **Credential Protection**: The firmware logs the SSID, IP, Gateway, and RSSI, but **never logs plaintext passwords** to the Serial output.
- **Antenna Clearance**: Keep metallic tools, breadboard power rails, and hands at least 2 cm away from the ESP32 meander PCB antenna during testing to avoid detuning and impedance mismatch.

## Expected Output
```text
================================================================================
  1. SCANNING 2.4 GHz RF ENVIRONMENT
================================================================================
Scanning for available 802.11 b/g/n access points...
RESULT: Found 5 wireless networks.

--------------------------------------------------------------------------------
No.  | SSID                     | RSSI   | CH   | Security Protocol 
--------------------------------------------------------------------------------
1    | ArvaLink-2               |  -52 dB | 6    | WPA2-PSK          
2    | ArvaLink-1               |  -58 dB | 1    | WPA2-PSK          
3    | Office_Guest             |  -84 dB | 11   | Open (No Security)
...
TARGET AP CHECK   : Target SSID 'ArvaLink-2' WAS DETECTED in RF scan.

================================================================================
  2. ATTEMPTING STATION CONNECTION
================================================================================
CONNECTING TO     : ArvaLink-2

PASSWORD          : [PROTECTED / NOT PRINTED]
TIMEOUT LIMIT     : 15000 ms
....
CONNECTION STATUS : SUCCESSFUL (WL_CONNECTED)
TIME TO CONNECT   : 2150 ms
ASSIGNED IP       : 192.168.1.145
SUBNET MASK       : 255.255.255.0
GATEWAY IP        : 192.168.1.1
PRIMARY DNS       : 192.168.1.1
SIGNAL STRENGTH   : -52 dBm (Excellent)
ESP32 BSSID       : A4:2B:B0:XX:YY:ZZ
```

## Failure Diagnosis & Troubleshooting
1. **No Networks Found (Found 0 wireless networks)**:
   - Check if the ESP32 RF shield or antenna trace is mechanically damaged.
   - Verify that your router is broadcasting a 2.4 GHz band signal.
2. **WL_NO_SSID_AVAIL**:
   - The ESP32 cannot find the specified SSID. Check for typos (SSID names are case-sensitive) or hidden SSID broadcast settings.
3. **WL_CONNECT_FAILED**:
   - Pre-Shared Key (PSK / password) mismatch, or unsupported WPA3-Enterprise configuration.
4. **RSSI Weaker than -85 dBm**:
   - Signal is near receiver sensitivity floor (~ -96 dBm). Expect intermittent packet drops. Move closer to the router or remove metallic obstacles.

