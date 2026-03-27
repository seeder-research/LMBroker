# FlexLM Broker — Architecture

## Overview

```
Clients  (lmgrd protocol, port 27000)
    │
    ▼
┌───────────────────────────────────────────┐
│  flexlm-broker                            │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Broker  (src/broker/)              │  │
│  │  TCP listener + protocol dispatcher │  │
│  │  Virtual LICENSE file generator     │  │
│  └──────────────┬──────────────────────┘  │
│                 │ select_backend()         │
│  ┌──────────────▼──────────────────────┐  │
│  │  Pool Manager  (src/pool/)          │  │
│  │  Polls all backends via lmutil      │  │
│  │  Aggregates feature counts          │  │
│  │  Tracks fail streaks per server     │  │
│  └──────────────┬──────────────────────┘  │
│                 │ backend_statuses()       │
│  ┌──────────────▼──────────────────────┐  │
│  │  Health Monitor  (src/health/)      │  │
│  │  Logs unhealthy backends            │  │
│  │  (Alerting hooks added in Phase 5)  │  │
│  └─────────────────────────────────────┘  │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  Usage Tracker  (src/tracker/)      │  │
│  │  Async event queue                  │  │
│  │  Persists to PostgreSQL             │  │
│  └─────────────────────────────────────┘  │
│                                           │
│  ┌─────────────────────────────────────┐  │
│  │  REST API  (src/api/)  :8080        │  │
│  │  GET /api/v1/health                 │  │
│  │  GET /api/v1/features[/:name]       │  │
│  │  GET /api/v1/servers                │  │
│  └─────────────────────────────────────┘  │
└───────────────────────────────────────────┘
    │  lmutil lmstat (subprocess)
    ▼
Backend pool: lmgrd server 1 … N  (scales to hundreds)
```

## Component Details

### Broker (`src/broker/`)
- Binds the advertised FlexLM port (default 27000).
- Accepts client connections; each connection handled in its own thread.
- **Phase 1 (scaffold)**: TCP accept loop only.
- **Phase 4**: Full FlexLM protocol framing — parses CHECKOUT/CHECKIN/QUERY
  requests and forwards to the appropriate backend via `PoolManager::select_backend()`.
- `VirtualLicense` renders a synthetic `LICENSE` file so clients see a single
  `SERVER` line pointing to the broker.

### Pool Manager (`src/pool/`)
- Maintains a list of `BackendStatus` structs (one per server entry in config).
- Background thread polls every `poll_interval_sec` using `lmutil lmstat`.
- `LmutilWrapper::parse_lmstat()` parses the text output with a regex into
  `FeatureCount` structs.
- `aggregated_features()` sums counts across all healthy backends.
- `select_backend(feature)` returns the first healthy server that has at least
  one available seat for the requested feature (round-robin / priority ordering
  is a Phase 3 enhancement).

### Health Monitor (`src/health/`)
- Reads `backend_statuses()` on the same interval as the poll.
- Logs warnings for any backend with `healthy == false`.
- Future: emit alerts via webhook / email / SNMP.

### Usage Tracker (`src/tracker/`)
- Thread-safe event queue — callers call `record()` and return immediately.
- Worker thread drains the queue and writes to PostgreSQL via `DbConnection`
  (a thin libpq wrapper).
- Handles CHECKOUT, CHECKIN, DENIAL, SERVER_UP, SERVER_DOWN events.
- Auto-reconnects on lost DB connection.

### REST API (`src/api/`)
- Embedded HTTP server (cpp-httplib).
- Optional Bearer token authentication (`api_token` in config).
- JSON responses via nlohmann/json.

## Failover Logic

```
poll_loop() runs every poll_interval_sec
    for each BackendStatus bs:
        features = lmutil lmstat port@host
        if features empty:
            bs.fail_streak++
            if fail_streak >= failover_threshold:
                mark unhealthy, emit SERVER_DOWN event
        else:
            if was unhealthy: emit SERVER_UP event
            bs.healthy = true
            bs.fail_streak = 0
            bs.features = features
```

`select_backend()` skips any backend where `healthy == false`.  
A client requesting a feature that is only available on unhealthy backends
will receive a denial — recorded in the `denials` table.

## Database Schema

See `sql/001_schema.sql`.

| Table           | Purpose                                        |
|-----------------|------------------------------------------------|
| `servers`       | One row per backend server                     |
| `features`      | Polled license counts per server               |
| `checkouts`     | Checkout events (open until CHECKIN)           |
| `denials`       | Denied requests                                |
| `health_events` | UP/DOWN transitions per server                 |

Convenience views:
- `v_license_utilisation` — current aggregated counts
- `v_denial_rate_24h` — denial counts per feature over last 24 h

## Phase 4 — Protocol Layer

### Framing (`src/broker/framing.cpp`)

All FlexLM packets use a 4-byte header:

```
Bytes 0-1  uint16 BE  payload_length  (bytes that follow the header)
Bytes 2-3  uint16 BE  opcode
Bytes 4+   []byte     payload
```

