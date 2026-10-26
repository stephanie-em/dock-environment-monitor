#pragma once
#include <Arduino.h>
#include "CloudProtocol.h"
using CaptureTelemetry=void (*)(CloudProtocol::Sensor (&samples)[6],bool& paused,uint64_t nowMs);
uint64_t telemetryUptimeMs();
void telemetryBegin();
void telemetryTick(CaptureTelemetry capture);
