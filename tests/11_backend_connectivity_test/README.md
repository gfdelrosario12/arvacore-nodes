# Test 11 — ArvaCore Backend Connectivity and HTTPS/TLS Integration Test

## Purpose
Validate complete network stack integration between the ESP32 and the real ArvaCore Cloud API (`https://api-aquaflow.gladwindr.me/api/v1`). The test isolates:
1. DNS host address resolution via `WiFi.hostByName()`.
2. TLS 1.2 / 1.3 cryptographic handshake negotiation.
3. Custom header authentication: `X-ArvaCore-Gateway-Key`.
4. Endpoint interaction on the real status route: `GET /pairing/status/{nodeId}`.

## Real ArvaCore API Endpoints Reference
Derived from firmware (`sketch_oct11a.ino`) and gateway testing script (`test_uplink_gateway.py`):
- **Base URL**: `https://api-aquaflow.gladwindr.me/api/v1`
- **Pairing Status**: `GET /pairing/status/{nodeId}`
- **Pairing Request**: `POST /pairing/request`
- **Telemetry Uplink**: `POST /telemetry/uplink/{nodeId}`
- **Gateway Secret Header**: `X-ArvaCore-Gateway-Key: ed630e68...`

## Security Guidelines
- The sketch checks header delivery without exposing full credentials in application logs.
- `client.setInsecure()` is used during diagnostic hardware validation to prevent certificate expiration issues from stalling physical sensor validation. In hardened production environments, a root CA thumbprint or certificate bundle can be supplied.

## Expected Output
```text
================================================================================
  STEP 1: DNS RESOLUTION AUDIT
================================================================================
Resolving target hostname: api-aquaflow.gladwindr.me
DNS STATUS        : RESOLVED SUCCESSFULLY
RESOLVED IP       : 172.67.182.XX
RESOLUTION TIME   : 48 ms

================================================================================
  STEP 2: HTTPS / TLS HANDSHAKE AND API QUERY
================================================================================
TARGET ENDPOINT   : https://api-aquaflow.gladwindr.me/api/v1/pairing/status/NODE-A1B2
NODE ID TESTED    : NODE-A1B2
AUTH HEADER       : X-ArvaCore-Gateway-Key: [CONFIGURED / PROTECTED]
Sending HTTP GET request over TLS...
ROUNDTRIP TIME    : 412 ms
HTTP STATUS CODE  : 200
RESPONSE BODY     : {"nodeId":"NODE-A1B2","pairingStatus":"APPROVED"}
DIAGNOSTIC RESULT : SUCCESS — Backend is reachable and responded with HTTP 200 OK.
```

## Failure Diagnosis & Error Taxonomy
| Observed Symptom | Root Cause | Engineering Solution |
|---|---|---|
| **DNS FAILED** | Local Wi-Fi router cannot forward external DNS requests | Verify Internet connectivity on router; set static DNS (`WiFi.config(..., IPAddress(8,8,8,8))`) |
| **HTTP status -1 (Connection refused)** | Firewall blocking port 443 outbound or server offline | Verify server status using browser / `curl -I https://api-aquaflow.gladwindr.me` |
| **HTTP status -11 (Timeout)** | TLS handshake took longer than `HTTP_TIMEOUT_MS` (10s) | High packet loss or weak RSSI; verify Test 10 |
| **HTTP 401 / 403 Forbidden** | `GATEWAY_KEY` mismatch or expired header token | Verify key string against ArvaCore backend environment configuration |
| **HTTP 404 Not Found** | Node ID not registered in database | Expected for brand-new nodes; run pairing request via Test 14 |

