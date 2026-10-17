# Test 02 — HC-SR04 Ultrasonic Sensor Independent Test

## Purpose
Independently test the HC-SR04 ultrasonic distance sensor on GPIO 5 (TRIG) and GPIO 18 (ECHO). The sketch sends trigger pulses, measures microsecond echo return times, filters timeouts and out-of-range echoes, sorts valid readings to compute the statistical median, and reports detailed transducer diagnostics every 3 seconds.

## Hardware Components Required
- ESP32-WROOM-32 development board
- HC-SR04 ultrasonic distance sensor module (standard 5V version)
- Voltage divider resistors:
  - Resistor R1: 1.0 kΩ (between HC-SR04 ECHO and ESP32 GPIO 18)
  - Resistor R2: 2.0 kΩ (between ESP32 GPIO 18 and common GND)
  *(Alternatively, a bidirectional 5V-to-3.3V logic level shifter)*
- Breadboard and jumper wires
- Rigid target object (box, wall, book) and standard physical ruler/tape measure

## Exact Wiring Schematic
```text
  [ESP32 DEV MODULE]                     [HC-SR04 SENSOR]
         VIN / 5V ------------------------> VCC (5.0V)
              GND ------------------------> GND (Common Ground)
          GPIO 5  ------------------------> TRIG (3.3V CMOS directly triggers HC-SR04)

                                            ECHO (5.0V Output)
                                              |
                                            [ 1.0 kΩ (R1) ]
                                              |
          GPIO 18 <---------------------------+ (V_div = 5V * 2k / (1k + 2k) ≈ 3.33V)
                                              |
                                            [ 2.0 kΩ (R2) ]
                                              |
              GND <---------------------------+
```

## Critical Electrical Rules
- **NEVER connect HC-SR04 ECHO directly to ESP32 GPIO 18**: The sensor runs at 5V and outputs 5V TTL logic. Direct connection can degrade or permanently destroy the ESP32 input protection diodes and silicon gates.
- **Supply Voltage**: The classic HC-SR04 requires 5.0V on VCC. Supplying it from 3.3V results in failed ultrasonic burst generation or zero echo pulses (unless using an HC-SR04P or RCWL-9610 wide-voltage version).

## Expected Output
```text
================================================================================
  ULTRASONIC MEASUREMENT CYCLE
================================================================================
CYCLE NUMBER      : #1
TIMESTAMP         : 3000 ms
  [SAMPLE 1] VALID: 45.20 cm (Echo duration: 2635 us)
  [SAMPLE 2] VALID: 45.18 cm (Echo duration: 2634 us)
  [SAMPLE 3] VALID: 45.25 cm (Echo duration: 2638 us)
  [SAMPLE 4] VALID: 45.22 cm (Echo duration: 2636 us)
  [SAMPLE 5] VALID: 45.20 cm (Echo duration: 2635 us)

VALID READINGS    : 5/5
ECHO TIMEOUTS     : 0
OUT-OF-RANGE      : 0
ULTRASONIC STATUS : WORKING — Echo response detected.
ULTRASONIC VALUE  : 45.20 cm
RANGE MIN/MAX     : Min = 45.18 cm | Max = 45.25 cm
DISTANCE ACCURACY : NOT YET VERIFIED — compare with a known physical distance.
```

## Failure Modes & Troubleshooting
1. **Status: FAULT — Echo Timeouts: 5/5**:
   - Check VCC with a multimeter. It must measure between 4.8V and 5.2V.
   - Verify common ground continuity between sensor GND and ESP32 GND.
   - Swap TRIG and ECHO wiring (a common breadboard mistake).
   - Ensure the target surface is perpendicular and acoustic-reflective (not soft foam, angled surfaces, or fabrics).
2. **Readings are 0 cm or Out of Range (< 2 cm)**:
   - Target is too close (blind zone is < 2 cm). Move target to 20–50 cm.
3. **Fluctuating / Erratic Measurements**:
   - Reflections from nearby objects or table edges in the 15° acoustic cone.
   - Poor ground connection or noise from unshielded wires.
