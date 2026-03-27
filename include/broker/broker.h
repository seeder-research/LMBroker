#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "common/config.h"
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"
#include "broker/thread_pool.h"

namespace broker {

class Broker {
public:
    Broker(const common::Config& cfg,
           std::shared_ptr<pool::PoolManager> pool,
           std::shared_ptr<tracker::UsageTracker> tracker);
    ~Broker();

    void start();
    void stop();

    size_t active_connections() const;

private:
    void accept_loop();

    common::Config                          cfg_;
    std::shared_ptr<pool::PoolManager>      pool_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;

    std::thread                             accept_thread_;
    std::atomic<bool>                       running_{false};
    int                                     listen_fd_{-1};

    std::unique_ptr<ThreadPool>             thread_pool_;
    std::atomic<size_t>                     active_connections_{0};

    static constexpr size_t kDefaultThreads  = 32;
    static constexpr size_t kDefaultMaxQueue = 2048;
};

} // namespace broker
