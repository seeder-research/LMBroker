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
    int         total{0};
    int         in_use{0};
    int         available{0};
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
