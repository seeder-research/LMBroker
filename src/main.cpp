#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include "common/config.h"
#include "common/config_reloader.h"
#include "common/logger.h"
#include <spdlog/spdlog.h>
#include "pool/pool_manager.h"
#include "health/health_monitor.h"
#include "tracker/usage_tracker.h"
#include "api/rest_api.h"
#include "api/alerter.h"
#include "broker/broker.h"

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_reload_requested{false};

void on_sigterm(int) { g_running = false; }
void on_sighup(int)  { g_reload_requested = true; }

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  on_sigterm);
    std::signal(SIGTERM, on_sigterm);
    std::signal(SIGHUP,  on_sighup);

    const std::string config_path =
        (argc > 1) ? argv[1] : "/etc/flexlm-broker/broker.conf";

    auto cfg     = common::Config::load(config_path);
    auto logger  = common::Logger::init(cfg);
    auto tracker = std::make_shared<tracker::UsageTracker>(cfg);
    auto pool    = std::make_shared<pool::PoolManager>(cfg, tracker);
    auto health  = std::make_shared<health::HealthMonitor>(cfg, pool);
    auto alerter  = std::make_shared<api::Alerter>(cfg, pool, tracker);
    auto api      = std::make_shared<api::RestApi>(cfg, pool, tracker, alerter);
    auto broker  = std::make_shared<broker::Broker>(cfg, pool, tracker);

    // ConfigReloader: applies diffs to the live pool on SIGHUP or file-mtime change
    auto reloader = std::make_shared<common::ConfigReloader>(
        config_path, cfg,
        [&pool](const common::Config& new_cfg, const common::ConfigDiff& diff) {
            for (const auto& s : diff.added)
                pool->add_server(s);
            for (const auto& s : diff.removed)
                pool->remove_server(s.host, s.port);
            if (diff.poll_interval_changed)
                pool->set_poll_interval(new_cfg.poll_interval_sec);
            if (diff.failover_threshold_changed)
                pool->set_failover_threshold(new_cfg.failover_threshold);
        },
        /* watch_interval_sec = */ 15
    );

    tracker->start();
    pool->start();
    health->start();
    alerter->start();
    api->start();
    broker->start();
    reloader->start();

    logger->info("flexlm-broker started (pid {})", getpid());
    logger->info("Send SIGHUP or edit {} to reload config", config_path);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Forward SIGHUP to the reloader (signal handler only sets atomic flag)
        if (g_reload_requested.exchange(false)) {
            reloader->trigger_reload();
        }
    }

    logger->info("Shutting down...");
    reloader->stop();
    broker->stop();
    api->stop();
    alerter->stop();
    health->stop();
    pool->stop();
    tracker->stop();
    return 0;
}
