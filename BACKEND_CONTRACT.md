# Environmental dashboard and firmware integration contract

Stage: website/backend only, branch `feature/environment-dashboard`. No firmware changed or flashed, no production deployment, no production database changes. Baseline ESP32 firmware **3.0.1 only serves local HTTP** and cannot populate this dashboard yet.

## Architecture and review

The new signed-in page is `/xaerova/environment`, using the existing React router, Xaerova account shell, SectionCard styling, and EN/FR translation system. It has six read-only cards. No configuration, commissioning, polling, or actuator controls were added. The browser only reads the same-origin cloud API; it never contacts the ESP32.

Backend: existing Cloudflare Pages Functions and D1 `DB` binding. User authentication uses the existing `authenticateXaerovaUser` Clerk JWT verifier and existing `xaerova_users`. Device ownership remains **only** `xaerova_devices.user_id`; no new account system or ownership assignment is performed. Environmental credentials/snapshots are stored in `xaerova_environment` by migration `0023_environment_telemetry.sql` (review/apply only locally or in separately authorized staging).

Every readings request joins active device ownership to the verified Clerk subject. `userId` query parameters are ignored; another account's or nonexistent device returns 404. Responses are `Cache-Control: private, no-store` and vary on Authorization/Cookie. The client clears data on sign-out/account change, ignores obsolete requests, and displays an error instead of treating an API failure as an unconnected device.

Inspection found the existing device listing/rename/delete/pairing-token endpoints trusted caller-supplied identifiers. They now require the same Clerk authentication and constrain mutations to its owner. The website callers now send Clerk bearer tokens. Pairing still uses the existing five-minute pairing-token flow; it rejects re-pairing an existing device owned by another account. A deliberate ownership transfer requires a separate operator workflow. Existing dock, session, flight, booking, and actuator code is unchanged. External clients using the previously unauthenticated device-management endpoints must now send a Clerk session; device heartbeat/control credentials retain their existing behavior.

Environmental provisioning records the verified owner at provisioning time as an additional security boundary, not as a new ownership source. Transfer, deactivation or revocation blocks ingestion and hides old snapshots. A new owner must explicitly provision a new environmental credential; they never inherit the prior owner's readings.

Hosted preview is now available following separate user authorization; see [hosted preview and verification](environment-hosted-preview.md). Production and firmware remain unchanged.

## Reviewable local preview

Dependencies are already present in this checkout. If absent, install with the existing lockfile using `npm ci`. Use Node **22.13+** for tests (`node:sqlite`).

```sh
npm run dev -- --host 127.0.0.1
```

Open **http://127.0.0.1:5173/preview/environment.html**. This standalone development entry needs no login and makes no API/device requests. It renders the same dashboard component/account styling. It defaults to **Awaiting device connection**. Its persistent banner clearly labels the saved diagnostics and simulated timeline. The preview selector covers fixture values, invalid, stale, offline, and paused states. EN/FR is available. This is a view-state selector only; it cannot control hardware.

`scripts/fixtures/environment-diagnostics.json` contains only the six readings, firmware, uptime, and paused state from the supplied October 26, 2026 export, with an explicit fixture notice. LAN, raw transactions, and hardware settings were omitted. The normal application does not import this fixture or the preview entry. `npm run build` only includes the normal `index.html` entry, so `/preview/environment.html` is not published in its output.

Browser verification with an installed Chrome on macOS:

```sh
node scripts/check-environment-preview.cjs
```

Optional `CHROME_BIN` points to another installed Chromium executable. Screenshots are written to ignored `artifacts/environment/`. The script checks actual React rendering, all preview states, EN/FR, desktop/mobile overflow, runtime exceptions, and absence of API/device/non-local requests. The preview server can be stopped with Ctrl-C after review.

For the **real authenticated page**, configure a local Pages Functions backend, use Clerk development keys, and a local DB copy with deliberately verified existing account/device relationships. Vite's existing `/api` proxy points to port 8080; the Express server in `server/` is only a notification server and does not implement these APIs. Run Pages locally on 8080 (not the notification server), serving a local build, then run Vite on 5173. For example, with a locally reviewed Pages config/local D1 database:

```sh
npm run build
npx wrangler pages dev dist --port 8080
```

Wrangler Pages dev is local by default; do not enable remote bindings. The repository's normal config names production resources and a separate existing preview environment: **do not deploy or run remote migration commands as part of this stage**. Do not copy production secrets/data for a preview. A backend integration test harness is included and uses isolated in-memory SQLite, real signed test JWTs, and stubbed JWKS, so no live account/database is needed for automated verification.

## Required environment and secrets

