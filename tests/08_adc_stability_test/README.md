# Test 08 — ESP32 ADC Stability and Noise Characterization

## Purpose
Perform high-rate statistical characterization of the ESP32 internal Successive Approximation Register (SAR) ADC on GPIO 34 (ADC1_CH6). The sketch takes 100 consecutive burst samples, computes peak-to-peak jitter, standard deviation, variance, and calibrated millivolts to evaluate electrical noise, input impedance effects, power rail ripple, and floating-pin phenomena.

## Hardware Components Required
- ESP32-WROOM-32 development board
- Component under test on GPIO 34 (Soil sensor, potentiometer, or clean resistor divider)
- 1x 100 nF (0.1 µF) ceramic capacitor (optional for noise suppression)
- Digital Multimeter (DMM)

## Electrical Characteristics of ESP32 ADC1
1. **Resolution**: 12-bit (0 to 4095 levels).
2. **Attenuation**: Set to `ADC_11db`, providing an input voltage range of approximately 150 mV to 3100 mV.
3. **Non-Linearity Zones**:
   - The ESP32 ADC is notably non-linear below ~100 mV and above ~3000 mV.
   - For precision measurements, analog signals should ideally swing between 0.2V and 2.8V.
4. **Input Impedance & Source Resistance**:
   - The ESP32 internal sampling capacitor needs time to charge. If the external sensor source impedance is $>10\text{ k}\Omega$, sampling errors and noise increase drastically.
5. **Floating Pin Myth**:
   - *A disconnected pin DOES NOT stay at 0V.* Because GPIO 34 is high-impedance CMOS, an unconnected wire acts as an antenna, picking up 50/60 Hz AC mains hum and radio emissions, producing values ranging wildly from 200 to 4095.

## Test Conditions to Benchmark
1. **Bench Condition A (Clean Ground)**: Connect a 10 kΩ resistor between GPIO 34 and GND.
   - *Expected Result*: Mean $\approx 0\text{--}40$ counts, Standard Deviation $< 3.0$ counts.
2. **Bench Condition B (Clean Reference Divider)**: Connect two 10 kΩ resistors as a divider between 3.3V and GND, with the center tap to GPIO 34.
   - *Expected Result*: Mean $\approx 2048$ counts (~1650 mV), Standard Deviation $< 8.0$ counts.
3. **Bench Condition C (Floating Pin)**: Unplug any jumper from GPIO 34.
   - *Expected Result*: High jitter, Spread $> 500$ counts, High standard deviation.
4. **Bench Condition D (Connected Soil Sensor)**: Sensor connected in air.
   - *Expected Result*: Stable reading ($\approx 3100\text{--}3300$ counts) with low jitter ($< 50$ counts).

## Expected Output
```text
================================================================================
  ADC STABILITY EVALUATION WINDOW
================================================================================
CYCLE NUMBER      : #1
TIMESTAMP         : 2000 ms
RAW ADC MEAN      : 2451.80 / 4095
MIN / MAX RAW     : Min = 2442 | Max = 2465
PEAK-TO-PEAK JITTER: 23 ADC counts (~17.4 mV equivalent ripple)
STANDARD DEVIATION: 4.82 counts (Variance: 23.23)
CALIBRATED VOLTAGE: 1856.2 mV (~1.856 V)

ELECTRICAL STATE  : EXCELLENT STABILITY — Low noise, clean impedance.
ANALYSIS          : Signal source impedance is within spec (<10k) with minimal ripple.
```

## Troubleshooting & Noise Reduction Guide
- **Add a Decoupling Capacitor**: Solder or insert a 100 nF ceramic capacitor between GPIO 34 and GND directly adjacent to the ESP32 header. This shunts high-frequency RF and switching regulator noise directly to ground.
- **Multisampling**: Always average 10–20 readings in firmware to filter white Gaussian thermal noise.
- **Power Decoupling**: If jitter exceeds 150 counts with a sensor connected, verify the 3.3V regulator rail using a DMM or oscilloscope for high ripple caused by cheap USB switch-mode power supplies.

