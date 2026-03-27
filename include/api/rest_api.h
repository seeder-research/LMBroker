#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "common/config.h"
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"

namespace api {

class RestApi {
public:
    RestApi(const common::Config& cfg,
            std::shared_ptr<pool::PoolManager> pool,
            std::shared_ptr<tracker::UsageTracker> tracker);
    ~RestApi();

    void start();
    void stop();

private:
    void serve();
    bool authenticate(const std::string& auth_header) const;

    common::Config                          cfg_;
    std::shared_ptr<pool::PoolManager>      pool_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;
    std::thread                             thread_;
    std::atomic<bool>                       running_{false};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace api