| Name / binding | Purpose | Where it belongs |
| --- | --- | --- |
| `VITE_CLERK_PUBLISHABLE_KEY` | Existing Clerk frontend instance; use development instance locally | Frontend `.env.local` (existing convention) |
| `CLERK_SECRET_KEY` | Existing server Clerk JWT/JWKS authentication | Local ignored `.dev.vars`; secret store for a future authorized release |
| `CLERK_AUTHORIZED_PARTIES` | Existing optional comma-separated allowed frontend origins; include the actual local/staging origin | Backend only; recommended for deployed environments |
| `DB` | Existing D1 binding; contains users, devices, and new migration | Local/staging binding while reviewing |
| `JOIN_LINKS` | Existing pairing-token KV binding | Needed for device-pairing flows; unchanged |

No new frontend environment variable, global ingestion password, Wi-Fi secret, or per-device plaintext environment variable is required. The environmental credential is a per-device **SHA-256 hash in D1**, and its plaintext is provisioned only to the later ESP32 publisher. Existing other-feature bindings/secrets remain unchanged.

## Secure provisioning (prepare now; do not modify ESP32 in this stage)

1. Independently verify the existing active `xaerova_devices` row and its `user_id`, and determine which existing dock/device relationship this ESP32 represents. **No mapping can be inferred from the diagnostics**; the export contains no cloud device identifier or account identity. If no appropriate relationship exists, the owner must explicitly pair/register it through the site's existing workflow. Ingestion and provisioning never create an owner or device.
2. Use that exact `device_id` as telemetry `deviceId` (restricted to 1–128 ASCII letters/digits/`_.:-`). Do not use the local IP, Modbus ID, session ID, or a fabricated cloud identity. If an existing identifier is incompatible, resolve the contract before the firmware update.
3. Generate 32 cryptographically random bytes in a trusted password manager and encode as 64 lowercase hex characters, prefixed `xenv_`. Use a unique credential for every environmental publisher. Never reuse the existing `xdv_` Android/control token, local mutation token, Clerk token, or Wi-Fi password.
4. Keep the plaintext in the password manager. Generate hash-only SQL using the included script. Identifiers are nonsecret arguments; **the secret is entered in a hidden prompt or piped from the password manager via stdin**, never shell arguments/history. Example:

   ```sh
   node scripts/provision-environment.cjs EXISTING_DEVICE_ID VERIFIED_CLERK_USER_ID /private/tmp/environment.local.sql
   ```

   The script makes no network/database calls, refuses to overwrite an existing output file, writes mode 0600, and never logs the secret. The generated `.local.sql` is ignored by Git. Inspect it before use.
5. On an explicitly selected **local/staging** database with the existing schema, apply `0023_environment_telemetry.sql`, then the reviewed SQL. The SQL uses `INSERT … SELECT` from the existing active device and verified owner, so unknown/wrong-owner devices produce no provisioning row. Require `provisioned_rows = 1`. No production database commands have been executed for this task.
6. For the later firmware work only: transfer the plaintext credential and HTTPS ingestion URL by a trusted local provisioning channel. Keep cloud settings/boot counter in a **separate Preferences/NVS namespace**, e.g. `skymind-cloud`. Do not add secrets to tracked `Config.h`, frontend bundles, URLs, query strings, diagnostic exports, screenshots or serial logs. Do not add a customer-facing provisioning/control endpoint in this stage.
7. Rotation: stop the old publisher, generate a new secret, and rerun provisioning for the verified existing owner/device. This atomically replaces the hash and clears the latest snapshot/order state; old credentials fail immediately and the page returns to awaiting until a new accepted snapshot arrives. Protect hash-only provisioning files and delete them after use. Revocation: an operator can set `credential_hash = NULL` for that environmental device; it blocks ingestion and hides the snapshot. Device deactivation also blocks both. These operations require the normal separately authorized database procedure.

All deployed traffic must use **HTTPS with server certificate and hostname validation**. The ESP32 must have a trusted CA and a synchronized clock; never use `setInsecure()`. The backend rejects plain HTTP except literal localhost/loopback for local development; it does not trust a caller's forwarded-protocol header. Do not expose/port-forward the ESP32's unauthenticated local HTTP service. Redact Authorization headers and bodies in any future edge/proxy tracing configuration; no new endpoint logs credentials, payloads, or failures.

## Exact firmware ingestion API, version 1

`POST /api/xaerova/environment/ingest` on the future configured HTTPS site origin.

Headers:

```text
Authorization: Bearer <unique xenv_ credential>
Content-Type: application/json
```

