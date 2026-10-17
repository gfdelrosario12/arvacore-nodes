# Test 05 — Pairing Button Independent Test

## Purpose
Verify the electrical integrity, internal pull-up function, contact bounce behavior, and transition detection of the **Pairing Button** on GPIO 23. This sketch isolates physical tactile switch function and deliberately avoids Wi-Fi scanning, radio activation, or backend API pairing requests.

## Hardware Components Required
- ESP32-WROOM-32 development board
- Momentary tactile push button (active-low)
- 2 jumper wires

## Exact Wiring Schematic
```text
  [ESP32 DEV MODULE]                  [TACTILE PUSH BUTTON]
       GPIO 23 -----------------------> Terminal A1 / B1
           GND -----------------------> Terminal A2 / B2 (Opposing diagonal or switched contact)
```

## Logic Verification
- Pin configured as `pinMode(23, INPUT_PULLUP)`.
- When the switch is open, the internal ~45 kΩ pull-up holds GPIO 23 at 3.3V (`HIGH`).
- When pressed, the contacts short GPIO 23 directly to Ground (`LOW`).
- Debounce filter ensures false rapid trigger events from contact bounce are rejected.

## Expected Output
```text
================================================================================
  PAIRING BUTTON EVENT DETECTED
================================================================================
TIMESTAMP         : 6120 ms
RAW GPIO STATE    : LOW (0V)
PAIRING BUTTON    : PRESSED
BUTTON EVENT      : PRESS DETECTED (Total presses: 1)
ISOLATION CHECK   : Hardware switch closed. No network packet generated.

================================================================================
  PAIRING BUTTON EVENT DETECTED
================================================================================
TIMESTAMP         : 6310 ms
RAW GPIO STATE    : HIGH (3.3V)
PAIRING BUTTON    : RELEASED
BUTTON EVENT      : RELEASE DETECTED (Total releases: 1)
```

## Failure Diagnosis & Troubleshooting
1. **Button Always Reads LOW on Startup**:
   - Check if button is rotated 90 degrees in breadboard holes, shorting the internal bus bar to GND.
   - Inspect solder joints on custom PCB/perfboard for solder bridges between GPIO 23 and the ground plane.
2. **Pressing Button Has No Effect**:
   - Check continuity between the switch pin and ESP32 GND with a multimeter.
   - Verify jumper is in GPIO 23 and not GPIO 22 (adjacent pin).
3. **Ghost Press Events When Hands Move Near Wires**:
   - Indicates internal pull-up may be disabled or damaged. Add an external 10 kΩ pull-up resistor between GPIO 23 and 3.3V.
