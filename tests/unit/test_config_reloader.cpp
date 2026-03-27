#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include "common/config_reloader.h"

static std::string write_config(const std::string& content) {
    char path[] = "/tmp/broker_reloader_XXXXXX.conf";
    int fd = mkstemps(path, 5);
    write(fd, content.data(), content.size());
    close(fd);
    return path;
}

static void overwrite_config(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::trunc);
    f << content;
}

static const char* kBase = R"(
[server.1]
host = srv1.example.com
port = 27000
name = primary

[pool]
poll_interval_sec  = 30
failover_threshold = 3
)";

static const char* kAddServer = R"(
[server.1]
host = srv1.example.com
port = 27000
name = primary

[server.2]
host = srv2.example.com
port = 27000
name = secondary

[pool]
poll_interval_sec  = 30
failover_threshold = 3
)";

static const char* kChangedInterval = R"(
[server.1]
host = srv1.example.com
port = 27000
name = primary

[pool]
poll_interval_sec  = 60
failover_threshold = 3
)";

// ── trigger_reload picks up added server ──────────────────────────────────────

TEST(ConfigReloader, TriggerReloadDetectsAddedServer) {
    auto path = write_config(kBase);

    std::atomic<int>  callback_count{0};
    std::vector<common::ServerEntry> added_servers;
    std::mutex mtx;

    auto base_cfg = common::Config::load(path);
    common::ConfigReloader reloader(path, base_cfg,
        [&](const common::Config&, const common::ConfigDiff& diff) {
            std::lock_guard<std::mutex> lk(mtx);
            callback_count++;
            for (const auto& s : diff.added) added_servers.push_back(s);
        },
        /* watch_interval_sec= */ 60 // long — rely on trigger_reload()
    );
    reloader.start();

    // Modify the file, then signal a reload
    overwrite_config(path, kAddServer);
    reloader.trigger_reload();

    // Give the watcher thread time to process (it checks every 100ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    reloader.stop();

    std::remove(path.c_str());

    EXPECT_EQ(callback_count.load(), 1);
    ASSERT_EQ(added_servers.size(), 1u);
    EXPECT_EQ(added_servers[0].host, "srv2.example.com");
}

// ── mtime watcher fires automatically ─────────────────────────────────────────

TEST(ConfigReloader, MtimeWatcherDetectsChange) {
    auto path = write_config(kBase);

    std::atomic<int> callback_count{0};

    auto base_cfg = common::Config::load(path);
    common::ConfigReloader reloader(path, base_cfg,
        [&](const common::Config&, const common::ConfigDiff&) {
            callback_count++;
        },
        /* watch_interval_sec= */ 1 // short for test
    );
    reloader.start();

    // Wait past one interval, then change the file
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    overwrite_config(path, kAddServer);

    // Wait for the watcher to pick it up
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    reloader.stop();

    std::remove(path.c_str());
    EXPECT_GE(callback_count.load(), 1);
}

// ── no callback when file unchanged ──────────────────────────────────────────

TEST(ConfigReloader, NoCallbackWhenUnchanged) {
    auto path = write_config(kBase);

    std::atomic<int> callback_count{0};
    auto base_cfg = common::Config::load(path);
    common::ConfigReloader reloader(path, base_cfg,
        [&](const common::Config&, const common::ConfigDiff&) { callback_count++; },
        1);
    reloader.start();

    // trigger_reload with no file change → diff is empty → no callback
    reloader.trigger_reload();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    reloader.stop();

    std::remove(path.c_str());
    EXPECT_EQ(callback_count.load(), 0);
}

// ── poll interval change propagated ──────────────────────────────────────────

TEST(ConfigReloader, PollIntervalChangePropagated) {
    auto path = write_config(kBase);

    std::atomic<bool> interval_changed{false};
    auto base_cfg = common::Config::load(path);
    common::ConfigReloader reloader(path, base_cfg,
        [&](const common::Config& new_cfg, const common::ConfigDiff& diff) {
            if (diff.poll_interval_changed)
                interval_changed = true;
        },
        60);
    reloader.start();

    overwrite_config(path, kChangedInterval);
    reloader.trigger_reload();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    reloader.stop();

    std::remove(path.c_str());
    EXPECT_TRUE(interval_changed.load());
}

// ── current_config() reflects latest ─────────────────────────────────────────

TEST(ConfigReloader, CurrentConfigReflectsLatest) {
    auto path = write_config(kBase);

    auto base_cfg = common::Config::load(path);
    common::ConfigReloader reloader(path, base_cfg,
        [](const common::Config&, const common::ConfigDiff&) {},
        60);
    reloader.start();

    EXPECT_EQ(reloader.current_config().servers.size(), 1u);

    overwrite_config(path, kAddServer);
    reloader.trigger_reload();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    reloader.stop();

    std::remove(path.c_str());
    EXPECT_EQ(reloader.current_config().servers.size(), 2u);
}
