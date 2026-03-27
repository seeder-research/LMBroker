#pragma once
#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include "common/config.h"

namespace tracker {

enum class EventType {
    CHECKOUT,
    CHECKIN,
    DENIAL,
    SERVER_UP,
    SERVER_DOWN
};

struct UsageEvent {
    EventType   type{EventType::CHECKOUT};
    std::string feature;
    std::string user;
    std::string client_host;
    std::string backend_host;
    uint16_t    backend_port{0};
    long long   timestamp_ms{0}; // epoch ms; 0 = fill automatically
};

class UsageTracker {
public:
    explicit UsageTracker(const common::Config& cfg);
    ~UsageTracker();

    void start();
    void stop();

    // Thread-safe. Enqueues an event for async persistence.
    void record(UsageEvent event);

private:
    void flush_loop();
    void persist(const UsageEvent& ev);

    common::Config              cfg_;
    std::queue<UsageEvent>      queue_;
    std::mutex                  mtx_;
    std::condition_variable     cv_;
    std::thread                 worker_;
    std::atomic<bool>           running_{false};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tracker
