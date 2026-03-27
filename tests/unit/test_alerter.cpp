#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "api/alerter.h"
#include "common/config.h"
#include "pool/pool_manager.h"

static common::Config make_alert_cfg(int cooldown = 1) {
    common::Config cfg;
    cfg.alerts.webhook_url          = "http://test-webhook.local/hook";
    cfg.alerts.cooldown_sec         = cooldown;
    cfg.alerts.denial_spike_threshold = 5;
    cfg.alerts.pool_exhaustion_pct  = 0.90f;
    return cfg;
}

static std::shared_ptr<pool::PoolManager> make_pool() {
    common::Config cfg;
    return std::make_shared<pool::PoolManager>(cfg, nullptr);
}

// ── Cooldown suppression ──────────────────────────────────────────────────────

TEST(Alerter, FirstAlertFires) {
    auto cfg  = make_alert_cfg(60);
    auto pool = make_pool();
    api::Alerter alerter(cfg, pool, nullptr);

    std::atomic<int> count{0};
    alerter.set_sender([&](const std::string&, const std::string&,
                            const std::string&) -> bool {
        count++;
        return true;
    });

    alerter.fire({api::AlertType::SERVER_DOWN, "h:27000",
                  "test", api::Alerter::now_iso_public()});
    EXPECT_EQ(count.load(), 1);
}

TEST(Alerter, SuppressedAfterCooldown) {
    auto cfg  = make_alert_cfg(60);
    auto pool = make_pool();
    api::Alerter alerter(cfg, pool, nullptr);

    std::atomic<int> count{0};
    alerter.set_sender([&](const std::string&, const std::string&,
                            const std::string&) -> bool {
        count++;
        return true;
    });
    alerter.start();

    // Manually inject a backend-down state twice quickly
    // The second should be suppressed by cooldown
    alerter.fire({api::AlertType::SERVER_DOWN, "h:27000", "msg1", ""});
    alerter.fire({api::AlertType::SERVER_DOWN, "h:27000", "msg2", ""});
    EXPECT_EQ(count.load(), 1); // second suppressed
    alerter.stop();
}

TEST(Alerter, DifferentSubjectsBothFire) {
    auto cfg  = make_alert_cfg(60);
    auto pool = make_pool();
    api::Alerter alerter(cfg, pool, nullptr);

    std::atomic<int> count{0};
    alerter.set_sender([&](const std::string&, const std::string&,
                            const std::string&) -> bool {
        count++;
        return true;
    });

    alerter.fire({api::AlertType::SERVER_DOWN, "host1:27000", "msg", ""});
    alerter.fire({api::AlertType::SERVER_DOWN, "host2:27000", "msg", ""});
    EXPECT_EQ(count.load(), 2);
}

TEST(Alerter, CooldownExpiresAndRefires) {
    auto cfg  = make_alert_cfg(0); // 0s cooldown
    auto pool = make_pool();
    api::Alerter alerter(cfg, pool, nullptr);

    std::atomic<int> count{0};
    alerter.set_sender([&](const std::string&, const std::string&,
                            const std::string&) -> bool {
        count++;
        return true;
    });

    alerter.fire({api::AlertType::SERVER_DOWN, "h:27000", "first", ""});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    alerter.fire({api::AlertType::SERVER_DOWN, "h:27000", "second", ""});
    EXPECT_EQ(count.load(), 2);
}

TEST(Alerter, SuppressedKeysReflectCooldown) {
    auto cfg  = make_alert_cfg(60);
    auto pool = make_pool();
    api::Alerter alerter(cfg, pool, nullptr);
    alerter.set_sender([](const std::string&, const std::string&,
                           const std::string&) { return true; });

    EXPECT_TRUE(alerter.suppressed_keys().empty());
    alerter.fire({api::AlertType::SERVER_DOWN, "h:27000", "msg", ""});
    auto keys = alerter.suppressed_keys();
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_NE(keys[0].find("SERVER_DOWN"), std::string::npos);
}

TEST(Alerter, WebhookPayloadIsValidJson) {
    auto cfg  = make_alert_cfg(60);
    auto pool = make_pool();
    api::Alerter alerter(cfg, pool, nullptr);

    std::string captured_body;
    alerter.set_sender([&](const std::string&, const std::string&,
                            const std::string& body) -> bool {
        captured_body = body;
        return true;
    });

    alerter.fire({api::AlertType::POOL_EXHAUSTED, "MATLAB",
                  "MATLAB at 98% utilisation", "2026-03-27T10:00:00Z"});

    ASSERT_FALSE(captured_body.empty());
    // Must be parseable JSON containing expected fields
    auto j = nlohmann::json::parse(captured_body, nullptr, false);
    ASSERT_FALSE(j.is_discarded());
    EXPECT_EQ(j["type"],    "POOL_EXHAUSTED");
    EXPECT_EQ(j["subject"], "MATLAB");
}