Maximum body: **8192 bytes**, measured from the actual stream. No browser CORS access is needed. Only POST is exported. No request cookies or user session are accepted as environmental credentials.

Example JSON (illustrative timestamp/identity; replace with actual synchronized time and provisioned identity):

```json
{
  "schemaVersion": 1,
  "deviceId": "existing-device-id",
  "firmware": "3.1.0",
  "bootId": "b4c7d1e2-6c7a-4ea2-9dc7-6b1a2c3d4e5f",
  "bootNumber": 12,
  "sequence": 48,
  "uptimeMs": 98000,
  "capturedAt": 1793025000000,
  "setupPaused": false,
  "sensors": {
    "wind": { "value": 0, "valid": true, "ageMs": 1000 },
    "rain": { "value": 0, "valid": true, "ageMs": 0 },
    "light": { "value": 50, "valid": true, "ageMs": 0 },
    "humidity": { "value": 64, "valid": true, "ageMs": 0 },
    "water_ingress": { "value": 0, "valid": true, "ageMs": 0 },
    "temperature": { "value": 25.562, "valid": true, "ageMs": 0 }
  }
}
```

| Field | Exact meaning / constraints |
| --- | --- |
| `schemaVersion` | Integer literal 1 |
| `deviceId` | Stable provisioned existing `xaerova_devices.device_id`; 1–128 ASCII letters/digits/`_.:-` |
| `firmware` | Actual publisher firmware version; 1–40 letters/digits/`.+_-`. `3.1.0` above is illustrative, not a firmware delivered here |
| `bootId` | Random UUID **v4**, new each physical boot |
| `bootNumber` | Durable positive integer, incremented/persisted before publishing on every boot; never reset on Wi-Fi reconnect |
| `sequence` | Nonnegative integer; increases for every new captured snapshot within a boot, starting at 0; never reused for changed content |
| `uptimeMs` | Nonnegative **64-bit extended monotonic milliseconds** since boot; never a wall clock; handle Arduino `millis()` rollover |
| `capturedAt` | Synchronized Unix epoch **milliseconds** at snapshot capture, not send/retry time |
| `setupPaused` | Boolean reflecting existing local paused state |
| `sensors` | Object keyed by the six stable keys above; all six expected from firmware. A missing key is normalized to invalid/null, never zero. Unknown keys are rejected |
| `value` | Finite converted display value; numeric 0/1 for rain/ingress. `null` when invalid. Never raw registers |
| `valid` | Actual boolean. `true` requires numeric value and known age; `false` requires `value: null` |
| `ageMs` | Nonnegative integer elapsed monotonic milliseconds since the sensor's last successful sample, measured **at capture**; at most uptime. `null` only for invalid/never-sampled values |

All counters/times must be JavaScript-safe integers (`0 … 9007199254740991`; bootNumber starts at 1). Top-level fields are required and unknown fields are rejected. Each supplied sensor must include all three fields; no status/config/raw/Wi-Fi/service details are allowed. Negative wind/lux, humidity outside 0–100, temperature outside DS18B20's −55–125 °C, and binary values other than 0 or 1 are rejected. Missing or invalid samples are accepted as unavailable; malformed samples reject the entire snapshot with 400.

Mapping the baseline `status.sensors` array: indexes `0=wind`, `1=rain`, `2=light`, `3=humidity`, `4=water_ingress`, `5=temperature`. `status.sensors[].value` is **already converted**; do not apply settings scale or offset again. Use monotonic last-good times for precise `ageMs`. If reusing the baseline's truncated whole-second `age`, conservatively use `age * 1000 + 999` (bounded by uptime) rather than underreporting age. Never use settings `revision` for ordering. A paused RS485 sensor is invalid; temperature continues independently.

### Clock, ordering, replay, receipt

The publisher must maintain clock error within **±2000 ms** against backend UTC. Synchronize before publishing; if time quality is unknown, keep local sampling/UI working and withhold cloud telemetry. Clock adjustments must not re-date old samples; preserve monotonic ages. If clock moves backwards relative to the last published capture, wait for recovery and capture a fresh snapshot. This contract relies on the authenticated firmware reporting honest ages/time; it cannot attest that physical sensor data is truthful.

Capture is accepted only in `[backend receipt − 5000 ms, backend receipt + 2000 ms]`. Old/future capture times return 422 without changing stored data or receipt time. Thus buffered readings cannot be made fresh by their arrival time.

A **single atomic conditional UPDATE** stores the latest snapshot only if:

