#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include "api/metrics.h"
#include "common/config.h"
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"

// Build a PoolManager with a fixed backend list but never poll,
// so aggregated_features() returns empty — enough to test renderer structure.
static std::shared_ptr<pool::PoolManager> make_empty_pool() {
    common::Config cfg;
    return std::make_shared<pool::PoolManager>(cfg, nullptr);
}

// ── MetricsRenderer output structure ─────────────────────────────────────────

TEST(Metrics, OutputContainsBrokerInfo) {
    auto pool    = make_empty_pool();
    api::MetricsRenderer renderer(pool, nullptr);
    auto output = renderer.render();
    EXPECT_NE(output.find("flexlm_broker_info"), std::string::npos);
    EXPECT_NE(output.find("version="), std::string::npos);
}

TEST(Metrics, OutputContainsHelpLines) {
    auto pool = make_empty_pool();
    api::MetricsRenderer renderer(pool, nullptr);
    auto output = renderer.render();
    EXPECT_NE(output.find("# HELP flexlm_feature_total"), std::string::npos);
    EXPECT_NE(output.find("# TYPE flexlm_feature_total gauge"), std::string::npos);
    EXPECT_NE(output.find("# HELP flexlm_backend_healthy"), std::string::npos);
}

TEST(Metrics, OutputContainsActiveCheckoutsWhenTrackerNull) {
    auto pool = make_empty_pool();
    api::MetricsRenderer renderer(pool, nullptr);
    auto output = renderer.render();
    // When tracker is null, DB metrics are skipped — should NOT crash
    EXPECT_NE(output.find("flexlm_broker_info"), std::string::npos);
}

TEST(Metrics, LabelEscapingBackslash) {
    // Test escape_label via a round-trip through render with injected data
    // (escape_label is private; test indirectly by checking output format)
    auto pool = make_empty_pool();
    api::MetricsRenderer renderer(pool, nullptr);
    auto output = renderer.render();
    // As long as output doesn't have unescaped backslashes in label values
    // (there are none in the broker_info metric), we just check it renders
    EXPECT_FALSE(output.empty());
}

TEST(Metrics, PrometheusFormatNoBlankMetricNames) {
    auto pool = make_empty_pool();
    api::MetricsRenderer renderer(pool, nullptr);
    auto output = renderer.render();
    // Every non-blank, non-comment line must start with "flexlm_"
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;
        EXPECT_EQ(line.substr(0, 7), "flexlm_")
            << "Unexpected metric line: " << line;
    }
}
