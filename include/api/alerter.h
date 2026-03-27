#pragma once
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>
#include "common/config.h"
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"

namespace api {

enum class AlertType {
    SERVER_DOWN,
    SERVER_UP,
    POOL_EXHAUSTED,     // feature utilisation >= exhaustion_pct
    POOL_RECOVERED,     // feature utilisation back below threshold
    DENIAL_SPIKE,       // denials/min exceeded threshold
};

inline const char* alert_type_name(AlertType t) {
    switch (t) {
    case AlertType::SERVER_DOWN:     return "SERVER_DOWN";
    case AlertType::SERVER_UP:       return "SERVER_UP";
    case AlertType::POOL_EXHAUSTED:  return "POOL_EXHAUSTED";
    case AlertType::POOL_RECOVERED:  return "POOL_RECOVERED";
    case AlertType::DENIAL_SPIKE:    return "DENIAL_SPIKE";
    }
    return "UNKNOWN";
}

struct Alert {
    AlertType   type;
    std::string subject;   // e.g. "licserver1:27000" or "MATLAB"
    std::string message;
    std::string timestamp; // ISO-8601
};

// Alerter watches pool and tracker state, fires webhook POSTs when
// thresholds are crossed, and enforces per-alert-type cooldown.
class Alerter {
public:
    Alerter(const common::Config&                    cfg,
            std::shared_ptr<pool::PoolManager>        pool,
            std::shared_ptr<tracker::UsageTracker>    tracker);
    ~Alerter();

    void start();
    void stop();

    // Inject a custom sender for testing (default: HTTP POST via curl subprocess)
    using SendFn = std::function<bool(const std::string& url,
                                      const std::string& secret,
                                      const std::string& body)>;
    void set_sender(SendFn fn) { sender_ = std::move(fn); }

    // Force-fire an alert (bypasses cooldown — used by admin endpoints)
    void fire(const Alert& alert);

    // Returns a copy of all currently suppressed alert keys (for admin API)
    std::vector<std::string> suppressed_keys() const;

public:
    // Exposed for testing
    static std::string now_iso_public() { return now_iso(); }

private:
    void watch_loop();
    void check_backends();
    void check_pool_exhaustion();
    void check_denial_spike();

    // Returns true if the alert was fired (not suppressed by cooldown)
    bool maybe_fire(AlertType type, const std::string& subject,
                    const std::string& message);

    // Cooldown key = "TYPE:subject"
    std::string cooldown_key(AlertType type, const std::string& subject) const;
    bool        in_cooldown(const std::string& key) const;
    void        set_cooldown(const std::string& key);

    static bool default_send(const std::string& url,
                              const std::string& secret,
                              const std::string& body);
    static std::string make_payload(const Alert& alert);
    static std::string now_iso();

    common::Config                          cfg_;
    std::shared_ptr<pool::PoolManager>      pool_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;

    std::thread          thread_;
    std::atomic<bool>    running_{false};

    // Cooldown table: key → time when cooldown expires
    mutable std::mutex                                         cooldown_mtx_;
    std::map<std::string, std::chrono::steady_clock::time_point> cooldown_;

    // Track previous backend health to detect transitions
    std::map<std::string, bool> prev_health_;

    // Track previous exhaustion state per feature
    std::map<std::string, bool> prev_exhausted_;

    SendFn sender_{default_send};
};

} // namespace api
