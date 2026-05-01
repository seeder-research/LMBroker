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
    std::string vendor;
    int         total{0};
    int         in_use{0};
    int         available{0};
    int         queued{0};
    bool        uncounted{false};
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

    // Dynamic pool management — safe to call while running
    void add_server(const common::ServerEntry& server);
    bool remove_server(const std::string& host, uint16_t port);

    // Runtime tuning — safe to call while running
    void set_poll_interval(int seconds);
    void set_failover_threshold(int threshold);

    // Aggregated feature counts across all healthy backends
    std::vector<FeatureCount> aggregated_features() const;

    // Pick a healthy backend that has at least `count` seats of `feature`
    // available. Backends in `excluded` (as "host:port" strings) are skipped.
    // Returns nullptr if no suitable backend exists.
    const common::ServerEntry* select_backend(
            const std::string& feature,
            int count = 1,
            const std::vector<std::string>& excluded = {}) const;

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
