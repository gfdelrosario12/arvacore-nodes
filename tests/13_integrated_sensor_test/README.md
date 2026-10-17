# Test 13 — Integrated Sensor Subsystem Test

## Purpose
Simultaneously test the HC-SR04 ultrasonic sensor (GPIO 5 & 18) and the analog soil moisture sensor (GPIO 34) on a single ESP32 without activating pump hardware or initiating Wi-Fi transmissions.
The firmware enforces strict fault isolation: if the ultrasonic sensor experiences an echo timeout, the soil sensor continues to sample and report normally; if the soil sensor is unplugged (saturating the ADC), the ultrasonic sensor continues measuring distance.

## Hardware Components Required
- ESP32-WROOM-32 development board
- HC-SR04 ultrasonic distance sensor (VCC to 5V, GND to GND, TRIG to GPIO 5, ECHO to GPIO 18 via 1k/2k divider)
- Analog soil moisture sensor (VCC to 3.3V/5V, GND to GND, AO to GPIO 34)
- 1.0 kΩ and 2.0 kΩ voltage divider resistors
- Breadboard and jumper wires

## Circuit Summary Table
| Sensor Pin | ESP32 Connection | Notes |
|---|---|---|
| HC-SR04 VCC | VIN (5.0V) | Must be 5V for standard HC-SR04 |
| HC-SR04 GND | GND | Common system ground |
| HC-SR04 TRIG | GPIO 5 | 3.3V digital output |
| HC-SR04 ECHO | GPIO 18 | Via 1k/2k divider (3.3V safe) |
| Soil Sensor VCC | 3.3V (or 5.0V) | Check sensor version voltage limits |
| Soil Sensor GND | GND | Common system ground |
| Soil Sensor AO | GPIO 34 | Analog input (ADC1_CH6, 12-bit) |

## Expected Output
```text
================================================================================
  INTEGRATED TRANSDUCER CYCLE
================================================================================
CYCLE NUMBER      : #1 | TIMESTAMP: 3000 ms
--- [TRANSDUCER 1: SOIL MOISTURE SENSOR (GPIO 34)] ---
SOIL SENSOR STATUS: WORKING — Analog signal verified.
SOIL ADC VALUE    : 2450/4095 (Spread: 18 counts)
SOIL ADC VOLTAGE  : 1855.2 mV
SOIL MOISTURE     : 44.12%
--- [TRANSDUCER 2: HC-SR04 ULTRASONIC (GPIO 5 / 18)] ---
ULTRASONIC STATUS : WORKING — Valid echo responses detected.
ULTRASONIC VALUE  : 42.15 cm (Median of 5/5 valid samples)
--------------------------------------------------------------------------------
SUBSYSTEM HEALTH  : Soil = [PASS] | Ultrasonic = [PASS]
```

## Fault Isolation Demonstration
- **Case A: Unplug Soil Sensor AO**:
  - Transducer 1 reports `SOIL SENSOR STATUS: ELECTRICAL FAULT / SATURATION` (ADC 4095).
  - Transducer 2 reports `ULTRASONIC STATUS: WORKING` with accurate distance values.
- **Case B: Block Ultrasonic Echo (disconnect ECHO)**:
  - Transducer 1 reports `SOIL SENSOR STATUS: WORKING` with valid moisture.
  - Transducer 2 reports `ULTRASONIC STATUS: FAULT` (0/5 pings).
