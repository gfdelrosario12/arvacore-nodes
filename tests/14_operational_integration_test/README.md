# Test 14 — ArvaNode Operational Integration Test

## 1. Executive Summary
This document defines the complete end-to-end system test for the ArvaNode IoT Sensor Node.
The full system integration incorporates:
- Node identity generation via ESP-IDF eFuse MAC address (`NODE-XXXX`).
- State machine persistence in non-volatile flash via ESP32 `Preferences` (`arvanode` namespace).
- Pushbutton debouncing and edge detection for Activation (GPIO 19) and Pairing (GPIO 23).
- Dual LED indicator control (Green Status LED on GPIO 25, Blue Activation/Telemetry LED on GPIO 26).
- Dual sensor data acquisition (HC-SR04 ultrasonic distance and analog soil moisture).
- Live backend verification and REST uplink telemetry via `sketch_oct11a/sketch_oct11a.ino`.

---

## 2. Complete State Machine & LED Signaling Table

| System State | Green LED (GPIO 25) | Blue LED (GPIO 26) | Trigger Event / Condition | Next State / Action |
|---|---|---|---|---|
| **STANDBY (Unpowered)** | OFF (LOW) | OFF (LOW) | Initial boot or monitoring stopped | Press GPIO 19 to power on and enable pairing |
| **UNPAIRED (Powered)** | OFF (LOW) | OFF (LOW) | Booted, `isDevicePoweredOn = true`, unpaired | Press GPIO 23 to submit pairing request |
| **WAITING_FOR_PAIRING** | Continuous Slow Blink (500ms ON / 500ms OFF) | OFF (LOW) | Backend returned `PENDING` status | Node automatically polls backend status every 5s |
| **PAIRING_APPROVED** | Solid ON (HIGH) | OFF (LOW) | Backend confirms pairing approved | Press GPIO 19 to run sensor audit and start monitoring |
| **ACTIVATION_FAILED** | Solid ON (HIGH) | Triple Flash (3x 250ms) | Sensor audit failed or unapproved | Monitoring denied; stays in Standby |
| **MONITORING_ACTIVE** | Solid ON (HIGH) | Solid ON (HIGH) | Sensor audit passed, activation verified | First telemetry in 3s; repeats every 1s |
| **UPLINK_TRANSMITTING** | Solid ON (HIGH) | Continuous Rapid Blink (100ms ON / 100ms OFF) | Preparing & sending HTTP POST telemetry payload | Restores to Solid ON upon response receipt |
| **PAIRING_REVOKED** | OFF (LOW) | Triple Flash (3x 250ms) | Backend status check fails during cycle | Monitoring immediately terminated |

---

## 3. Production Firmware Operational Sequence Test

### Test Step 1: Standby Boot Check
1. Flash `sketch_oct11a/sketch_oct11a.ino` to the ESP32.
2. Open Serial Monitor at **115200 baud**.
3. **Verify Output**:
   - Prints chip MAC, derived `NodeId`, and confirms `Preferences` initialized.
   - Both LEDs must remain **OFF**.
   - Mode reported: `STANDBY`.

### Test Step 2: Power-On & Pairing Initiation
1. Press the **Activation Button (GPIO 19)** once.
   - Serial reports: `Device logically activated. Pairing button is now enabled.`
2. Press the **Pairing Button (GPIO 23)** once.
   - Serial reports: `[PAIRING] POST https://api-aquaflow.gladwindr.me/api/v1/pairing/request`
   - Payload:
     ```json
     {"nodeId":"NODE-XXXX","macAddress":"XX:XX:XX:XX:XX:XX","hardwareModel":"ESP32-WROOM-32","firmwareVersion":"2.1.0"}
     ```
   - If the backend returns `PENDING`:
     - Green LED begins continuous slow blinking.
     - Serial reports: `Pairing is pending administrator approval. Waiting and checking automatically...`
     - Loop queries status every 5 seconds without requiring button presses.
   - Once approved by admin:
     - Green LED becomes **Solid ON**.
     - Serial reports: `Backend approved this node.`

### Test Step 3: Activation & Hardware Audit
1. Press the **Activation Button (GPIO 19)** again.
2. The firmware automatically runs `verifyBackendPairingStatus()` and `runActivationAudit()`:
   - Soil ADC reading must not be saturated (< 4050 and > 50).
   - Ultrasonic echo pulse test must succeed.
3. If both tests pass:
   - Blue LED flashes once, then illuminates **Solid ON**.
   - Serial reports: `Monitoring active. First telemetry cycle will run in 3 seconds (repeats every 1 second).`

### Test Step 4: Telemetry Schedule and Uplink Validation
1. Observe the Blue LED 3 seconds after activation:
   - Blue LED starts **rapid continuous blinking** (100ms interval).
   - Serial Monitor prints:
     ```text
     [TELEMETRY] Sequence #1
     [TELEMETRY] POST https://api-aquaflow.gladwindr.me/api/v1/telemetry/uplink/NODE-XXXX
     [TELEMETRY] HTTP status: 200
     [SUCCESS] Telemetry accepted. Sequence #1
     ```
   - Blue LED returns to Solid ON.
2. Subsequent cycles repeat every **1 second**, with the Blue LED blinking rapidly during each upload burst.

### Test Step 5: Deactivation
1. Press the **Activation Button (GPIO 19)** while monitoring is active.
   - Serial reports: `Monitoring stopped. Device remains powered.`
   - Blue LED turns **OFF**.
   - Green LED remains **Solid ON** (pairing approval preserved in flash).
   - Telemetry cycle halts.
