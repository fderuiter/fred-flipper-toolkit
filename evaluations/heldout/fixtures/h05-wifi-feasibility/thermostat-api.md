# Nimbus T3 local HTTP API (synthetic)

- The thermostat joins the home Wi-Fi (2.4 GHz, WPA2) and announces itself over mDNS as `nimbus-t3-xxxx.local`.
- All requests go to `https://<host>:8443/api/v1/` over TLS with a self-signed certificate that the app is told to pin.
- Every request needs header `Authorization: Bearer <local token>`. The token is shown once in the thermostat's menu and should be treated as a password.
- `GET /api/v1/status` → `{"room_c": 21.4, "setpoint_c": 20.0, "mode": "heat"}`
- `PUT /api/v1/setpoint` with body `{"setpoint_c": <5.0–30.0>}` → `204`. Values outside 5–30 °C are rejected by the thermostat.
- The thermostat keeps its own frost protection (minimum 5 °C) and over-temperature cut-out.
