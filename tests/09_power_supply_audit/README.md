# Test 09 — Power Supply and Voltage Audit Procedure

## 1. Overview & Technical Limitations
> **CRITICAL EMBEDDED ENGINEERING REALITY**:
> Software running on the ESP32 **CANNOT measure its own supply voltage (VCC/VIN)** or arbitrary external sensor rails without dedicated, calibrated external hardware voltage dividers connected to ADC pins. Any software that claims to measure external battery voltage or 5V VBUS without hardware dividers is producing fabricated or static mock values.
>
> This document details the physical test procedure, required instrumentation, safe electrical thresholds, and step-by-step audit steps to validate the ArvaNode electrical power distribution network.

---

## 2. Required Test Instruments
- **Digital Multimeter (DMM)**: With calibrated DC Voltage mode (0.01V resolution minimum) and Continuity mode.
- **Oscilloscope (Optional but recommended)**: For capturing dynamic voltage dips and transience during Wi-Fi transmission bursts (RF TX power surges up to 450 mA).
- **USB In-Line Power Meter (Optional)**: For monitoring current draw from the host computer or wall adapter.

---

## 3. Power Distribution Architecture (ESP32-WROOM-32 Dev Module)

```text
[USB 5V / VBUS] ------------------------> [AMS1117 / ME6211 3.3V LDO] --------> [ESP32 3.3V Rail]
       |                                                                               |
       +--------> [HC-SR04 VCC Pin (5.0V)]                                             +---> [Capacitive Soil Sensor (3.3V)]
                                                                                       +---> [Buttons Pull-Up Circuits]
                                                                                       +---> [LED Anode Resistors]
```

---

## 4. Voltage Audit Checkpoints & Permissible Tolerances

| Test Point ID | Physical Location | Nominal Voltage | Acceptable Range | What It Powers | Failure Symptom if Out of Range |
|---|---|---|---|---|---|
| **TP-1 (VBUS/VIN)** | ESP32 `VIN` or `5V` pin | +5.00 V DC | 4.75 V to 5.25 V | Board LDO regulator & HC-SR04 VCC | USB brownout, HC-SR04 pulse failure |
| **TP-2 (3V3 RAIL)** | ESP32 `3V3` pin | +3.30 V DC | 3.20 V to 3.36 V | ESP32 Core, Radio, Flash, Soil Sensor | Core reboot, Flash read crash, ADC drift |
| **TP-3 (GND REF)** | Sensor Ground vs ESP32 GND | 0.00 V DC | < 0.05 V (50 mV) | Common Ground reference | Ground loop, ultrasonic trigger offset |
| **TP-4 (SOIL VCC)** | Soil Sensor VCC Terminal | +3.30 V or 5.0 V | ± 5% of target | Soil oscillator / amplifier | Inaccurate/zero ADC readings |
| **TP-5 (HC-SR04 ECHO)** | Node between 1k & 2k divider | Pulse peak | 3.10 V to 3.40 V | GPIO 18 Echo input | Overvoltage on GPIO 18 if > 3.6V! |

---

## 5. Step-by-Step Measurement Procedure

### Step 5.1: USB Supply Under Idle vs Radio Load
1. Plug the ESP32 into your workstation USB port.
2. Set the DMM to **DC Volts (20V range)**.
3. Place Black probe on ESP32 `GND`; place Red probe on `VIN` (or `5V` pin).
   - **Measure Idle Voltage**: Record $V_{\text{idle}}$ (must be $\ge 4.80\text{V}$).
4. Run a Wi-Fi scan or transmit routine (or observe during Wi-Fi connection attempt):
   - **Measure Loaded Voltage**: Record $V_{\text{load}}$.
   - **Evaluation**: If $V_{\text{load}} < 4.60\text{V}$, the USB cable has excessive resistance ($R_{\text{cable}} > 1\,\Omega$) or the host USB port is current-limited. **Replace cable immediately.**

### Step 5.2: 3.3V System Rail Stability
1. Keep Black probe on `GND`; place Red probe on `3V3` header pin.
2. Measure voltage under full operating load (both LEDs lit, sensor running).
   - Expected: $3.25\text{V} - 3.33\text{V}$.
   - If voltage is below $3.15\text{V}$, the on-board linear regulator (AMS1117-3.3) is dropping out or overheating.

### Step 5.3: HC-SR04 Voltage Divider Safety Verification
> **DANGER ZONE CHECK**: Protect ESP32 GPIO 18 from 5V over-voltage destruction.
1. Disconnect the jumper going into ESP32 GPIO 18.
2. Leave the 1k/2k divider connected between the HC-SR04 ECHO pin and GND.
3. Power the circuit and trigger pings or observe continuous pings.
4. Measure the voltage at the junction between the 1k resistor and the 2k resistor using an oscilloscope or DMM with Min/Max peak capture:
   - Nominal calculation:
     $$V_{\text{ECHO\_DIV}} = 5.0\text{V} \times \left(\frac{2000\,\Omega}{1000\,\Omega + 2000\,\Omega}\right) = 3.33\text{V}$$
   - **PASS**: Voltage is between $3.0\text{V}$ and $3.4\text{V}$. Safe to reconnect to GPIO 18.
   - **FAIL**: If voltage exceeds $3.6\text{V}$, DO NOT reconnect to GPIO 18! Verify resistor values.

### Step 5.4: Ground Plane Continuity
1. Disconnect power. Switch DMM to **Continuity / Resistance mode ($\Omega$)**.
2. Measure resistance between:
   - ESP32 GND pin and HC-SR04 GND pin.
   - ESP32 GND pin and Soil Moisture Sensor GND pin.
   - Breadboard upper ground rail and lower ground rail.
3. **Requirement**: All ground paths must measure $< 0.5\,\Omega$. Any reading $> 1.0\,\Omega$ indicates high-resistance jumper wire or breadboard spring clip oxidation.

---

## 6. Identifying ESP32 Hardware Brownout Resets
When the ESP32 VDD rail drops below approximately **2.43V–2.80V** (configurable BOD threshold), the internal analog comparator immediately triggers a hardware brownout reset.

### Symptoms in Serial Monitor:
```text
rst:0x10 (RTCWDT_RTC_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
Brownout detector was triggered
```
### Corrective Engineering Action:
1. Solder a **470 µF to 1000 µF 10V low-ESR electrolytic capacitor** across the `VIN` (5V) and `GND` pins on the breadboard or sensor expansion shield.
2. Place a **10 µF tantalum capacitor + 100 nF ceramic capacitor** directly across `3V3` and `GND`.
3. Power the HC-SR04 from an independent 5V power supply (with grounds tied together) if powering from a high-loss laptop USB port.
