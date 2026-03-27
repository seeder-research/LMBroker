#pragma once
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include "common/config.h"

namespace tracker { class UsageTracker; }

namespace pool {

struct FeatureCount {
    std::string feature;
    std::string vendor;     // vendor daemon name (from lmstat output)
    int         total{0};
    int         in_use{0};
    int         available{0};
    int         queued{0};  // licenses currently queued/waiting
    bool        uncounted{false}; // true = no seat limit (floating uncounted)
};

struct BackendStatus {
    common::ServerEntry       server;
    bool                      healthy{false};
    int                       fail_streak{0};
    std::vector<FeatureCount> features;
};

class PoolManager {
public:
    PoolManager(const common::Config& cfg,
                std::shared_ptr<tracker::UsageTracker> tracker);
    ~PoolManager();

    void start();
    void stop();

    // Dynamically add a backend server (safe to call while running)
    void add_server(const common::ServerEntry& server);

    // Remove a backend server by host:port (safe to call while running)
    bool remove_server(const std::string& host, uint16_t port);

    // Aggregated feature counts across all healthy backends
    std::vector<FeatureCount> aggregated_features() const;

    // Pick a healthy backend that has the requested feature available.
    // Returns nullptr if none available (caller must check).
    const common::ServerEntry* select_backend(const std::string& feature) const;

    std::vector<BackendStatus> backend_statuses() const;

private:
    void poll_loop();
    void poll_server(BackendStatus& bs);

    common::Config                          cfg_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;
    std::vector<BackendStatus>              backends_;
    mutable std::mutex                      mtx_;
    std::thread                             poll_thread_;
    std::atomic<bool>                       running_{false};
};

} // namespace pool
