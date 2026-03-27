# flexlm-broker

Centralized FlexLM license broker for Linux.

- **Aggregates** licenses from a dynamic pool of backend FlexLM servers (scales to hundreds)
- **Failover** — unhealthy backends are automatically removed from the pool and re-added when they recover
- **Usage tracking** — license utilisation and denial events persisted to PostgreSQL
- **REST API** — query pool status and metrics over HTTP

---

## Quick Start

```bash
# 1. Fetch header-only dependencies (nlohmann/json, cpp-httplib, spdlog)
./scripts/fetch_deps.sh

# 2. Create the PostgreSQL database and apply schema
FLEXLM_DB=flexlm FLEXLM_USER=broker FLEXLM_PASS=secret ./scripts/setup_db.sh

# 3. Build (Release by default)
./scripts/build.sh

# 4. Configure
sudo mkdir -p /etc/flexlm-broker
sudo cp config/broker.conf.example /etc/flexlm-broker/broker.conf
# Edit: add your backend servers, DB connstr, API token

# 5. Run
./build/flexlm-broker /etc/flexlm-broker/broker.conf
```

---

## Build Requirements

| Requirement  | Install (Debian/Ubuntu)              |
|--------------|--------------------------------------|
| CMake ≥ 3.16 | `apt install cmake`                  |
| GCC/Clang    | `apt install build-essential`        |
| libpq-dev    | `apt install libpq-dev`              |
| pkg-config   | `apt install pkg-config`             |
| lmutil       | Provided by your FlexLM vendor       |
| GTest        | `apt install libgtest-dev` (optional)|

---

## Configuration

Copy `config/broker.conf.example` and edit:

```ini
[broker]
port = 27000          # port clients connect to

[api]
port  = 8080
token = <strong-random-token>

[pool]
poll_interval_sec  = 30
failover_threshold = 3

[server.1]
host = licserver1.example.com
port = 27000
name = primary

[server.2]
host = licserver2.example.com
port = 27000
name = secondary

[database]
connstr = host=localhost dbname=flexlm user=broker password=secret

[logging]
level = info
```

Add as many `[server.N]` blocks as needed.

---

## REST API

All endpoints return JSON. Authenticated endpoints require:
```
Authorization: Bearer <token>
```

| Endpoint                    | Auth | Description                      |
|-----------------------------|------|----------------------------------|
| `GET /api/v1/health`             | No  | Broker liveness                       |
| `GET /api/v1/features`           | Yes | Aggregated live pool (from lmstat)    |
| `GET /api/v1/features/:name`     | Yes | Single feature detail                 |
| `GET /api/v1/servers`            | Yes | Backend server health                 |
| `POST /api/v1/servers`           | Yes | Add backend server at runtime         |
| `DELETE /api/v1/servers/:h/:p`   | Yes | Remove backend server at runtime      |
| `GET /api/v1/utilisation`        | Yes | DB-backed utilisation (per poll)      |
| `GET /api/v1/denials`            | Yes | Denial counts per feature (24 h)      |
| `GET /api/v1/checkouts/active`   | Yes | All currently open checkouts          |
| `GET /api/v1/health/events`      | Yes | Recent server UP/DOWN events          |

### Example — check pool utilisation
```bash
curl -H "Authorization: Bearer changeme" http://localhost:8080/api/v1/utilisation
```
```json
[
  { "feature": "MATLAB", "total": 50, "in_use": 12, "available": 38, "queued": 0, "last_polled": "2026-03-27T10:00:00Z" }
]
```

### Example — active checkouts
```bash
curl -H "Authorization: Bearer changeme" http://localhost:8080/api/v1/checkouts/active
```
```json
[
  { "id": 1, "feature": "MATLAB", "username": "jsmith", "client_host": "ws1", "backend_host": "licserver1", "backend_port": 27000, "checked_out_at": "2026-03-27T09:00:00Z", "duration_sec": 3600 }
]
```

### Example — original features endpoint (live from lmstat)
```bash
curl -H "Authorization: Bearer changeme" http://localhost:8080/api/v1/features
```
```json
[
  { "feature": "MATLAB",   "total": 50, "in_use": 12, "available": 38 },
  { "feature": "Simulink", "total": 20, "in_use":  0, "available": 20 }
]
```

---

## Prometheus / Grafana

Scrape `GET /metrics` (no auth needed):

```yaml
# prometheus.yml
scrape_configs:
  - job_name: flexlm_broker
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: /metrics
```

Key dashboards to build in Grafana:
- `flexlm_feature_in_use / flexlm_feature_total` — utilisation gauge
- `flexlm_denials_24h` — bar chart per feature
- `flexlm_backend_healthy` — backend status table
- `flexlm_active_checkouts_total` — live open checkouts

## Alerting

Configure in `broker.conf`:

```ini
[alerts]
webhook_url            = https://hooks.slack.com/services/XXX/YYY/ZZZ
webhook_secret         = my-hmac-secret
cooldown_sec           = 300
denial_spike_threshold = 10
pool_exhaustion_pct    = 0.95
```

Webhook payload (JSON):
```json
{
  "type":      "POOL_EXHAUSTED",
  "subject":   "MATLAB",
  "message":   "MATLAB at 98% utilisation (49/50)",
  "timestamp": "2026-03-27T10:00:00Z"
}
```

Test the webhook without waiting for a real alert:
```bash
curl -X POST -H "Authorization: Bearer <token>" \
  http://localhost:8080/api/v1/admin/alerts/test
```

---

## Running Tests

```bash
# Unit tests only
./scripts/run_tests.sh

# Unit + integration (requires live PostgreSQL)
export TEST_DB_CONNSTR="host=localhost dbname=flexlm user=broker password=secret"
./scripts/run_tests.sh --integration
```

---

## Project Structure

```
flexlm-broker/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── common/        # Config parser, logger
│   ├── pool/          # PoolManager, LmutilWrapper
│   ├── health/        # HealthMonitor
│   ├── tracker/       # UsageTracker, DbConnection
│   ├── api/           # RestApi (cpp-httplib)
│   └── broker/        # Broker (TCP), VirtualLicense
├── include/           # Public headers (mirrors src/)
├── tests/
│   ├── unit/          # GTest unit tests
│   └── integration/   # GTest integration tests
├── sql/
│   └── 001_schema.sql # PostgreSQL schema + views
├── config/
│   └── broker.conf.example
├── scripts/
│   ├── fetch_deps.sh  # Download header-only deps
│   ├── setup_db.sh    # Create DB + apply schema
│   ├── build.sh       # CMake configure + make
│   └── run_tests.sh   # Run ctest
├── docs/
│   └── architecture.md
└── third_party/       # Populated by fetch_deps.sh
```

---

## Development Phases

| Phase | Status      | Scope                                               |
|-------|-------------|-----------------------------------------------------|
| 0     | ✅ Complete  | Scaffold — all components stubbed and wired         |
| 1     | 🔲 Next      | lmutil output parser + pool polling tests           |
| 2     | ✅ Complete  | Dynamic config reload (SIGHUP + mtime watcher)      |
| 3     | ✅ Complete  | Full PostgreSQL tracking (checkout/checkin/denial)  |
| 4     | ✅ Complete  | FlexLM TCP protocol framing, connection state machine, thread pool |
| 5     | ✅ Complete  | Alerting webhooks, Prometheus metrics, admin API    |

See `docs/architecture.md` for detailed design notes.
