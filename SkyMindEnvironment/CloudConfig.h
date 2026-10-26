#pragma once
#include <stdint.h>
#if __has_include("DeviceSecrets.h")
#include "DeviceSecrets.h"
#endif
#ifndef SKYMIND_ENABLE_CLOUD
#define SKYMIND_ENABLE_CLOUD 0
#endif
#ifndef SKYMIND_CLOUD_DEVICE_ID
#define SKYMIND_CLOUD_DEVICE_ID "REPLACE_WITH_DOCK_DEVICE_ID"
#endif
#ifndef SKYMIND_CLOUD_TOKEN
#define SKYMIND_CLOUD_TOKEN "REPLACE_WITH_LONG_RANDOM_DEVICE_SECRET"
#endif
#define CLOUD_TELEMETRY_ENABLED SKYMIND_ENABLE_CLOUD
constexpr char CLOUD_DEVICE_ID[]=SKYMIND_CLOUD_DEVICE_ID;
constexpr char CLOUD_TELEMETRY_TOKEN[]=SKYMIND_CLOUD_TOKEN;
constexpr char CLOUD_TELEMETRY_URL[]="https://environment-dashboard.skymindapp.pages.dev/api/xaerova/environment/ingest";
constexpr uint32_t CLOUD_TELEMETRY_INTERVAL_MS=2000;
// Default: Espressif's embedded trusted root CA bundle, with hostname/date checks.
// Optional override: a trusted PEM CA; leading/trailing whitespace is normalized.
// No insecure transport fallback. Unsupported cores without a CA bundle fail closed.
constexpr char CLOUD_TLS_CA_CERT[]="";
constexpr char CLOUD_NTP_SERVER_1[]="time.cloudflare.com";
constexpr char CLOUD_NTP_SERVER_2[]="pool.ntp.org";
constexpr char CLOUD_NTP_SERVER_3[]="time.google.com";
