# Test 04 — Activation Button Independent Test

## Purpose
Verify the electrical integrity, internal pull-up function, contact bounce behavior, and transition detection of the **Activation Button** on GPIO 19. The test strictly isolates the switch mechanism and avoids executing any application logic (no pump activation, no audit, no telemetry).

## Hardware Components Required
- ESP32-WROOM-32 development board
- Momentary push button (tactile switch, 6x6 mm or panel mount)
- 2 jumper wires

## Exact Wiring Schematic
```text
  [ESP32 DEV MODULE]                  [TACTILE PUSH BUTTON]
       GPIO 19 -----------------------> Terminal A1 / B1
           GND -----------------------> Terminal A2 / B2 (Opposing diagonal or switched contact)
```

## Internal Pull-up Theory & Mechanics
- Software configures `pinMode(19, INPUT_PULLUP)`.
- The ESP32 connects an internal pull-up resistor (~45 kΩ) between the GPIO pad and the internal 3.3V rail.
- **When button is open (released)**: Minimal current flows; pin voltage remains at 3.3V (`HIGH`).
- **When button is closed (pressed)**: Current flows from the pull-up through the switch contacts to GND; pin voltage drops to 0.0V (`LOW`).

## Expected Output
```text
================================================================================
  BUTTON EVENT DETECTED
================================================================================
TIMESTAMP         : 4210 ms
RAW GPIO STATE    : LOW (0V)
ACTIVATION BUTTON : PRESSED
BUTTON EVENT      : PRESS DETECTED (Total presses: 1)

================================================================================
  BUTTON EVENT DETECTED
================================================================================
TIMESTAMP         : 4460 ms
RAW GPIO STATE    : HIGH (3.3V)
ACTIVATION BUTTON : RELEASED
BUTTON EVENT      : RELEASE DETECTED (Total releases: 1)
```

## Failure Diagnosis & Troubleshooting
1. **Initial State is Always LOW / Constantly "HELD"**:
   - Cause: Button terminals are inserted rotated 90 degrees on a 4-pin breadboard switch (shorting the pin to GND permanently), or a physical solder bridge exists.
   - Fix: Use a multimeter to measure continuity across the terminals when not pressed. Rotate button 90 degrees or connect to opposite diagonal pins.
2. **Pressing Button Produces No Output**:
   - Cause: Cold solder joint, loose breadboard contact, broken GND wire, or button wired to adjacent GPIO pin.
   - Fix: Measure voltage at GPIO 19 with a DMM: it must read 3.3V when open and drop to 0.0V when pressed.
3. **Multiple Press Events Counted on a Single Physical Tap**:
   - Cause: Excessive contact bounce (> 50 ms) typical of worn mechanical switches.
   - Fix: Increase `DEBOUNCE_DELAY_MS` to 80 or 100 ms in code or add a 100 nF capacitor across the button terminals.

