#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include "common/config.h"
#include "common/logger.h"
#include "pool/pool_manager.h"
#include "health/health_monitor.h"
#include "tracker/usage_tracker.h"
#include "api/rest_api.h"
#include "broker/broker.h"

std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    const std::string config_path =
        (argc > 1) ? argv[1] : "/etc/flexlm-broker/broker.conf";

    auto cfg     = common::Config::load(config_path);
    auto logger  = common::Logger::init(cfg);
    auto tracker = std::make_shared<tracker::UsageTracker>(cfg);
    auto pool    = std::make_shared<pool::PoolManager>(cfg, tracker);
    auto health  = std::make_shared<health::HealthMonitor>(cfg, pool);
    auto api     = std::make_shared<api::RestApi>(cfg, pool, tracker);
    auto broker  = std::make_shared<broker::Broker>(cfg, pool, tracker);

    tracker->start();
    pool->start();
    health->start();
    api->start();
    broker->start();

    logger->info("flexlm-broker started (pid {})", getpid());

    while (g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    logger->info("Shutting down...");
    broker->stop();
    api->stop();
    health->stop();
    pool->stop();
    tracker->stop();
    return 0;
}
