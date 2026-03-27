#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include "common/config.h"
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"

namespace broker {

// Broker — TCP listener that presents a virtual lmgrd endpoint to clients.
//
// Phase 1 (scaffold): accepts connections and dispatches each to a worker
// thread. Full FlexLM protocol framing is implemented in Phase 4.
class Broker {
public:
    Broker(const common::Config& cfg,
           std::shared_ptr<pool::PoolManager> pool,
           std::shared_ptr<tracker::UsageTracker> tracker);
    ~Broker();

    void start();
    void stop();

private:
    void accept_loop();
    void handle_client(int fd);

    common::Config                          cfg_;
    std::shared_ptr<pool::PoolManager>      pool_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;
    std::thread                             accept_thread_;
    std::atomic<bool>                       running_{false};
    int                                     listen_fd_{-1};
};

} // namespace broker
