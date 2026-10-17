# Test 07 — Blue Activation / Data LED Independent Test

## Purpose
Independently verify the electrical driving capability and visual functionality of the **Blue Activation/Data LED** on GPIO 26. In the ArvaNode system, this LED indicates active sensor monitoring (Solid ON) and active telemetry transmission uplink (rapid 100 ms continuous blinking). This test isolates the LED without triggering Wi-Fi, sensors, or HTTP uplinks.

## Hardware Components Required
- ESP32-WROOM-32 development board
- 1x Standard 3mm or 5mm Blue LED (typical Forward Voltage $V_f \approx 2.9\text{V} - 3.2\text{V}$)
- 1x Current-limiting resistor: **150 Ω to 220 Ω**
- Breadboard and jumper wires

## Exact Wiring Schematic
```text
  [ESP32 DEV MODULE]
       GPIO 26 ---------------------> [ Resistor: 150 Ω - 220 Ω ]
                                                   |
                                            (Anode / Long Leg +)
                                                  [>|] BLUE LED
                                            (Cathode / Short Leg - flat side)
                                                   |
           GND <-----------------------------------+
```

## Critical Electrical Insight for Blue LEDs
- Blue InGaN LEDs exhibit a higher forward threshold voltage ($V_f \approx 3.0\text{V}$) than red or green LEDs ($V_f \approx 2.0\text{V}$).
- When driven from a 3.3V GPIO through a 220 Ω resistor:
  $$I = \frac{3.3\text{V} - 3.0\text{V}}{220\,\Omega} = \frac{0.3\text{V}}{220\,\Omega} \approx 1.36\text{ mA}$$
- If higher brightness is desired under field sunlight conditions, a 150 Ω resistor will yield $\approx 2.0\text{ mA}$, which remains safe for the GPIO pad while increasing lumen output.

## Expected Output
```text
================================================================================
  COMMAND: SOLID ON (Monitoring Active Emulation)
================================================================================
GPIO 26 STATE     : HIGH (3.3V)
EXPECTED VISUAL   : Blue LED illuminated steadily at continuous brightness.

================================================================================
  COMMAND: TELEMETRY TRANSMISSION BLINK (100 ms ON / 100 ms OFF)
================================================================================
GPIO 26 BEHAVIOR  : Alternating 0V / 3.3V at 5 Hz
EXPECTED VISUAL   : Blue LED flickering rapidly (emulates active HTTP transmission).
```

## Failure Diagnosis & Troubleshooting
1. **Blue LED Does Not Light Up (Even Though Green LED Works on Same Resistor)**:
   - Cause: Higher forward voltage requirement ($V_f$). If using a 1 kΩ or 4.7 kΩ resistor, the voltage drop leaves insufficient current to forward-bias the blue semiconductor junction.
   - Fix: Use a 150 Ω or 220 Ω resistor.
2. **Reversed Polarity**:
   - Fix: Verify that the longer wire lead (Anode) connects to GPIO 26 through the resistor, and the shorter lead with the flat notch on the plastic collar connects to GND.
3. **GPIO 26 Pad Damage**:
   - Fix: Probe GPIO 26 directly with a multimeter in DC Volts mode. It must cycle between 0.0V and 3.3V.

