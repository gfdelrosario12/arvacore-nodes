# ArvaNode Hardware Diagnostics & Troubleshooting Guide

## 1. Troubleshooting Decision Tree

```text
[Node Failure Detected]
         |
         v
[Is Serial Monitor Output Available @ 115200 Baud?]
   |                                     |
  NO                                    YES
   |                                     |
   +---> Check USB cable (data lines?)    +---> Does Test 01 boot without Brownout reset?
   +---> Check CP2102/CH340 drivers        |        |
   +---> Check 5V/3.3V power rails (DMM)   |       NO  --> [Power Fault: USB voltage < 4.6V or cable drop]
                                           |       YES --> [Proceed to Subsystem Audit]
                                           |
    +--------------------------------------+--------------------------------------+
    |                                      |                                      |
    v                                      v                                      v
[Ultrasonic Sensor Fault]             [Soil Sensor Fault]                  [Network / Backend Fault]
    |                                      |                                      |
    +-> Timeouts (0/5 valid)?              +-> ADC = 4095 (Saturated High)?       +-> No SSIDs found?
    |   - VCC connected to 5V?             |   - AO jumper loose / disconnected   |   - Antenna damaged or AP 5GHz only
    |   - Divider wired correctly?         |   - Sensor VCC missing               |
    |   - Target within 2-300 cm?          +-> ADC = 0 (Saturated Low)?           +-> Wi-Fi connect failed?
    +-> Inaccurate Distance?               |   - AO shorted to GND                |   - Password typo (case-sensitive)
        - Check alignment (perpendicular)  |   - Defective probe amplifier        |   - DHCP IP exhaustion
        - Settle time between pings?       +-> Jitter > 350 counts?               +-> HTTPS Transport Error?
                                               - Add 100nF filter capacitor           - DNS resolution failed
                                               - Check ground loop continuity         - Gateway Key header mismatch
```

---

## 2. Common Hardware Symptoms, Root Causes & Field Remedies

### Symptom 1: ESP32 Reboots in an Infinite Loop ("Brownout detector was triggered")
- **Root Cause**: The 3.3V rail dropped below the internal brownout detector voltage (~2.43V–2.80V) when the ESP32 radio chip engaged full RF transmit power (peak bursts up to 450 mA).
- **Remedy**:
  1. Replace long or thin USB cables with a high-gauge, short USB data cable.
  2. Solder a 470 µF to 1000 µF electrolytic capacitor across the 5V and GND rails.
  3. Avoid powering the ESP32 through unpowered USB hubs or keyboard passthrough ports.

### Symptom 2: HC-SR04 Produces 100% Echo Timeouts
- **Root Cause**:
  1. The sensor VCC is connected to the ESP32 3.3V pin instead of the 5.0V pin. Classic HC-SR04 units will not trigger ultrasonic bursts below ~4.5V.
  2. Missing common ground between the HC-SR04 and the ESP32.
  3. The voltage divider is wired incorrectly, attenuating the pulse below the ESP32 minimum CMOS input HIGH threshold ($V_{IH} \approx 0.75 \times VDD = 2.475\text{V}$).
- **Remedy**:
  - Connect HC-SR04 VCC to `VIN` (5V USB rail).
  - Use exact resistor values: $R_1 = 1.0\text{ k}\Omega$ (from ECHO to GPIO 18), $R_2 = 2.0\text{ k}\Omega$ (from GPIO 18 to GND).
  - Verify pulse with an oscilloscope or multimeter.

### Symptom 3: Soil Moisture Sensor Reads Flatline 4095 (or 0)
- **Root Cause**:
  1. Reading 4095: GPIO 34 is an input-only pin without internal pull-ups or pull-downs. If the AO jumper wire is loose or the sensor has lost power, the high-impedance gate drifts to the rail or picks up noise.
  2. Reading 0: The AO line is pinched against a ground shield or the sensor board amplifier is blown.
- **Remedy**:
  - Measure DC voltage directly at the sensor's AO terminal pin with a DMM:
    - In dry air: Should measure $\approx 2.4\text{V} - 2.8\text{V}$.
    - Dipped in water: Should drop to $\approx 1.2\text{V} - 1.6\text{V}$.
  - If voltage changes at the sensor but not on GPIO 34, replace the jumper wire.

### Symptom 4: Button Reads Constantly Pressed (LOW)
- **Root Cause**:
  - The tactile switch has 4 pins connected internally in pairs (A1-B1 and A2-B2). If inserted rotated 90 degrees into a breadboard, it permanently shorts GPIO 19 or 23 to Ground.
- **Remedy**:
  - Rotate the tactile switch 90 degrees or wire to two diagonally opposite pins.

### Symptom 5: Blue or Green LED Fails to Light Up
- **Root Cause**:
  - Reversed diode polarity (Anode/Cathode swapped).
  - Current-limiting resistor too high (e.g. 10 kΩ instead of 220 Ω).
  - Blue LED forward voltage threshold ($V_f \approx 3.0\text{V}$) not reached.
- **Remedy**:
  - Longer leg = Anode (+) -> connected to GPIO pin through 150Ω–220Ω resistor.
  - Shorter leg / flat rim = Cathode (-) -> connected to GND.

---

## 3. Recommended Sequential Test Execution Order

To systematically isolate faults without cross-component interference, execute the test suite in this strict sequence:

1. **`tests/09_power_supply_audit`**: Validate 5.0V, 3.3V, common ground, and voltage divider with DMM.
2. **`tests/01_esp32_board_audit`**: Verify ESP32 silicon, flash memory, reset reason, and GPIO registers.
3. **`tests/06_green_led_test`**: Verify Green LED driver circuit and visual visibility.
4. **`tests/07_blue_led_test`**: Verify Blue LED driver circuit and rapid blinking.
5. **`tests/04_activation_button_test`**: Confirm Activation button pull-up and contact closure.
6. **`tests/05_pairing_button_test`**: Confirm Pairing button pull-up and contact closure.
7. **`tests/08_adc_stability_test`**: Benchmark baseline ADC noise and identify floating lines.
8. **`tests/03_soil_moisture_test`**: Verify analog soil sensor electrical response and air/water span.
9. **`tests/02_ultrasonic_test`**: Verify HC-SR04 ultrasonic echo pulse generation and median filtering.
10. **`tests/13_integrated_sensor_test`**: Validate dual-transducer concurrent acquisition and fault isolation.
11. **`tests/10_wifi_test`**: Verify 2.4 GHz RF environment and station DHCP association.
12. **`tests/11_backend_connectivity_test`**: Confirm DNS, TLS 443 handshake, and Gateway Key authentication.
13. **`tests/14_operational_integration_test`**: Run full production firmware lifecycle validation (`sketch_oct11a`).

