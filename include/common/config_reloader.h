#pragma once
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include "config.h"

namespace common {

// ConfigReloader watches a config file for changes and applies diffs
// to a live PoolManager without restarting the process.
//
// Two reload triggers are supported — both can be active simultaneously:
//   1. SIGHUP  — process receives SIGHUP; main() calls trigger_reload()
//   2. Mtime   — background thread polls the file's mtime every watch_interval_sec
//
// On reload, the registered callback receives the diff.
// The callback is always invoked from the watcher thread (not the signal handler).
class ConfigReloader {
public:
    using ReloadCallback = std::function<void(const Config& new_cfg,
                                              const ConfigDiff& diff)>;

    ConfigReloader(const std::string&   config_path,
                   const Config&        current_cfg,
                   ReloadCallback       on_reload,
                   int                  watch_interval_sec = 10);
    ~ConfigReloader();

    void start();
    void stop();

    // Call from SIGHUP handler (or anywhere): schedules an immediate reload
    // on the next watcher loop iteration. Signal-safe (atomic store only).
    void trigger_reload();

    // Returns a copy of the currently active config (thread-safe)
    Config current_config() const;

private:
    void watch_loop();
    bool do_reload();                  // returns true if diff was non-empty
    time_t file_mtime() const;

    std::string      config_path_;
    Config           current_cfg_;
    ReloadCallback   on_reload_;
    int              watch_interval_sec_;

    mutable std::mutex   cfg_mutex_;
    std::atomic<bool>    reload_requested_{false};
    std::atomic<bool>    running_{false};
    std::thread          watch_thread_;
    time_t               last_mtime_{0};
};

} // namespace common
