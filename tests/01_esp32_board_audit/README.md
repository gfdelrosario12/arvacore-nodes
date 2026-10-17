# Test 01 — ESP32 Board and GPIO Audit

## Purpose
Inspect and verify the core ESP32-WROOM-32 hardware environment, silicon revision, CPU clock frequency, memory architecture, reset reason, MAC address derivation for `NodeId`, and initial GPIO/ADC register state configurations.

## Hardware Components Required
- ESP32-WROOM-32 development board (e.g. NodeMCU-32S / ESP32 Dev Module)
- Micro-USB data cable (ensure data lines D+/D- are present, not a charge-only cable)
- Workstation running Arduino IDE (or `arduino-cli`)

## Pin Assignments Under Audit
| Signal Name | ESP32 GPIO | Direction | Electrical Characteristics |
|---|---|---|---|
| `SOIL_PIN` AO | GPIO 34 | INPUT | Input-only pad (GPI), no internal pull-ups. ADC1_CH6 |
| `TRIG_PIN` | GPIO 5 | OUTPUT | 3.3V CMOS output |
| `ECHO_PIN` | GPIO 18 | INPUT | 3.3V input (Must receive voltage-divided 5V pulse) |
| `BTN_ACTIVATE` | GPIO 19 | INPUT_PULLUP | Active LOW, internal pull-up enabled |
| `BTN_PAIRING` | GPIO 23 | INPUT_PULLUP | Active LOW, internal pull-up enabled |
| `LED_PAIRING` (Green) | GPIO 25 | OUTPUT | 3.3V active HIGH drive |
| `LED_ACTIVATE` (Blue) | GPIO 26 | OUTPUT | 3.3V active HIGH drive |

## Electrical Safety & Precautions
- **GPIO 34 is an Input-Only Pin**: Never attempt to drive it as an output or enable internal pull-up resistors in software; the hardware does not support internal pull-ups on GPIOs 34–39.
- **3.3V Logic Tolerance**: The ESP32 is strictly a 3.3V CMOS device. Never connect 5V VCC directly to any GPIO.
- **Brownout Protection**: Ensure the USB port or USB hub supplies at least 500mA without voltage drops below 4.5V on VBUS.

## Arduino IDE Configuration
- Board: `ESP32 Dev Module`
- CPU Frequency: `240MHz (WiFi/BT)`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO` or `DIO`
- Upload Speed: `921600` or `115200`
- Baud Rate: `115200`

## Expected Output
```text
================================================================================
  ARVANODE TEST 01: ESP32 BOARD AND GPIO AUDIT
================================================================================
...
CHIP MODEL          : ESP32 (Features: WiFi 802.11bgn BLE BT Classic)
SILICON REVISION    : Revision 1
CPU CORES           : 2 cores @ 240 MHz
XTAL FREQUENCY      : 40 MHz
SDK VERSION         : v4.4...
RESET REASON        : Vbat Power-on reset
INTERNAL FREE HEAP  : 298412 bytes
FLASH CHIP SIZE     : 4194304 bytes (4 MB)
WIFI STATION MAC    : 24:6F:28:XX:YY:ZZ
DERIVED NODE ID     : NODE-YYZZ
...
GPIO 25 [LED_PAIRING]   : OUTPUT configured. Driven LOW.
GPIO 26 [LED_ACTIVATE]  : OUTPUT configured. Driven LOW.
GPIO  5 [HC-SR04 TRIG]  : OUTPUT configured. Driven LOW.
GPIO 18 [HC-SR04 ECHO]  : INPUT configured (Requires external level shifter to 3.3V).
GPIO 19 [BTN_ACTIVATE]  : INPUT_PULLUP configured. Logic state: HIGH (Released/Pulled up)
GPIO 23 [BTN_PAIRING]   : INPUT_PULLUP configured. Logic state: HIGH (Released/Pulled up)
GPIO 34 [SOIL_PIN AO]   : ANALOG INPUT configured (12-bit, 11dB attenuation, ~0-3100mV range).
...
BOARD HARDWARE AUDIT: COMPLETED
```

## Failure Diagnosis & Troubleshooting
1. **Reset Reason is "Brownout reset"**:
   - Cause: The USB cable has high conductor resistance or the USB port cannot supply transient current spikes (up to 300–500mA during RF calibration).
   - Fix: Use a shorter, thicker USB cable or connect to an externally powered USB 3.0 hub.
2. **Buttons Read LOW on Boot Without Being Pressed**:
   - Cause: External short to ground on GPIO 19 or GPIO 23, defective button mechanism, or excessive solder bridge.
   - Fix: Inspect button terminals with a digital multimeter in continuity mode.
3. **Free Heap is Unusually Low (< 100 KB on boot)**:
   - Cause: Memory leak or improper partition table allocation.
