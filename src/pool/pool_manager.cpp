#include "pool/pool_manager.h"
#include "pool/lmutil_wrapper.h"
#include "tracker/usage_tracker.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <map>

namespace pool {

PoolManager::PoolManager(const common::Config& cfg,
                         std::shared_ptr<tracker::UsageTracker> tracker)
    : cfg_(cfg), tracker_(std::move(tracker)) {
    for (const auto& s : cfg_.servers)
        backends_.push_back({s, false, 0, {}});
}

PoolManager::~PoolManager() { stop(); }

void PoolManager::start() {
    running_ = true;
    poll_thread_ = std::thread(&PoolManager::poll_loop, this);
}

void PoolManager::stop() {
    running_ = false;
    if (poll_thread_.joinable()) poll_thread_.join();
}

void PoolManager::poll_loop() {
    // Poll immediately on start, then on interval
    while (running_) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& bs : backends_) poll_server(bs);
        }
        // Sleep in short increments so stop() is responsive
        for (int i = 0; i < cfg_.poll_interval_sec * 10 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PoolManager::poll_server(BackendStatus& bs) {
    auto features = LmutilWrapper::lmstat(bs.server.host, bs.server.port);

    if (features.empty()) {
        bs.fail_streak++;
        if (bs.fail_streak >= cfg_.failover_threshold && bs.healthy) {
            bs.healthy = false;
            spdlog::warn("[pool] Backend {}:{} marked UNHEALTHY (streak={})",
                         bs.server.host, bs.server.port, bs.fail_streak);
            // Record health event
            if (tracker_) {
                tracker::UsageEvent ev;
                ev.type         = tracker::EventType::SERVER_DOWN;
                ev.backend_host = bs.server.host;
                ev.backend_port = bs.server.port;
                tracker_->record(std::move(ev));
            }
        }
    } else {
        bool was_down = !bs.healthy;
        bs.healthy    = true;
        bs.fail_streak = 0;
        bs.features   = features;
        if (was_down) {
            spdlog::info("[pool] Backend {}:{} is back UP",
                         bs.server.host, bs.server.port);
            if (tracker_) {
                tracker::UsageEvent ev;
                ev.type         = tracker::EventType::SERVER_UP;
                ev.backend_host = bs.server.host;
                ev.backend_port = bs.server.port;
                tracker_->record(std::move(ev));
            }
        }
    }
}

std::vector<FeatureCount> PoolManager::aggregated_features() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::map<std::string, FeatureCount> agg;
    for (const auto& bs : backends_) {
        if (!bs.healthy) continue;
        for (const auto& f : bs.features) {
            auto& a    = agg[f.feature];
            a.feature  = f.feature;
            a.total   += f.total;
            a.in_use  += f.in_use;
            a.available += f.available;
        }
    }
    std::vector<FeatureCount> result;
    result.reserve(agg.size());
    for (auto& [k, v] : agg) result.push_back(v);
    return result;
}

const common::ServerEntry* PoolManager::select_backend(
        const std::string& feature) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& bs : backends_) {
        if (!bs.healthy) continue;
        for (const auto& f : bs.features)
            if (f.feature == feature && f.available > 0)
                return &bs.server;
    }
    return nullptr;
}

std::vector<BackendStatus> PoolManager::backend_statuses() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return backends_;
}

} // namespace pool
