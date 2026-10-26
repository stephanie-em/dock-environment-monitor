# EnvironmentMonitorTesting — 3.0.3-preview.2

Open `EnvironmentMonitorTesting/EnvironmentMonitorTesting.ino` in Arduino IDE. Select ESP32 Dev Module, Espressif core 3.3.8 and OneWire 2.3.8. Open Serial Monitor at **115200 baud** and reset the board to see startup logs.

Serial milestones:
- `[wifi] Connected; IP=..., RSSI=...` confirms connection to the configured Wi-Fi.
- `[cloud] UTC synchronized; ready for verified HTTPS telemetry` confirms clock readiness.
- `[cloud] Website accepted telemetry: HTTP 200 (HTTPS verified)` confirms the website ingestion endpoint accepted a snapshot. Success logs repeat once per minute and on recovery.
- Wi-Fi retries/loss, UTC retries, HTTPS errors and rejected HTTP status codes are logged separately. Check the signed-in website dashboard to confirm the expected device readings appear.

This checkout has no `DeviceSecrets.h`; cloud publishing is disabled by default. Configure Wi-Fi and an already-provisioned device credential privately using `DeviceSecrets.example.h` only when preparing an authorized board test. Do not commit that private file. No upload or live device verification has been performed here.

The local hostname is `environmentmonitortesting.local`; fallback Wi-Fi is named `EnvironmentMonitorTesting-<device suffix>`. Persisted sensor settings and backend device identity are preserved.

For an authorized hardware upgrade, use the current partition scheme and upload normally without erasing flash or recommissioning sensors. Check all six local sensor readings, website readings, disconnect/reconnect behavior and power-cycle/settings retention. If the serial log reports HTTP 401/403, publishing stops; check provisioning privately.

See `CHANGELOG.md`, `VALIDATION.md` and `BACKEND_CONTRACT.md` for history and test limits.
