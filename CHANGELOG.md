# 3.0.3-preview.1

Corrected the user-supplied 3.0.2 cloud candidate to match the already-deployed SkyMind preview API. Replaced incompatible payload/hex boot identity with the version-1 backend contract, durable boot counter and UUID v4, UTC synchronization, 64-bit uptime/sample ages, 2-second publication, trusted TLS bundle and current-only retry handling. Added portable encoder/contract tests and one private settings-header template. Retained commissioned storage layout, drivers, pins and UART fix. No hardware was flashed; see VALIDATION.md for exact build/core and limits.
