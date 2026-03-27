#include "tracker/usage_tracker.h"
#include "tracker/db_connection.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>

namespace tracker {

// ── helpers ───────────────────────────────────────────────────────────────────

static long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch()).count();
}

static std::string ms_to_iso(long long ms) {
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&sec));
    return buf;
}

// Pull a named column from a DbRow; returns "" if not found
static std::string col(const DbRow& row, const std::string& name) {
    for (const auto& [k, v] : row) if (k == name) return v;
    return "";
}

static int col_int(const DbRow& row, const std::string& name, int def = 0) {
    auto v = col(row, name);
    return v.empty() ? def : std::stoi(v);
}

// ── Impl ──────────────────────────────────────────────────────────────────────

struct UsageTracker::Impl {
    DbConnection db;
    explicit Impl(const std::string& connstr) : db(connstr) {}
};

// ── lifecycle ─────────────────────────────────────────────────────────────────

UsageTracker::UsageTracker(const common::Config& cfg) : cfg_(cfg) {}
UsageTracker::~UsageTracker() { stop(); }

void UsageTracker::start() {
    impl_    = std::make_unique<Impl>(cfg_.db_connstr);
    running_ = true;
    worker_  = std::thread(&UsageTracker::flush_loop, this);
}

void UsageTracker::stop() {
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void UsageTracker::record(UsageEvent event) {
    if (event.timestamp_ms == 0)
        event.timestamp_ms = now_ms();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.push(std::move(event));
    }
    cv_.notify_one();
}

// ── async flush ───────────────────────────────────────────────────────────────

void UsageTracker::flush_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::seconds(2),
                     [this]{ return !queue_.empty() || !running_; });
        while (!queue_.empty()) {
            auto ev = std::move(queue_.front());
            queue_.pop();
            lk.unlock();
            try   { persist(ev); }
            catch (const std::exception& e) {
                spdlog::error("[tracker] persist failed: {}", e.what());
            }
            lk.lock();
        }
    }
}

// ── persist dispatcher ────────────────────────────────────────────────────────

void UsageTracker::persist(const UsageEvent& ev) {
    if (!impl_ || !impl_->db.is_connected()) {
        impl_->db.reconnect();
        if (!impl_->db.is_connected()) return;
    }

    std::string ts       = ms_to_iso(ev.timestamp_ms);
    std::string port_str = std::to_string(ev.backend_port);

    switch (ev.type) {

    // ── CHECKOUT ──────────────────────────────────────────────────────────
    case EventType::CHECKOUT: {
        const char* v[] = {
            ev.feature.c_str(), ev.user.c_str(),
            ev.client_host.c_str(), ev.backend_host.c_str(),
            port_str.c_str(), ts.c_str()
        };
        impl_->db.execute_params(
            "INSERT INTO checkouts "
            "(feature, username, client_host, backend_host, backend_port, checked_out_at)"
            " VALUES ($1,$2,$3,$4,$5,$6)",
            6, v);
        spdlog::debug("[tracker] CHECKOUT feature={} user={} host={}",
                      ev.feature, ev.user, ev.client_host);
        break;
    }

    // ── CHECKIN ───────────────────────────────────────────────────────────
    case EventType::CHECKIN: {
        // Close the most recent open checkout matching feature+user+client_host
        const char* v[] = {
            ts.c_str(), ev.feature.c_str(),
            ev.user.c_str(), ev.client_host.c_str()
        };
        bool ok = impl_->db.execute_params(
            "UPDATE checkouts SET checked_in_at = $1 "
            "WHERE id = ("
            "  SELECT id FROM checkouts "
            "  WHERE feature=$2 AND username=$3 AND client_host=$4 "
            "    AND checked_in_at IS NULL "
            "  ORDER BY checked_out_at DESC LIMIT 1"
            ")",
            4, v);
        if (ok)
            spdlog::debug("[tracker] CHECKIN feature={} user={}", ev.feature, ev.user);
        break;
    }

    // ── DENIAL ────────────────────────────────────────────────────────────
    case EventType::DENIAL: {
        const char* reason = ev.denial_reason.empty()
                             ? nullptr : ev.denial_reason.c_str();
        // Use 5-param version; pass NULL for reason when empty
        const char* v[] = {
            ev.feature.c_str(), ev.user.c_str(),
            ev.client_host.c_str(), ts.c_str(),
            reason  // may be nullptr — PQexecParams handles NULL params
        };
        impl_->db.execute_params(
            "INSERT INTO denials (feature, username, client_host, denied_at, reason)"
            " VALUES ($1,$2,$3,$4,$5)",
            5, v);
        spdlog::debug("[tracker] DENIAL feature={} user={} reason={}",
                      ev.feature, ev.user, ev.denial_reason);
        break;
    }

    // ── FEATURE_POLL ──────────────────────────────────────────────────────
    case EventType::FEATURE_POLL:
        persist_feature_poll(ev);
        break;

    // ── SERVER_UP / SERVER_DOWN ───────────────────────────────────────────
    case EventType::SERVER_UP:
    case EventType::SERVER_DOWN:
        persist_server_event(ev);
        break;

    } // switch
}

