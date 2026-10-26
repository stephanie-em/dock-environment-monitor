# EnvironmentMonitorTesting — 3.0.3-preview.2 cloud validation

The renamed Arduino sketch compiled successfully using Arduino CLI 1.4.1, Espressif core 3.3.8, OneWire 2.3.8 and FQBN `esp32:esp32:esp32` with default partition settings. Default publishing-disabled build: 1,033,428 bytes flash / 50,664 bytes static RAM. Cloud-enabled compile-only build using synthetic, unprovisioned credentials: 1,233,978 bytes flash / 51,644 bytes static RAM; fits the default 1,310,720-byte app partition. Neither build was uploaded.

Protocol, commissioning and transport regressions passed with AddressSanitizer/UndefinedBehaviorSanitizer. All six cloud encoder modes passed with JSON parsing using a diagnostic build retaining the existing GCC 14 misleading-indentation warning. The strict cloud encoder build still fails under `-Werror` at `CloudProtocol.h:54`; no warning fix was included in this change.

Serial logs for Wi-Fi, UTC readiness and HTTPS acceptance/failures are compiled; physical Wi-Fi, website ingestion, serial output timing and sensor behavior have not been verified on a board. No firmware was uploaded and no live telemetry was sent. The separate website contract suite and missing dashboard-preview fixture test were not run.

---

The following describes the earlier preview.1 validation, not a new execution of those checks.

# Corrected firmware software validation — October 26, 2026

PASS: Arduino IDE bundled arduino-cli 1.4.1 compiled for esp32:esp32:esp32 using installed Espressif core 3.3.8 and OneWire 2.3.8, with default partition scheme. Publishing-disabled build: 1,032,800 bytes flash / 50,664 bytes static RAM before final counter-type guard; full enabled build with synthetic, unprovisioned test credentials after final guard: 1,233,014 bytes flash / 51,644 bytes static RAM. The complete enabled network/TLS/NVS code was linked and fits the 1,310,720-byte app partition. Synthetic private configuration was removed before packaging. No binaries are distributed.

PASS: actual portable firmware JSON encoder compiled with -Wall -Wextra -Werror and exercised for six cases: all zeros, the saved diagnostic fixture, binary one, paused, invalid/stale, and 64-bit uptime past millis rollover. Each emitted JSON was accepted with HTTP 200 by the real website ingestion handler using isolated SQLite test ownership/credential rows. Duplicate snapshots returned 409 without refreshing receipt; delayed/future capture times returned 422. Website's actual validator/freshness functions verified conversion preservation, nulls/paused temperature and offline behavior. There were no calls to a live backend/device and no actual account/device rows created remotely.

PASS: original protocol, commissioning and UART transport C++ tests compiled with AddressSanitizer/UndefinedBehaviorSanitizer (LeakSanitizer disabled) and passed. Protocol.h, Commission.h, RS485Transport.h, Dashboard.h, App.h and .ino are byte-identical to the supplied candidate. Settings structure and existing setup/NVS loading are unchanged; only telemetry hooks, runtime-only sample timestamps and version reporting changed in App.cpp. Hardware Config.h lines were retained; credentials moved into a private optional header.

Portable cloud encoder/policy checks cover UUID shape, credential/ID syntax, clock-not-synced/too-old/invalid time, bounded pending age and capped retry backoff, sample staleness, NaN/range/binary rejection and bounded JSON buffer.

Reproduce from the website checkout (Node 22.13+, installed npm dependencies and C++ compiler):

    node /path/to/this/package/tests/cloud_contract_test.cjs /path/to/skymind

Original regressions:

    c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/protocol_test.cpp -o /tmp/skymind-protocol
    ASAN_OPTIONS=detect_leaks=0 /tmp/skymind-protocol

Repeat for commission_test.cpp and transport_test.cpp.

Limits: exact original core 3.3.5 was not installed/tested. No real ESP32/sensors attached, no flash/upload, no NVS retention/power-cycle, no actual TLS/DNS/NTP/UTC accuracy/heap/timing or cloud disconnect/reconnect test. The core's trusted CA bundle is configured and linked; a real endpoint handshake is not proven by compilation. SDK/DNS calls may overrun a cooperative timing budget; sensor polling stays separate and backend rejects delayed captures. This is a software-validated candidate requiring verified provisioning and bench checks, not a completed live integration. Production and original Downloads source remain unchanged.