- The current dedicated hash matches, the device is active, and its current owner still matches the provisioning owner.
- This is the first snapshot after provisioning, **or** capture time is not lower than the stored capture time and either:
  - `bootNumber` is greater and `bootId` differs; or
  - `bootNumber` and `bootId` match, `sequence` is strictly greater, and uptime is not lower.

Duplicate/replayed/lower-sequence packets, older boots, same-number/different-identity boots, falling uptime, or regressing capture timestamps return 409. They **never refresh receipt time**. Same-boot `sequence` may skip numbers (discarded snapshots are expected). Store bootNumber durably in the separate cloud namespace; failure to persist it must disable cloud publishing. After a deliberate counter/storage reset, rotate/reprovision the environmental credential and reset the server stream through the operator procedure, never invent a larger bootNumber from remote errors.

On 200, the backend stores the normalized JSON plus its own integer Unix-ms `receivedAt`. No historical series is retained. Response:

```json
{ "accepted": true, "receivedAt": 1793025000120 }
```

| HTTP | Meaning / firmware handling |
| --- | --- |
| 200 | Accepted; discard the transmitted snapshot |
| 400 | HTTP instead of HTTPS, invalid JSON/schema/value; fix payload, do not retry unchanged |
| 401 | Missing/incorrect/revoked credential, inactive/unprovisioned device or changed ownership; stop rapid retries, request operator provisioning |
| 409 | Replay/order conflict or authorization changed during processing; discard packet, capture next current snapshot; investigate persistent conflicts/counter reset |
| 413 | Actual body exceeds 8192 bytes; reduce payload |
| 415 | Content-Type must be application/json |
| 422 | Clock/delivery window violation; synchronize clock and capture a new snapshot |
| 429 / transient 5xx / timeout | Potential infrastructure response; bounded exponential backoff, no unbounded queue, return to newest snapshot on next attempt |

Retrying an already accepted snapshot may return 409 if its 200 response was lost, or 422 once outside the time window. It must not update the receipt clock. A retry must preserve capture timestamp, ages, boot identity, sequence and values exactly. Prefer dropping pending snapshots and sending a newly captured one. The transport should use a request timeout at most 5 seconds, one in-flight upload, and a one-snapshot maximum pending queue.

### Publication and freshness

Expected firmware publication: **every 2 seconds**, a compact current snapshot. On reconnect, immediately capture/publish the current state; never drain an old backlog. Keep sampling at its commissioned cadence (approximately every 2 seconds per RS485 sensor) and respect the existing local 10-second validity timeout. Cloud publication/retry must not block Modbus, OneWire, the local UI, Wi-Fi recovery, or setup. Use a bounded network task and a copied immutable snapshot; do not concurrently touch transaction state.

The dashboard fetches the cloud API every **5 seconds**, and recomputes displayed ages every **1 second**, including between responses. Server time from each response anchors the frontend, with conservative client transit/elapsed time (monotonic and wall-clock maximum), so a wrong client time does not make readings fresh.

For accepted telemetry, shared backend/frontend policy is:

```text
captureAgeUpperBound = max(0, receivedAt - capturedAt + 2000)
                    + max(0, serverNow - receivedAt)
sampleAgeUpperBound = sensor.ageMs + captureAgeUpperBound
```

- Available only when `valid`, value/age are known, and sample age is **strictly less than 10000 ms**.
- At **10000 ms** or beyond: show `—` / Unavailable · stale; never zero or Dry as fallback.
- Device offline when capture age reaches **30000 ms**. Last-update age displays the conservative capture age, not simply receipt age.
- Before any accepted snapshot: **Awaiting device connection**, all six cards unavailable. Saved JSON is never loaded by that route.
- `setupPaused` makes the five RS485 readings unavailable even if a bad publisher marks them valid. Temperature can remain current independently. A freshly reporting paused device may be online with unavailable sensors.
- Network/API failures display an explicit load error and retry automatically. No fixture fallback.
- Valid numeric zero displays as zero for wind/lux/humidity/temperature, and Dry for rain/ingress. Numeric one displays Rain/Wet. Formatting rounds numeric values for display only (light integer; other numbers one decimal).

## Exact authenticated readings API

`GET /api/xaerova/environment` returns all active devices belonging to the authenticated account, including unprovisioned devices with `snapshot: null`. Optional `?deviceId=<encoded existing ID>` restricts to one **owned** active device; 404 for another owner's/missing/inactive device. A verified Clerk Bearer session is used by the page; the existing verifier also supports its session-cookie convention. No separate user login is added.

