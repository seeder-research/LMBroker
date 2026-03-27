#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "common/config.h"
#include "pool/pool_manager.h"

namespace health {

class HealthMonitor {
public:
    HealthMonitor(const common::Config& cfg,
                  std::shared_ptr<pool::PoolManager> pool);
    ~HealthMonitor();
    void start();
    void stop();

private:
    void monitor_loop();

    common::Config                      cfg_;
    std::shared_ptr<pool::PoolManager>  pool_;
    std::thread                         thread_;
    std::atomic<bool>                   running_{false};
};

} // namespace health
