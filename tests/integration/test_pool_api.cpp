#include <gtest/gtest.h>
#include <cstdlib>
#include <thread>
#include <chrono>
#include "common/config.h"
#include "tracker/usage_tracker.h"
#include "pool/pool_manager.h"
#include "api/rest_api.h"

// Integration test: spin up PoolManager + RestApi against a real (or mock)
// backend. Requires TEST_DB_CONNSTR. Backend servers are not required —
// the pool will simply report all backends as unhealthy, which is still
// a valid state to query via the API.

class PoolApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* cs = std::getenv("TEST_DB_CONNSTR");
        if (!cs) GTEST_SKIP() << "TEST_DB_CONNSTR not set";

        cfg_.db_connstr  = cs;
        cfg_.api_port    = 18080; // use non-standard port to avoid conflicts
        cfg_.api_token   = "";    // disable auth for tests
        cfg_.poll_interval_sec = 999; // don't poll during test

        tracker_ = std::make_shared<tracker::UsageTracker>(cfg_);
        pool_    = std::make_shared<pool::PoolManager>(cfg_, tracker_);
        api_     = std::make_shared<api::RestApi>(cfg_, pool_, tracker_);

        tracker_->start();
        pool_->start();
        api_->start();

        // Give server time to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (api_)     api_->stop();
        if (pool_)    pool_->stop();
        if (tracker_) tracker_->stop();
    }

    common::Config                          cfg_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;
    std::shared_ptr<pool::PoolManager>      pool_;
    std::shared_ptr<api::RestApi>           api_;
};

TEST_F(PoolApiTest, HealthEndpointReturns200) {
    // Use curl as a lightweight HTTP client in tests
    int rc = system("curl -sf http://localhost:18080/api/v1/health > /dev/null");
    EXPECT_EQ(rc, 0);
}

TEST_F(PoolApiTest, FeaturesEndpointReturnsJson) {
    int rc = system("curl -sf http://localhost:18080/api/v1/features > /dev/null");
    EXPECT_EQ(rc, 0);
}

TEST_F(PoolApiTest, ServersEndpointReturnsJson) {
    int rc = system("curl -sf http://localhost:18080/api/v1/servers > /dev/null");
    EXPECT_EQ(rc, 0);
}
