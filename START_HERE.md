# PRIVATE — configured environmental monitor

This package contains a real dedicated device credential and the Wi-Fi settings from your edited local firmware. Share only with the authorized teammate through a private channel. Do not upload this package/DeviceSecrets.h to Git, a public drive or chat.

The preview backend now has Environmental Monitor registered under stephmkimnj@gmail.com. Its matching device ID and environmental credential are already filled in in SkyMindEnvironment/DeviceSecrets.h. Cloud publishing is enabled. No sample readings were sent during provisioning.

The Wi-Fi values were preserved from the user-edited Config.h. Confirm they are the network available to the board.

Teammate steps:
1. Extract this entire folder. Keep DeviceSecrets.h alongside every sketch source file.
2. Open SkyMindEnvironment/SkyMindEnvironment.ino in Arduino IDE.
3. Use ESP32 Dev Module, core 3.3.8 (the tested core), OneWire 2.3.8. Compile first.
4. When performing the authorized board upgrade, upload normally using the current partition scheme. Do not erase flash or reset/recommission the sensors.
5. Check all six sensors locally and watch USB Serial Monitor at 115200 for cloud errors. If you see HTTP 401/403, stop and report the status; do not share the private token.
6. The owner signs into https://environment-dashboard.skymindapp.pages.dev/xaerova/environment using stephmkimnj@gmail.com, selects Environmental Monitor, and checks actual readings. Teammate can verify local sensors/upload errors without the owner’s login.
7. Check disconnect/reconnect and power-cycle/settings retention.

Live readings only appear after the actual ESP32 is running this firmware, connected to the correct Wi-Fi/internet, synchronized to UTC and sending successful HTTPS requests. Provisioning alone does not produce readings. The sample preview page is not used for this verification.

The sensor drivers/wiring/NVS format/UART fix were retained. This is a software-compiled candidate; physical TLS/time/sensor/retention validation remains the board-side test. Read START_HERE.md and VALIDATION.md for exact software checks/limits; their generic instruction to supply ID/token has already been completed in this private package. The original public ZIP remains credential-free.
