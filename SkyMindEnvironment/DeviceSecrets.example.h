#pragma once
// Copy to DeviceSecrets.h and edit that PRIVATE file only. Never commit/share it publicly.
#define SKYMIND_WIFI_SSID "YOUR_WIFI_SSID"
#define SKYMIND_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
// Must match an EXISTING, verified, active preview-owned device; do not invent an ID.
#define SKYMIND_CLOUD_DEVICE_ID "REPLACE_WITH_DOCK_DEVICE_ID"
// Dedicated environmental credential, xenv_ + 64 lowercase hex; provision hash first.
#define SKYMIND_CLOUD_TOKEN "REPLACE_WITH_LONG_RANDOM_DEVICE_SECRET"
// Change to 1 only after the ID/credential are provisioned and hardware testing is authorized.
#define SKYMIND_ENABLE_CLOUD 0