`FrameReader` accumulates bytes from a TCP stream and emits complete `Packet`
structs via `pop()`. It handles partial receives, multi-packet buffers, and
oversized-packet detection.

`frame_packet()` / `send_packet()` / `recv_packet()` form the write path.

### Protocol (`src/broker/protocol.cpp`)

Typed encode/decode for every opcode:

| Opcode | Value | Direction | Description |
|---|---|---|---|
| HELLO | 0x0001 | C→S | Client announces version + identity |
| HELLO_ACK | 0x0002 | S→C | Server version response |
| CHECKOUT | 0x0010 | C→S | Request feature seat |
| CHECKOUT_ACK | 0x0011 | S→C | Grant (handle) or deny (reason) |
| CHECKIN | 0x0020 | C→S | Return feature seat |
| CHECKIN_ACK | 0x0021 | S→C | Confirm checkin |
| HEARTBEAT | 0x0030 | C→S | Keepalive |
| HEARTBEAT_ACK | 0x0031 | S→C | Keepalive echo |
| QUERY | 0x0040 | C→S | Request feature availability |
| QUERY_ACK | 0x0041 | S→C | Feature list with counts |
| ERROR | 0x00FF | Both | Error code + message |

String fields use length-prefixed encoding: 1 byte length, then N chars (no null terminator).

### Connection state machine (`src/broker/connection.cpp`)

```
INIT ──► NEGOTIATING ──► ACTIVE ──► CLOSING ──► DONE
              │                          ▲
              └──────── error ───────────┘
```

- **NEGOTIATING**: expects HELLO; responds with HELLO_ACK; advances to ACTIVE
- **ACTIVE**: handles CHECKOUT, CHECKIN, HEARTBEAT, QUERY
- **CHECKOUT**: queries `PoolManager::select_backend()`, opens a fresh TCP
  connection to the backend, proxies the message, records CHECKOUT or DENIAL
  event with the tracker, assigns a broker-local handle
- **CHECKIN**: matches handle to checkout record, notifies backend, records
  CHECKIN event; abandoned checkouts on close also emit CHECKIN
- **QUERY**: answered entirely from `PoolManager::aggregated_features()` — no
  backend round-trip needed
- **UNKNOWN opcode**: transparently proxied to first healthy backend

### Thread pool (`src/broker/thread_pool.cpp`)

Fixed-size pool (default 32 threads, queue depth 2048). When the queue is
full, the *oldest* pending task is dropped (shed load, never block accept).
`submit()` returns false when the pool is stopping.

## REST API Reference


All endpoints return `Content-Type: application/json`.  
Authenticated endpoints require: `Authorization: Bearer <token>`

| Method | Path                        | Auth | Description                        |
|--------|-----------------------------|------|------------------------------------|
| GET    | `/api/v1/health`            | No   | Broker liveness check              |
| GET    | `/api/v1/features`          | Yes  | Aggregated pool feature list       |
| GET    | `/api/v1/features/:name`    | Yes  | Single feature detail              |
| GET    | `/api/v1/servers`           | Yes  | Backend server health status       |

### Example: `GET /api/v1/features`
```json
[
  { "feature": "MATLAB",   "total": 70, "in_use": 12, "available": 58 },
  { "feature": "Simulink", "total": 30, "in_use":  5, "available": 25 }
]
```

### Example: `GET /api/v1/servers`
```json
[
  { "host": "licserver1.example.com", "port": 27000, "name": "primary",   "healthy": true,  "fail_streak": 0, "features": 8 },
  { "host": "licserver2.example.com", "port": 27000, "name": "secondary", "healthy": false, "fail_streak": 4, "features": 0 }
]
```

## Build & Dependency Requirements

| Requirement    | Min version | Notes                              |
|----------------|-------------|------------------------------------|
| CMake          | 3.16        |                                    |
| GCC or Clang   | C++17       |                                    |
| libpq-dev      | any         | PostgreSQL client library          |
| pkg-config     | any         |                                    |
| lmutil         | any         | Must be in `PATH` at runtime       |
| nlohmann/json  | 3.11.x      | Fetched by `scripts/fetch_deps.sh` |
| cpp-httplib    | 0.15.x      | Fetched by `scripts/fetch_deps.sh` |
| spdlog         | 1.13.x      | Fetched by `scripts/fetch_deps.sh` |

## Development Phases

| Phase | Status      | Description                                       |
|-------|-------------|---------------------------------------------------|
| 0     | ✅ Complete  | Project scaffold                                  |
| 1     | ✅ Complete  | lmutil parser + pool polling integration tests    |
| 2     | 🔲 Planned   | Config file parser with dynamic server reload     |
| 3     | 🔲 Planned   | Full PostgreSQL tracking (checkout/checkin/denial)|
| 4     | 🔲 Planned   | FlexLM protocol broker (TCP framing)              |
| 5     | 🔲 Planned   | Alerting, metrics export, admin endpoints         |
