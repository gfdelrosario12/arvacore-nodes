# Test 03 — Analog Soil Moisture Sensor Electrical Test

## Purpose
Independently evaluate the analog soil moisture sensor connected to ESP32 GPIO 34 (ADC1_CH6). The test acquires 20 analog samples per test cycle, calculates the mean, spread, variance, and calibrated millivolts (`analogReadMilliVolts()`), and discriminates between open-circuit floating pins, saturated ground shorts, excessive electrical noise, and valid analog signal levels.

## Hardware Components Required
- ESP32-WROOM-32 development board
- Resistive or Capacitive Analog Soil Moisture Sensor module (e.g. Capacitive Soil Moisture Sensor v1.2)
- 3-pin connection cable (VCC, GND, AO)
- Cup of clean dry air and cup of tap water (for manual immersion calibration check)
- Digital Multimeter (DMM)

## Exact Wiring Schematic
```text
  [ESP32 DEV MODULE]                     [ANALOG SOIL SENSOR]
       3V3 or 5V ------------------------> VCC (Note: Check sensor version)
             GND ------------------------> GND (Common Ground)
         GPIO 34 <------------------------ AO (Analog Voltage Output)
```

> **Warning regarding VCC Voltage**:
> If the sensor is powered by 5.0V, ensure its maximum analog output voltage never exceeds **3.3V**. Many capacitive sensors (v1.2) operate stably and linearly when powered directly from the ESP32 **3.3V rail**, which guarantees the output voltage never exceeds 3.3V. If your sensor outputs >3.3V when powered by 5V, power it from 3.3V or install an appropriate resistor divider.

## Calibration Constants & Logic
- `SOIL_AIR_VALUE = 3200`: Measured ADC reading when the sensor prongs are completely dry in open air.
- `SOIL_WATER_VALUE = 1500`: Measured ADC reading when the sensor prongs are submerged in shallow water (do not submerge past the white component line).
- **Inverse Relationship**: Standard capacitive soil sensors output a higher voltage in dry conditions (low dielectric capacitance) and lower voltage in moist conditions (high dielectric capacitance).

## Expected Output (Sensor in Air)
```text
================================================================================
  SOIL SENSOR ELECTRICAL AUDIT CYCLE
================================================================================
CYCLE NUMBER      : #1
TIMESTAMP         : 3000 ms
RAW ADC AVERAGE   : 3192.4 / 4095
RAW MIN / MAX     : Min = 3180 | Max = 3205 (Spread: 25 counts)
SIGNAL VARIANCE   : 42.15 counts^2
MEASURED VOLTAGE  : 2420.5 mV (~2.421 V)
SOIL SENSOR STATUS: ANALOG SIGNAL DETECTED
SOIL ADC VALUE    : 3192/4095
SOIL ADC VOLTAGE  : 2420.5 mV
SOIL MOISTURE VALUE: 0.45% (Calculated relative to Air: 3200, Water: 1500)
CALIBRATION NOTE  : Moisture percentage is a provisional estimate. True volumetric moisture
                    content requires multi-point soil sample calibration.
```

## Failure Diagnosis & Troubleshooting
1. **Status: ELECTRICAL READING SATURATED HIGH (ADC >= 4050)**:
   - Meaning: The pin is reading full rail voltage (~3.1V–3.3V) or floating without connection.
   - Troubleshooting:
     - Check whether the sensor AO jumper has disconnected from GPIO 34.
     - Check if the sensor has power (measure VCC-GND on the sensor PCB using a multimeter).
     - Remember: GPIO 34 is input-only; a disconnected pin will float and pick up ambient electrostatic noise or drift to 4095.
2. **Status: ELECTRICAL READING SATURATED LOW (ADC <= 50)**:
   - Meaning: Pin is shorted to ground or the sensor output transistor/op-amp is held at 0V.
   - Troubleshooting: Check for pinched wires, solder bridges between AO and GND, or a reversed VCC/GND connection.
3. **Status: UNSTABLE ANALOG NOISE DETECTED (Spread > 350 counts)**:
   - Meaning: High impedance source or excessive ripple on the 3.3V power rail.
   - Troubleshooting: Place a 100 nF (0.1 µF) ceramic capacitor between GPIO 34 and GND directly at the ESP32 header pins.

