#include "pool/pool_manager.h"
#include "pool/lmutil_wrapper.h"
#include "tracker/usage_tracker.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <map>
#include <algorithm>

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
    while (running_) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& bs : backends_) poll_server(bs);
        }
        for (int i = 0; i < cfg_.poll_interval_sec * 10 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PoolManager::poll_server(BackendStatus& bs) {
    auto res = LmutilWrapper::lmstat(bs.server.host, bs.server.port);

    if (!res.server_up) {
        bs.fail_streak++;
        spdlog::debug("[pool] {}:{} poll failed (streak={}) — {}",
                      bs.server.host, bs.server.port,
                      bs.fail_streak, res.error_msg);
        if (bs.fail_streak >= cfg_.failover_threshold && bs.healthy) {
            bs.healthy = false;
            spdlog::warn("[pool] Backend {}:{} marked UNHEALTHY — {}",
                         bs.server.host, bs.server.port, res.error_msg);
            if (tracker_) {
                tracker::UsageEvent ev;
                ev.type         = tracker::EventType::SERVER_DOWN;
                ev.backend_host = bs.server.host;
                ev.backend_port = bs.server.port;
                tracker_->record(std::move(ev));
            }
        }
    } else {
        bool was_down  = !bs.healthy;
        bs.healthy     = true;
        bs.fail_streak = 0;
        bs.features    = res.features;

        spdlog::debug("[pool] {}:{} OK — {} features",
                      bs.server.host, bs.server.port, res.features.size());

        // Emit a FEATURE_POLL event for every feature on this backend
        if (tracker_) {
            for (const auto& f : res.features) {
                tracker::UsageEvent ev;
                ev.type         = tracker::EventType::FEATURE_POLL;
                ev.feature      = f.feature;
                ev.vendor       = f.vendor;
                ev.backend_host = bs.server.host;
                ev.backend_port = bs.server.port;
                ev.total        = f.total;
                ev.in_use       = f.in_use;
                ev.queued       = f.queued;
                tracker_->record(std::move(ev));
            }
        }

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

void PoolManager::add_server(const common::ServerEntry& server) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& bs : backends_)
        if (bs.server.host == server.host && bs.server.port == server.port) return;
    backends_.push_back({server, false, 0, {}});
    spdlog::info("[pool] Added backend {}:{} ({})",
                 server.host, server.port, server.name);
}

bool PoolManager::remove_server(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = std::remove_if(backends_.begin(), backends_.end(),
        [&](const BackendStatus& bs) {
            return bs.server.host == host && bs.server.port == port;
        });
    if (it == backends_.end()) return false;
    backends_.erase(it, backends_.end());
    spdlog::info("[pool] Removed backend {}:{}", host, port);
    return true;
}

void PoolManager::set_poll_interval(int seconds) {
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_.poll_interval_sec = seconds;
    spdlog::info("[pool] poll_interval updated to {}s", seconds);
}

void PoolManager::set_failover_threshold(int threshold) {
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_.failover_threshold = threshold;
    spdlog::info("[pool] failover_threshold updated to {}", threshold);
}

std::vector<FeatureCount> PoolManager::aggregated_features() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::map<std::string, FeatureCount> agg;
    for (const auto& bs : backends_) {
        if (!bs.healthy) continue;
        for (const auto& f : bs.features) {
            auto& a    = agg[f.feature];
            a.feature  = f.feature;
            a.vendor   = f.vendor;
            a.total   += f.total;
            a.in_use  += f.in_use;
            a.available += f.available;
            a.queued  += f.queued;
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
