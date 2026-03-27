#include "health/health_monitor.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace health {

HealthMonitor::HealthMonitor(const common::Config& cfg,
                             std::shared_ptr<pool::PoolManager> pool)
    : cfg_(cfg), pool_(std::move(pool)) {}

HealthMonitor::~HealthMonitor() { stop(); }

void HealthMonitor::start() {
    running_ = true;
    thread_ = std::thread(&HealthMonitor::monitor_loop, this);
}

void HealthMonitor::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void HealthMonitor::monitor_loop() {
    while (running_) {
        int healthy = 0, unhealthy = 0;
        for (const auto& bs : pool_->backend_statuses()) {
            if (bs.healthy) {
                ++healthy;
            } else {
                ++unhealthy;
                spdlog::warn("[health] Backend {}:{} DOWN (streak={})",
                             bs.server.host, bs.server.port, bs.fail_streak);
            }
        }
        spdlog::debug("[health] Pool: {} healthy, {} unhealthy", healthy, unhealthy);

        for (int i = 0; i < cfg_.poll_interval_sec * 10 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace health
