import urllib.request
import json
import time

def simulate_node_telemetry(gateway_url="https://api-aquaflow.gladwindr.me/api/v1/telemetry/uplink/NODE-01", 
                            gateway_key="ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9"):
    payload = {
        "nodeId": "NODE-01",
        "seq": 101,
        "measurements": {
            "soil_moisture": 42.0,
            "water_level_distance": 25.5
        },
        "soilMoisturePercent": 42.0,
        "water_level_distance": 25.5,
        "waterLevelCm": -5.5,
        "batteryVoltage": 4.12
    }
    
    headers = {
        "Content-Type": "application/json",
        "X-ArvaCore-Gateway-Key": gateway_key
    }
    
    data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(gateway_url, data=data, headers=headers, method="POST")
    
    try:
        with urllib.request.urlopen(req) as response:
            status = response.status
            body = response.read().decode('utf-8')
            print(f"[SUCCESS] Gateway HTTP {status}: {body}")
            return True
    except Exception as e:
        print(f"[FAILURE] Gateway communication failed: {e}")
        return False

if __name__ == "__main__":
    simulate_node_telemetry()
