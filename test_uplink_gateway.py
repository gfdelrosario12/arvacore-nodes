import urllib.request
import json
import time

def simulate_node_telemetry(gateway_url="http://localhost:8080/api/v1/telemetry/uplink/NODE-01", 
                            gateway_key="arvacore-gateway-sec-key-2026"):
    payload = {
        "nodeId": "NODE-01",
        "seq": 101,
        "water_level_distance": 25.5,
        "waterLevelCm": -5.5,
        "soilMoisturePercent": 42.0,
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

