#pragma once
#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <vector>
#include "common/config.h"

namespace tracker {

enum class EventType {
    CHECKOUT,
    CHECKIN,
    DENIAL,
    SERVER_UP,
    SERVER_DOWN,
    FEATURE_POLL  // periodic snapshot of feature counts from a backend
};

struct UsageEvent {
    EventType   type{EventType::CHECKOUT};
    std::string feature;
    std::string vendor;
    std::string user;
    std::string client_host;
    std::string backend_host;
    uint16_t    backend_port{0};
    int         total{0};       // used by FEATURE_POLL
    int         in_use{0};      // used by FEATURE_POLL
    int         queued{0};      // used by FEATURE_POLL and DENIAL
    std::string denial_reason;  // used by DENIAL
    long long   timestamp_ms{0};
};

// ── Query result structs (returned by UsageTracker query methods) ─────────────

struct UtilisationRow {
    std::string feature;
    int         total{0};
    int         in_use{0};
    int         available{0};
    int         queued{0};
    std::string last_polled;
};

struct DenialRow {
    std::string feature;
    long long   denials_24h{0};
};

struct ActiveCheckout {
    long long   id{0};
    std::string feature;
    std::string username;
    std::string client_host;
    std::string backend_host;
    int         backend_port{0};
    std::string checked_out_at;
    int         duration_sec{0};
};

struct ServerHealthRow {
    std::string host;
    int         port{0};
    std::string event;       // "UP" or "DOWN"
    std::string occurred_at;
};

// ── UsageTracker ──────────────────────────────────────────────────────────────

class UsageTracker {
public:
    explicit UsageTracker(const common::Config& cfg);
    ~UsageTracker();

    void start();
    void stop();

    // Enqueue an event for async persistence (thread-safe, non-blocking)
    void record(UsageEvent event);

    // ── Synchronous query methods (call from REST API handlers) ─────────

    // Current aggregated licence utilisation (from v_license_utilisation)
    std::vector<UtilisationRow> query_utilisation();

    // Denial counts per feature over last 24 h (from v_denial_rate_24h)
    std::vector<DenialRow> query_denials_24h();

    // Currently open checkouts (from v_active_checkouts)
    std::vector<ActiveCheckout> query_active_checkouts();

    // Last N health events across all servers
    std::vector<ServerHealthRow> query_health_events(int limit = 50);

private:
    void flush_loop();
    void persist(const UsageEvent& ev);
    void persist_feature_poll(const UsageEvent& ev);
    void persist_server_event(const UsageEvent& ev);

    common::Config              cfg_;
    std::queue<UsageEvent>      queue_;
    std::mutex                  mtx_;
    std::condition_variable     cv_;
    std::thread                 worker_;
    std::atomic<bool>           running_{false};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tracker
