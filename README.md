# ArvaNode: Physical IoT Field Monitoring Node Firmware

[![Board](https://img.shields.io/badge/Board-ESP32--WROOM--32-E7352C?logo=espressif)](https://www.espressif.com)
[![Sensors](https://img.shields.io/badge/Sensors-JSN--SR04T%20%7C%20Capacitive-00979D)](https://arduino.cc)
[![Protocol](https://img.shields.io/badge/Protocol-Wi--Fi%20%2F%20HTTP%20REST-blue)](https://espressif.com)

> Firmware and hardware specifications for the distributed sensor monitoring nodes deployed across rice paddy quadrants in the **ArvaCore** ecosystem.

---

## 1. Node Purpose & Hardware Architecture

The **ArvaNode** is an independent, battery-powered IoT field node responsible for real-time acquisition of field water depth and soil moisture under the Alternate Wetting and Drying (AWD) protocol.

### Microcontroller & Sensors
| Component | Function | Interface / Pin | Notes |
|:---|:---|:---|:---|
| **ESP32-WROOM-32** | Main controller & Wi-Fi uplink | — | 3.3V logic |
| **JSN-SR04T** | Ultrasonic distance measurement | TRIG: `GPIO 5`<br>ECHO: `GPIO 18` | Waterproof sensor mounted atop still-well pipe |
| **Capacitive Soil Moisture v1.2** | Root-zone volumetric water content | Analog In: `GPIO 34` | Corrosion-resistant analog probe |
| **Voltage Divider** | Sensor echo 5V to 3.3V stepping | ECHO pin path | $1\,\text{k}\Omega$ / $2\,\text{k}\Omega$ divider for 3.3V ESP32 pin protection |

---

## 2. Still-Well Installation & Geometry

The water level measurement follows the field still-well configuration detailed in **Thesis Chapter 3**:
- A perforated PVC tube is driven into the soil.
- An internal float rises and falls according to the perched water table.
- The JSN-SR04T sensor head is positioned at the top of the tube, exactly **$20.0\,\text{cm}$ above the soil surface datum**.
- The distance $d$ measured by ultrasonic echo calculates water level $h$:
  $$h = 20.0\,\text{cm} - d$$
  - $d = 20.0\,\text{cm} \implies h = 0.0\,\text{cm}$ (Field soil surface)
  - $d = 35.0\,\text{cm} \implies h = -15.0\,\text{cm}$ (AWD reflood threshold)
  - $d = 15.0\,\text{cm} \implies h = +5.0\,\text{cm}$ (Target ponded flood depth)

---

## 3. Firmware Structure (`sketch_oct11a/sketch_oct11a.ino`)

The firmware contains:
1. **Ultrasonic Sampling (`measureDistance`)**:
   - Generates $10\,\mu\text{s}$ trigger pulse.
   - Measures echo return duration with a $30\,\text{ms}$ timeout.
   - Filters out out-of-spec readings ($<20.0\,\text{cm}$ or $>450.0\,\text{cm}$).
2. **Capacitive Soil Sampling (`measureSoilMoisture`)**:
   - Reads ADC (`GPIO 34`).
   - Calibrates between dry air value (`3200`) and water immersion (`1500`) to output $0.0 - 100.0\%$.
3. **Wi-Fi Connectivity & Authenticated Uplink (`sendTelemetry`)**:
   - Connects to the local gateway Wi-Fi network.
   - Formats compliant JSON payloads with node identification, sequence number, distance, and water level.
   - Transmits HTTP POST to `/api/v1/telemetry/uplink/{nodeId}` with `X-ArvaCore-Gateway-Key`.

---

## 4. Telemetry Payload Format

```json
{
  "nodeId": "NODE-01",
  "seq": 101,
  "water_level_distance": 25.5,
  "waterLevelCm": -5.5,
  "soilMoisturePercent": 42.0,
  "batteryVoltage": 4.15
}
```

---

## 5. Build, Flash & Simulation

### Uploading to ESP32 via Arduino IDE or CLI
1. Install ESP32 board definitions in Arduino IDE (`Tools > Board > ESP32 Dev Module`).
2. Open [`sketch_oct11a/sketch_oct11a.ino`](sketch_oct11a/sketch_oct11a.ino).
3. Set your target gateway IP and Wi-Fi SSID:
   ```cpp
   const char* WIFI_SSID = "ARVACORE_GATEWAY";
   const char* WIFI_PASS = "arvacore2026";
   const char* GATEWAY_URL = "http://192.168.1.100:8080/api/v1/telemetry/uplink/NODE-01";
   const char* GATEWAY_KEY = "arvacore-gateway-sec-key-2026";
   ```
4. Compile and upload to your ESP32 board over USB.

### Testing Without Hardware (Python Uplink Simulator)
Use [`test_uplink_gateway.py`](test_uplink_gateway.py) to simulate an ArvaNode transmitting telemetry to a running backend:
```bash
python3 test_uplink_gateway.py
```
Expected output:
```text
[SUCCESS] Gateway HTTP 200: {"status":"SUCCESS","readingsIngested":3}
```