// ── FEATURE_POLL persistence ──────────────────────────────────────────────────
// Upserts the server row, then inserts a feature snapshot.
// Each poll creates a new row — historical data is preserved for trending.
void UsageTracker::persist_feature_poll(const UsageEvent& ev) {
    std::string ts       = ms_to_iso(ev.timestamp_ms);
    std::string port_str = std::to_string(ev.backend_port);
    std::string total    = std::to_string(ev.total);
    std::string in_use   = std::to_string(ev.in_use);
    std::string queued   = std::to_string(ev.queued);

    // Upsert server (sets name if provided)
    {
        const char* v[] = {
            ev.backend_host.c_str(), port_str.c_str(), ev.vendor.c_str()
        };
        impl_->db.execute_params(
            "INSERT INTO servers (host, port, name) VALUES ($1, $2, $3) "
            "ON CONFLICT (host, port) DO UPDATE SET name = EXCLUDED.name "
            "WHERE servers.name IS NULL OR servers.name = ''",
            3, v);
    }

    // Insert feature snapshot
    {
        const char* v[] = {
            ev.backend_host.c_str(), port_str.c_str(),
            ev.feature.c_str(), ev.vendor.c_str(),
            total.c_str(), in_use.c_str(), queued.c_str(), ts.c_str()
        };
        impl_->db.execute_params(
            "INSERT INTO features "
            "(server_id, feature, vendor, total, in_use, queued, polled_at) "
            "VALUES ("
            "  (SELECT id FROM servers WHERE host=$1 AND port=$2),"
            "  $3, $4, $5, $6, $7, $8"
            ")",
            8, v);
    }

    spdlog::debug("[tracker] FEATURE_POLL {}:{} feature={} total={} in_use={}",
                  ev.backend_host, ev.backend_port,
                  ev.feature, ev.total, ev.in_use);
}

// ── SERVER_UP/DOWN persistence ────────────────────────────────────────────────
void UsageTracker::persist_server_event(const UsageEvent& ev) {
    std::string ts       = ms_to_iso(ev.timestamp_ms);
    std::string port_str = std::to_string(ev.backend_port);
    const char* event_str = (ev.type == EventType::SERVER_UP) ? "UP" : "DOWN";

    // Upsert server
    {
        const char* v[] = { ev.backend_host.c_str(), port_str.c_str() };
        impl_->db.execute_params(
            "INSERT INTO servers (host, port) VALUES ($1, $2) "
            "ON CONFLICT (host, port) DO NOTHING",
            2, v);
    }

    // Insert health event
    {
        const char* v[] = {
            ev.backend_host.c_str(), port_str.c_str(), event_str, ts.c_str()
        };
        impl_->db.execute_params(
            "INSERT INTO health_events (server_id, event, occurred_at) "
            "VALUES ("
            "  (SELECT id FROM servers WHERE host=$1 AND port=$2),"
            "  $3, $4"
            ")",
            4, v);
    }

    spdlog::info("[tracker] SERVER_{} {}:{}", event_str,
                 ev.backend_host, ev.backend_port);
}

// ── synchronous query methods ─────────────────────────────────────────────────

std::vector<UtilisationRow> UsageTracker::query_utilisation() {
    if (!impl_) return {};
    auto rows = impl_->db.query(
        "SELECT feature, total, in_use, available, queued, "
        "       to_char(last_polled, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS last_polled "
        "FROM v_license_utilisation "
        "ORDER BY feature");

    std::vector<UtilisationRow> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        UtilisationRow r;
        r.feature     = col(row, "feature");
        r.total       = col_int(row, "total");
        r.in_use      = col_int(row, "in_use");
        r.available   = col_int(row, "available");
        r.queued      = col_int(row, "queued");
        r.last_polled = col(row, "last_polled");
        result.push_back(r);
    }
    return result;
}

std::vector<DenialRow> UsageTracker::query_denials_24h() {
    if (!impl_) return {};
    auto rows = impl_->db.query(
        "SELECT feature, denials_24h FROM v_denial_rate_24h");

    std::vector<DenialRow> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        DenialRow r;
        r.feature     = col(row, "feature");
        r.denials_24h = col_int(row, "denials_24h");
        result.push_back(r);
    }
    return result;
}

std::vector<ActiveCheckout> UsageTracker::query_active_checkouts() {
    if (!impl_) return {};
    auto rows = impl_->db.query(
        "SELECT id, feature, username, client_host, backend_host, backend_port, "
        "       to_char(checked_out_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS checked_out_at, "
        "       duration_sec "
        "FROM v_active_checkouts");

    std::vector<ActiveCheckout> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        ActiveCheckout r;
        r.id             = col_int(row, "id");
        r.feature        = col(row, "feature");
        r.username       = col(row, "username");
        r.client_host    = col(row, "client_host");
        r.backend_host   = col(row, "backend_host");
        r.backend_port   = col_int(row, "backend_port");
        r.checked_out_at = col(row, "checked_out_at");
        r.duration_sec   = col_int(row, "duration_sec");
        result.push_back(r);
    }
    return result;
}

std::vector<ServerHealthRow> UsageTracker::query_health_events(int limit) {
    if (!impl_) return {};
    std::string lim = std::to_string(limit);
    const char* v[] = { lim.c_str() };
    auto rows = impl_->db.query_params(
        "SELECT s.host, s.port, h.event, "
        "       to_char(h.occurred_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS occurred_at "
        "FROM health_events h "
        "JOIN servers s ON s.id = h.server_id "
        "ORDER BY h.occurred_at DESC "
        "LIMIT $1",
        1, v);

    std::vector<ServerHealthRow> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        ServerHealthRow r;
        r.host        = col(row, "host");
        r.port        = col_int(row, "port");
        r.event       = col(row, "event");
        r.occurred_at = col(row, "occurred_at");
        result.push_back(r);
    }
    return result;
}

} // namespace tracker
