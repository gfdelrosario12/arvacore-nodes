# Test 06 — Green Status / Pairing LED Independent Test

## Purpose
Independently test the electrical driving capability and visual functionality of the **Green Status/Pairing LED** on GPIO 25. The test cycles through four distinct operational states (Solid OFF, Solid ON, Slow Blink, Fast Blink) to confirm circuit continuity and resistor dimensioning.

## Hardware Components Required
- ESP32-WROOM-32 development board
- 1x Standard 3mm or 5mm Green LED (typical Forward Voltage $V_f \approx 2.0\text{V} - 2.2\text{V}$)
- 1x Current-limiting resistor: **220 Ω to 330 Ω**
- Breadboard and jumper wires

## Exact Wiring Schematic
```text
  [ESP32 DEV MODULE]
       GPIO 25 ---------------------> [ Resistor: 220 Ω - 330 Ω ]
                                                   |
                                            (Anode / Long Leg +)
                                                  [>|] GREEN LED
                                            (Cathode / Short Leg - flat side)
                                                   |
           GND <-----------------------------------+
```

## Resistor Calculation & Current Limits
$$I = \frac{V_{GPIO} - V_{f}}{R} = \frac{3.3\text{V} - 2.1\text{V}}{220\,\Omega} = \frac{1.2\text{V}}{220\,\Omega} \approx 5.45\text{ mA}$$
- The ESP32 GPIO pad can safely supply up to 12 mA (20 mA absolute max). A 220–330 Ω resistor guarantees bright visibility while keeping pad dissipation well within safe thermal specifications.
- **NEVER connect an LED directly to a GPIO without a series resistor.** Doing so will cause excessive current draw, damaging the ESP32 pin driver.

## Expected Output
```text
================================================================================
  COMMAND: SOLID ON
================================================================================
GPIO 25 STATE     : HIGH (3.3V)
EXPECTED VISUAL   : Green LED illuminated steadily at continuous brightness.

================================================================================
  COMMAND: SLOW BLINK (500 ms ON / 500 ms OFF)
================================================================================
GPIO 25 BEHAVIOR  : Alternating 0V / 3.3V at 1 Hz
EXPECTED VISUAL   : Green LED pulsing at 1 cycle per second (pairing wait emulation).
```

## Failure Diagnosis & Troubleshooting
1. **Serial Monitor shows commands, but LED never lights up**:
   - Cause 1: Reversed LED polarity. The longer lead (Anode) must face GPIO 25; the flat edge (Cathode) must connect to GND.
   - Cause 2: Missing or broken series resistor.
   - Cause 3: Burnt-out LED. Test LED using the 3.3V pin and resistor directly.
2. **LED is extremely dim**:
   - Cause: Resistor value too high (e.g., 10 kΩ instead of 220 Ω or 330 Ω). Verify color bands or measure with DMM.
3. **ESP32 resets or reboots when LED turns ON**:
   - Cause: LED connected without a resistor, causing a dead short from 3.3V to GND, triggering internal brownout detection.

