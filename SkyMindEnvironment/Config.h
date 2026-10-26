#pragma once
// Private local credentials live in DeviceSecrets.h (copy the example file).
#if __has_include("DeviceSecrets.h")
#include "DeviceSecrets.h"
#endif
#ifndef SKYMIND_WIFI_SSID
#define SKYMIND_WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef SKYMIND_WIFI_PASSWORD
#define SKYMIND_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
constexpr char WIFI_SSID[]=SKYMIND_WIFI_SSID;
constexpr char WIFI_PASSWORD[]=SKYMIND_WIFI_PASSWORD;
// A fallback hotspot starts after 25 seconds without the ZTE network.
// Its unique password is printed in the USB Serial Monitor (115200 baud).
constexpr char MDNS_NAME[] = "skymind-env";

// CLASSIC ESP32-WROOM DevKit only. GPIO numbers, not header positions.
constexpr int OUTDOOR_RX=16, OUTDOOR_TX=17, OUTDOOR_DIR=25;
constexpr int INDOOR_RX=32, INDOOR_TX=33, INDOOR_DIR=19;
constexpr int TEMPERATURE_PIN=4; // User's already-working D4 probe and pull-up.
constexpr int STATUS_LED_PIN=2; // Onboard blue LED, as in the earlier sketch.
constexpr bool STATUS_LED_ACTIVE_HIGH=true;
constexpr uint32_t LED_SLOW_HALF_PERIOD_MS=1000, LED_FAST_HALF_PERIOD_MS=150;
constexpr uint32_t WIFI_ATTEMPT_MS=12000, WIFI_RETRY_WAIT_MS=8000;
constexpr uint32_t POLL_MS=2000, STALE_MS=10000, REPLY_TIMEOUT_MS=500;
// Standalone environmental monitor: disconnect existing motor/relay/servo
// control wiring before using this pin map; 25/32/33 had other roles previously.
