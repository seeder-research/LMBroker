# Getting Started Guide

This guide walks you through cloning, building, configuring, and running
`flexlm-broker` from a clean Linux machine.

---

## Prerequisites

### System packages

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libpq-dev \
  libspdlog-dev \
  nlohmann-json3-dev \
  libcpp-httplib-dev \
  postgresql \
  postgresql-client \
  libgtest-dev        # optional — only needed to run tests
```

> **RHEL/CentOS equivalent:**
> ```bash
> sudo dnf install -y gcc-c++ cmake pkg-config libpq-devel \
>   spdlog-devel json-devel postgresql-server postgresql-devel
> # httplib and nlohmann may need manual install — see Step 3
> ```

### lmutil

`lmutil` is the FlexLM command-line tool shipped by your license vendor
(e.g. MathWorks, Synopsys, Cadence). The broker shells out to it to query
backend servers.

- It must be on `PATH` when the broker runs.
- Test it: `lmutil lmstat -c 27000@your-license-server`
- If `lmutil` is not on `PATH`, set `LMUTIL` in your environment or symlink
  it to `/usr/local/bin/lmutil`.

### PostgreSQL

The broker uses PostgreSQL to store utilisation history, checkout events,
and denial records.

If you already have a PostgreSQL server, you only need the client libraries
(`libpq-dev`). You can point the broker at any accessible instance.

---

## Step 1 — Clone the repository

```bash
git clone https://github.com/seeder-research/LMBroker.git
cd LMBroker
```

---

## Step 2 — Build GTest (Ubuntu only, if running tests)

On Ubuntu, `libgtest-dev` ships source only. Build it once:

```bash
cd /usr/src/googletest
sudo cmake . -B build -DCMAKE_BUILD_TYPE=Release
sudo cmake --build build -j$(nproc)
sudo cp build/lib/libgtest*.a /usr/local/lib/
# Return to your repo
cd -
```

Skip this step if you do not need to run the test suite.

---

## Step 3 — Build the broker

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
```

On a successful build you will see:

```
[100%] Built target flexlm-broker
```

The binary is at `build/flexlm-broker`.

> **If cmake reports missing libraries:**
>
> | Error | Fix |
> |---|---|
> | `libpq not found` | `apt install libpq-dev` |
> | `httplib not found` | `apt install libcpp-httplib-dev` |
> | `spdlog not found` | `apt install libspdlog-dev` |
> | `nlohmann/json not found` | `apt install nlohmann-json3-dev` |

---

## Step 4 — Set up the database

### 4a. Start PostgreSQL (if not already running)

```bash
sudo systemctl start postgresql
sudo systemctl enable postgresql  # start on boot
```

### 4b. Create the database, user, and schema

```bash
export FLEXLM_DB=flexlm
export FLEXLM_USER=broker
export FLEXLM_PASS=changeme          # choose a strong password

sudo -u postgres ./scripts/setup_db.sh
```

This creates:
- A PostgreSQL user `broker` with the given password
- A database `flexlm` owned by `broker`
- All required tables and views (via `scripts/migrate.sh`)

### 4c. Verify

```bash
psql -U broker -d flexlm -c "\dt"
```

Expected output lists: `checkouts`, `denials`, `features`, `health_events`,
`schema_migrations`, `servers`.

---

## Step 5 — Configure

```bash
sudo mkdir -p /etc/flexlm-broker
sudo cp config/broker.conf.example /etc/flexlm-broker/broker.conf
sudo $EDITOR /etc/flexlm-broker/broker.conf
```

### Minimum required changes

**1. Add your backend license servers**

Replace (or add to) the example `[server.N]` blocks:

```ini
[server.1]
host = licserver1.yourdomain.com
port = 27000
name = primary

[server.2]
host = licserver2.yourdomain.com
port = 27000
name = secondary
```

Add one block per backend. Labels (`N`) can be any unique string.

**2. Set the database connection string**

```ini
[database]
connstr = host=localhost port=5432 dbname=flexlm user=broker password=changeme sslmode=prefer
```

If PostgreSQL is on a different host, replace `localhost` with its address.

**3. Set a strong API token**

```ini
[api]
port  = 8080
token = replace-with-a-long-random-string
```

Generate one: `openssl rand -hex 32`

### Full annotated config reference

```ini
[broker]
host = 0.0.0.0    # interface to bind (0.0.0.0 = all interfaces)
port = 27000      # port your FlexLM clients connect to

[api]
host  = 0.0.0.0
port  = 8080
token = <your-token>   # required for all API calls except /health and /metrics

[pool]
poll_interval_sec  = 30   # how often to query each backend with lmutil
failover_threshold = 3    # consecutive failures before marking a backend unhealthy

# One block per backend license server. Labels must be unique.
[server.1]
host = licserver1.example.com
port = 27000
name = primary

[server.2]
host = licserver2.example.com
port = 27000
name = secondary

[database]
connstr = host=localhost port=5432 dbname=flexlm user=broker password=secret sslmode=prefer

[logging]
level = info          # trace | debug | info | warn | error
# file = /var/log/flexlm-broker/broker.log   # omit to log to stdout

[alerts]
# webhook_url            = https://hooks.slack.com/services/XXX/YYY/ZZZ
# webhook_secret         = my-hmac-secret
cooldown_sec             = 300
denial_spike_threshold   = 10
pool_exhaustion_pct      = 0.95
```

---

## Step 6 — Run

### Directly (foreground)

