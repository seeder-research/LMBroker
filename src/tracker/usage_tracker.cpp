#include "tracker/usage_tracker.h"
#include "tracker/db_connection.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>

namespace tracker {

// ── helpers ──────────────────────────────────────────────────────────────────

static long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch()).count();
}

// Convert epoch-ms to ISO-8601 string for PostgreSQL
static std::string ms_to_iso(long long ms) {
    std::time_t sec = ms / 1000;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&sec));
    return buf;
}

// ── Impl ─────────────────────────────────────────────────────────────────────

struct UsageTracker::Impl {
    DbConnection db;
    explicit Impl(const std::string& connstr) : db(connstr) {}
};

// ── public ───────────────────────────────────────────────────────────────────

UsageTracker::UsageTracker(const common::Config& cfg) : cfg_(cfg) {}

UsageTracker::~UsageTracker() { stop(); }

void UsageTracker::start() {
    impl_ = std::make_unique<Impl>(cfg_.db_connstr);
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

// ── private ──────────────────────────────────────────────────────────────────

void UsageTracker::flush_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::seconds(2),
                     [this]{ return !queue_.empty() || !running_; });
        while (!queue_.empty()) {
            auto ev = std::move(queue_.front());
            queue_.pop();
            lk.unlock();
            try { persist(ev); }
            catch (const std::exception& e) {
                spdlog::error("[tracker] persist failed: {}", e.what());
            }
            lk.lock();
        }
    }
}

void UsageTracker::persist(const UsageEvent& ev) {
    if (!impl_ || !impl_->db.is_connected()) return;

    std::string ts = ms_to_iso(ev.timestamp_ms);
    std::string port_str = std::to_string(ev.backend_port);

    switch (ev.type) {

    case EventType::CHECKOUT: {
        // Insert open checkout (checked_in_at is NULL until CHECKIN)
        const char* vals[] = {
            ev.feature.c_str(),
            ev.user.c_str(),
            ev.client_host.c_str(),
            ev.backend_host.c_str(),
            port_str.c_str(),
            ts.c_str()
        };
        impl_->db.execute_params(
            "INSERT INTO checkouts "
            "(feature, username, client_host, backend_host, backend_port, checked_out_at)"
            " VALUES ($1,$2,$3,$4,$5,$6)",
            6, vals);
        break;
    }

    case EventType::CHECKIN: {
        // Close the most recent open checkout for this user+feature
        const char* vals[] = {
            ts.c_str(),
            ev.feature.c_str(),
            ev.user.c_str(),
            ev.client_host.c_str()
        };
        impl_->db.execute_params(
            "UPDATE checkouts SET checked_in_at = $1 "
            "WHERE id = ("
            "  SELECT id FROM checkouts "
            "  WHERE feature=$2 AND username=$3 AND client_host=$4 "
            "    AND checked_in_at IS NULL "
            "  ORDER BY checked_out_at DESC LIMIT 1"
            ")",
            4, vals);
        break;
    }

    case EventType::DENIAL: {
        const char* vals[] = {
            ev.feature.c_str(),
            ev.user.c_str(),
            ev.client_host.c_str(),
            ts.c_str()
        };
        impl_->db.execute_params(
            "INSERT INTO denials (feature, username, client_host, denied_at)"
            " VALUES ($1,$2,$3,$4)",
            4, vals);
        break;
    }

    case EventType::SERVER_UP:
    case EventType::SERVER_DOWN: {
        const char* event_str =
            (ev.type == EventType::SERVER_UP) ? "UP" : "DOWN";
        // Upsert server then insert health event
        {
            const char* sv[] = { ev.backend_host.c_str(), port_str.c_str() };
            impl_->db.execute_params(
                "INSERT INTO servers (host, port) VALUES ($1,$2) "
                "ON CONFLICT (host, port) DO NOTHING",
                2, sv);
        }
        {
            const char* hv[] = {
                ev.backend_host.c_str(),
                port_str.c_str(),
                event_str,
                ts.c_str()
            };
            impl_->db.execute_params(
                "INSERT INTO health_events (server_id, event, occurred_at) "
                "VALUES ("
                "  (SELECT id FROM servers WHERE host=$1 AND port=$2),"
                "  $3, $4"
                ")",
                4, hv);
        }
        break;
    }

    } // switch
}

} // namespace tracker