```json
{
  "serverNow": 1793025000200,
  "devices": [
    {
      "deviceId": "existing-device-id",
      "deviceName": "Existing device name",
      "snapshot": {
        "receivedAt": 1793025000120,
        "telemetry": {
          "schemaVersion": 1,
          "deviceId": "existing-device-id",
          "firmware": "3.1.0",
          "bootId": "b4c7d1e2-6c7a-4ea2-9dc7-6b1a2c3d4e5f",
          "bootNumber": 12,
          "sequence": 48,
          "uptimeMs": 98000,
          "capturedAt": 1793025000000,
          "setupPaused": false,
          "sensors": {
            "wind": { "value": 0, "valid": true, "ageMs": 1000 },
            "rain": { "value": 0, "valid": true, "ageMs": 0 },
            "light": { "value": 50, "valid": true, "ageMs": 0 },
            "humidity": { "value": 64, "valid": true, "ageMs": 0 },
            "water_ingress": { "value": 0, "valid": true, "ageMs": 0 },
            "temperature": { "value": 25.562, "valid": true, "ageMs": 0 }
          }
        }
      }
    }
  ]
}
```

`deviceName` may be null. `snapshot` is null when there is no accepted telemetry, a credential is revoked, or its provisioning owner differs from the current owner. No credential/hash/NVS/settings/raw/private network information is returned. Unauthenticated requests return 401, unregistered Clerk users 403, and existing auth configuration failures 500. No user `PUT`, `POST`, or `DELETE` readings/control routes are implemented.

## Preserve commissioned hardware in the later update

Firmware source/hand-off directory was inspected but **not edited**. Do not flash or erase this stage. The later firmware must retain:

- NVS namespace **`skymind-env`**, binary `settings` C++ structure layout/size/version **1**, `setupPaused`, and `profilesV2`. Do not change partitions or erase flash; an incompatible settings change needs explicit migration, not defaults.
- Existing Wi-Fi configuration, fallback local access, RS485/OneWire drivers, local dashboard, pin assignments, power/ground distribution, manual direction on both SP3485 modules, and existing pull-up.
- Outdoor bus: 4800 8N1, manual direction, RX16/TX17/RSE25; wind ID2 FC03 reg0 ×0.1; rain ID1 FC03 reg0 binary; light ID3 FC03 regs0–1 high-word-first uint32 lux.
- Indoor bus: 9600 8N1, manual direction, RX32/TX33/RSE19; humidity ID2 FC03 reg1 ×0.1; ingress ID1 FC03 reg0 binary. Repeated IDs on separate buses are intentional.
- Powered DS18B20 GPIO4 with existing pull-up; LED GPIO2 behavior; no use of MCP23017 boards.
- **v3.0.1 UART fix:** RX pull-up; clear RX while TX finishes with receiver still disabled, then RSE low. Never flush after enabling RX. Keep separate automatic-direction behavior.
- No auto-commissioning, factory-default writes, rain configuration/heater writes, actuator changes, or cloud access to local mutation APIs.

The diagnostic settings are authoritative and are a reference, not an automatic restore/import file. Keep cloud credential/counter storage separate; preserve commissioned sampling and paused behavior across power cycles.

## Validation and remaining live prerequisites

Automated commands:

```sh
npm test
npx tsc --noEmit -p tsconfig.app.json
npx tsc --noEmit -p functions/tsconfig.json
npm run build
git diff --check
```

The new 16 tests exercise handlers with actual SQLite SQL, signed RSA Clerk JWTs and stubbed Clerk JWKS. They cover ownership isolation/spoofing, no cache, unauthorized ingestion, dedicated vs user/control credentials, HTTPS/schema/body limits, valid zeros and binary ones, no double scaling, missing/invalid/stale sensors, never-read values, paused RS485/independent temperature, freshness boundaries/delivery delay/future skew, delayed/future rejection, duplicate/concurrent/out-of-order/old-boot rejection without receipt refresh, reconnects, transfer/deactivation/revocation, hash-only provisioning/rotation, and the existing device list/rename/delete/pairing flows. Six-card HTML rendering is checked using the actual React component, not a parallel mock implementation. The pre-existing 8 helper OAuth/pairing tests also pass.

Browser QA results and limitations are recorded in `docs/environment-validation.md`. Automated backend tests do not replace deployment/runtime, real Clerk sign-in, firmware TLS/persistence, or physical sensor validation. None of those require a production release to review the implementation.

Still needed before live readings: verified ESP32-to-existing-device mapping, explicit local/staging migration and credential provisioning, later firmware publisher implementing this exact contract (including persistent boot counter and accurate UTC), authorized non-production end-to-end test, and physical checks of all six sensors, unplug/timeouts, internet disconnect/reconnect, and a retained-settings upgrade/power cycle. Production deployment and production schema rollout remain a separate authorized stage.
