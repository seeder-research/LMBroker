#include <gtest/gtest.h>
#include <cstdlib>
#include <thread>
#include <chrono>
#include "common/config.h"
#include "tracker/usage_tracker.h"

// All tests require TEST_DB_CONNSTR env var.
// Run migrations first: scripts/migrate.sh

class TrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* cs = std::getenv("TEST_DB_CONNSTR");
        if (!cs) GTEST_SKIP() << "TEST_DB_CONNSTR not set";
        cfg_.db_connstr = cs;
        tracker_ = std::make_shared<tracker::UsageTracker>(cfg_);
        tracker_->start();
    }
    void TearDown() override {
        if (tracker_) tracker_->stop();
    }
    // Helper: flush queue by sleeping slightly longer than the 2s flush interval
    void flush() { std::this_thread::sleep_for(std::chrono::seconds(3)); }

    common::Config                          cfg_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;
};

// ── CHECKOUT / CHECKIN round-trip ─────────────────────────────────────────────

TEST_F(TrackerTest, CheckoutAndCheckin) {
    tracker::UsageEvent co;
    co.type         = tracker::EventType::CHECKOUT;
    co.feature      = "TEST_FEAT_CI";
    co.user         = "testuser";
    co.client_host  = "testclient";
    co.backend_host = "backendhost";
    co.backend_port = 27000;
    tracker_->record(co);
    flush();

    auto checkouts = tracker_->query_active_checkouts();
    bool found = false;
    for (const auto& c : checkouts)
        if (c.feature == "TEST_FEAT_CI" && c.username == "testuser") { found = true; break; }
    EXPECT_TRUE(found) << "Checkout not found in active checkouts";

    // Now checkin
    tracker::UsageEvent ci;
    ci.type        = tracker::EventType::CHECKIN;
    ci.feature     = "TEST_FEAT_CI";
    ci.user        = "testuser";
    ci.client_host = "testclient";
    tracker_->record(ci);
    flush();

    checkouts = tracker_->query_active_checkouts();
    found = false;
    for (const auto& c : checkouts)
        if (c.feature == "TEST_FEAT_CI" && c.username == "testuser") { found = true; break; }
    EXPECT_FALSE(found) << "Checkout still open after checkin";
}

// ── DENIAL recorded ──────────────────────────────────────────────────────────

TEST_F(TrackerTest, DenialRecorded) {
    tracker::UsageEvent ev;
    ev.type          = tracker::EventType::DENIAL;
    ev.feature       = "TEST_FEAT_DENY";
    ev.user          = "denyuser";
    ev.client_host   = "denyclient";
    ev.denial_reason = "no licenses available";
    tracker_->record(ev);
    flush();

    auto denials = tracker_->query_denials_24h();
    bool found = false;
    for (const auto& d : denials)
        if (d.feature == "TEST_FEAT_DENY" && d.denials_24h > 0) { found = true; break; }
    EXPECT_TRUE(found) << "Denial not found in 24h denial query";
}

// ── FEATURE_POLL persisted ────────────────────────────────────────────────────

TEST_F(TrackerTest, FeaturePollPersisted) {
    tracker::UsageEvent ev;
    ev.type         = tracker::EventType::FEATURE_POLL;
    ev.feature      = "TEST_FEAT_POLL";
    ev.vendor       = "TESTVENDOR";
    ev.backend_host = "pollhost";
    ev.backend_port = 27000;
    ev.total        = 100;
    ev.in_use       = 42;
    ev.queued       = 3;
    tracker_->record(ev);
    flush();

    auto util = tracker_->query_utilisation();
    bool found = false;
    for (const auto& u : util) {
        if (u.feature == "TEST_FEAT_POLL") {
            found = true;
            EXPECT_EQ(u.total,   100);
            EXPECT_EQ(u.in_use,   42);
            EXPECT_EQ(u.queued,    3);
            EXPECT_EQ(u.available, 58);
        }
    }
    EXPECT_TRUE(found) << "Feature poll not visible in utilisation view";
}

// ── SERVER_UP / SERVER_DOWN health events ─────────────────────────────────────

TEST_F(TrackerTest, ServerHealthEventsRecorded) {
    tracker::UsageEvent down;
    down.type         = tracker::EventType::SERVER_DOWN;
    down.backend_host = "healthtesthost";
    down.backend_port = 27000;
    tracker_->record(down);

    tracker::UsageEvent up;
    up.type         = tracker::EventType::SERVER_UP;
    up.backend_host = "healthtesthost";
    up.backend_port = 27000;
    tracker_->record(up);
    flush();

    auto events = tracker_->query_health_events(10);
    int down_count = 0, up_count = 0;
    for (const auto& e : events) {
        if (e.host == "healthtesthost" && e.port == 27000) {
            if (e.event == "DOWN") down_count++;
            if (e.event == "UP")   up_count++;
        }
    }
    EXPECT_GE(down_count, 1);
    EXPECT_GE(up_count,   1);
}

// ── query_utilisation returns data ────────────────────────────────────────────

TEST_F(TrackerTest, QueryUtilisationDoesNotCrash) {
    auto rows = tracker_->query_utilisation();
    // Just checking it doesn't throw or crash — data may be empty on fresh DB
    SUCCEED();
}

// ── query_active_checkouts empty on fresh DB ──────────────────────────────────

TEST_F(TrackerTest, QueryActiveCheckoutsReturnsVector) {
    auto rows = tracker_->query_active_checkouts();
    // Type check — must be a vector<ActiveCheckout>
    EXPECT_GE(rows.size(), 0u);
}
