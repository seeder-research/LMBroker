#include "common/config_reloader.h"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <chrono>

namespace common {

ConfigReloader::ConfigReloader(const std::string& config_path,
                               const Config&      current_cfg,
                               ReloadCallback     on_reload,
                               int                watch_interval_sec)
    : config_path_(config_path),
      current_cfg_(current_cfg),
      on_reload_(std::move(on_reload)),
      watch_interval_sec_(watch_interval_sec),
      last_mtime_(file_mtime()) {}

ConfigReloader::~ConfigReloader() { stop(); }

void ConfigReloader::start() {
    running_      = true;
    watch_thread_ = std::thread(&ConfigReloader::watch_loop, this);
    spdlog::info("[reloader] Watching {} (interval={}s)",
                 config_path_, watch_interval_sec_);
}

void ConfigReloader::stop() {
    running_ = false;
    if (watch_thread_.joinable()) watch_thread_.join();
}

void ConfigReloader::trigger_reload() {
    reload_requested_.store(true, std::memory_order_relaxed);
}

Config ConfigReloader::current_config() const {
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    return current_cfg_;
}

// ── private ───────────────────────────────────────────────────────────────────

time_t ConfigReloader::file_mtime() const {
    struct stat st{};
    if (::stat(config_path_.c_str(), &st) == 0)
        return st.st_mtime;
    return 0;
}

bool ConfigReloader::do_reload() {
    Config new_cfg = Config::load(config_path_);

    Config old_cfg;
    {
        std::lock_guard<std::mutex> lk(cfg_mutex_);
        old_cfg = current_cfg_;
    }

    auto diff = diff_configs(old_cfg, new_cfg);
    if (diff.empty()) {
        spdlog::debug("[reloader] Config reloaded — no changes");
        return false;
    }

    spdlog::info("[reloader] Config changed: +{} servers, -{} servers{}{}",
                 diff.added.size(),
                 diff.removed.size(),
                 diff.poll_interval_changed    ? ", poll_interval changed" : "",
                 diff.failover_threshold_changed ? ", failover_threshold changed" : "");

    for (const auto& s : diff.added)
        spdlog::info("[reloader]   ADD  {}:{} ({})", s.host, s.port, s.name);
    for (const auto& s : diff.removed)
        spdlog::info("[reloader]   REMOVE {}:{} ({})", s.host, s.port, s.name);

    // Invoke callback (typically pool_->add_server / remove_server)
    on_reload_(new_cfg, diff);

    // Commit the new config atomically
    {
        std::lock_guard<std::mutex> lk(cfg_mutex_);
        current_cfg_ = new_cfg;
    }

    return true;
}

void ConfigReloader::watch_loop() {
    while (running_) {
        // Sleep in short increments to stay responsive to stop()
        for (int i = 0; i < watch_interval_sec_ * 10 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check for SIGHUP-triggered reload request
            if (reload_requested_.exchange(false, std::memory_order_relaxed)) {
                spdlog::info("[reloader] Reload triggered by signal");
                do_reload();
                last_mtime_ = file_mtime();
            }
        }
        if (!running_) break;

        // Mtime-based reload
        time_t mtime = file_mtime();
        if (mtime != 0 && mtime != last_mtime_) {
            spdlog::info("[reloader] Config file changed on disk — reloading");
            if (do_reload())
                last_mtime_ = mtime;
            else
                last_mtime_ = mtime; // update even if no diff (avoids re-checking)
        }
    }
}

} // namespace common