```bash
./build/flexlm-broker /etc/flexlm-broker/broker.conf
```

You should see startup messages like:

```
[info] [pool]   Added backend licserver1.example.com:27000 (primary)
[info] [pool]   Added backend licserver2.example.com:27000 (secondary)
[info] [broker] Listening on port 27000 (32 worker threads)
[info] [api]    REST API listening on 0.0.0.0:8080
[info] flexlm-broker started (pid 12345)
[info] Send SIGHUP or edit /etc/flexlm-broker/broker.conf to reload config
```

### As a systemd service (recommended for production)

```bash
sudo tee /etc/systemd/system/flexlm-broker.service << 'EOF'
[Unit]
Description=FlexLM License Broker
After=network.target postgresql.service

[Service]
Type=simple
User=flexlm
Group=flexlm
ExecStart=/opt/flexlm-broker/flexlm-broker /etc/flexlm-broker/broker.conf
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

# Reload config without restart
ExecReload=/bin/kill -HUP $MAINPID

[Install]
WantedBy=multi-user.target
EOF

sudo useradd -r -s /sbin/nologin flexlm
sudo cp build/flexlm-broker /opt/flexlm-broker/
sudo systemctl daemon-reload
sudo systemctl enable --now flexlm-broker
sudo systemctl status flexlm-broker
```

---

## Step 7 — Verify it is working

### Check broker health

```bash
curl http://localhost:8080/api/v1/health
```

```json
{
  "status": "ok",
  "servers_healthy": 2,
  "servers_total": 2
}
```

### Check the live license pool

```bash
TOKEN="replace-with-your-token"
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/v1/features
```

```json
[
  {
    "feature":   "MATLAB",
    "vendor":    "MLM",
    "total":     50,
    "in_use":    12,
    "available": 38,
    "queued":    0,
    "uncounted": false
  }
]
```

If the list is empty, the broker has not polled the backends yet (first poll
runs immediately on startup — wait a few seconds and retry), or `lmutil` is
not in `PATH`.

### Check backend server status

```bash
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8080/api/v1/servers
```

A healthy backend shows `"healthy": true` and `"fail_streak": 0`.

### Check Prometheus metrics

```bash
curl http://localhost:8080/metrics
```

```
# HELP flexlm_feature_total Total licensed seats per feature
# TYPE flexlm_feature_total gauge
flexlm_feature_total{feature="MATLAB",vendor="MLM"} 50
...
```

---

## Step 8 — Point FlexLM clients at the broker

On your workstations and compute nodes, set the `LM_LICENSE_FILE` environment
variable (or your application's equivalent) to point at the broker instead of
directly at your backend servers:

```bash
export LM_LICENSE_FILE=27000@broker-hostname
```

Or in your license config file:

```
SERVER broker-hostname ANY 27000
```

The broker presents itself as a standard FlexLM server — existing clients
require no modification.

---

## Reload config without restarting

After editing `broker.conf` (e.g. to add or remove a backend server):

```bash
# Option A — SIGHUP
sudo kill -HUP $(pidof flexlm-broker)

# Option B — systemctl (preferred with systemd)
sudo systemctl reload flexlm-broker

# Option C — just save the file
# The broker's mtime watcher picks up changes automatically within 15 seconds.
```

The broker will log which servers were added or removed and continue serving
clients without interruption.

---

## Troubleshooting

### Backends show `"healthy": false`

1. Confirm `lmutil` is on `PATH`: `which lmutil`
2. Test manually: `lmutil lmstat -c 27000@licserver1.example.com`
3. Check firewall: `telnet licserver1.example.com 27000`
4. Increase log verbosity: set `level = debug` in `[logging]`

### Features list is empty

- The backend may have no licenses checked out — wait for `poll_interval_sec`
  and check again.
- Run `lmutil lmstat -a -c 27000@licserver1.example.com` manually and
  confirm it lists features. If it does not, the parser may need tuning for
  your specific `lmutil` version.

### Database connection errors

```
[error] [db] Connect failed: FATAL: role "broker" does not exist
```

Re-run `scripts/setup_db.sh` or create the role manually:

```sql
CREATE USER broker WITH PASSWORD 'changeme';
GRANT ALL PRIVILEGES ON DATABASE flexlm TO broker;
```

### Port 27000 already in use

The broker uses port 27000 by default. If your machine is running `lmgrd`
locally, change the broker port:

```ini
[broker]
port = 27100
```

Then tell clients: `LM_LICENSE_FILE=27100@broker-hostname`

### API returns 401 Unauthorized

Ensure the `Authorization` header is correct:

```bash
curl -H "Authorization: Bearer your-exact-token-here" \
  http://localhost:8080/api/v1/features
```

Note: `/api/v1/health` and `/metrics` do **not** require authentication.

---

## Running the test suite

```bash
# Unit tests (no external dependencies)
cd build && ctest --output-on-failure -E integration

# Integration tests (require a live PostgreSQL instance)
export TEST_DB_CONNSTR="host=localhost dbname=flexlm user=broker password=changeme"
ctest --output-on-failure -L integration
```

Expected unit test result: **74/74 passed**.

---

## Upgrading

After pulling new code:

```bash
git pull origin main
cd build && make -j$(nproc)

# Apply any new database migrations
FLEXLM_USER=broker FLEXLM_DB=flexlm ./scripts/migrate.sh

# Reload (zero-downtime for config changes; restart for binary updates)
sudo systemctl restart flexlm-broker
```
