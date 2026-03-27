#include <gtest/gtest.h>
#include "common/config.h"

using common::Config;
using common::ServerEntry;
using common::ConfigDiff;
using common::diff_configs;

static ServerEntry make_srv(const std::string& host, uint16_t port,
                             const std::string& name = "") {
    ServerEntry s;
    s.host = host; s.port = port; s.name = name;
    return s;
}

// ── diff_configs ──────────────────────────────────────────────────────────────

TEST(ConfigDiff, NoDiffWhenIdentical) {
    Config a, b;
    a.servers = { make_srv("h1", 27000), make_srv("h2", 27000) };
    b.servers = a.servers;
    auto diff = diff_configs(a, b);
    EXPECT_TRUE(diff.empty());
}

TEST(ConfigDiff, DetectsAddedServer) {
    Config a, b;
    a.servers = { make_srv("h1", 27000) };
    b.servers = { make_srv("h1", 27000), make_srv("h2", 27001) };
    auto diff = diff_configs(a, b);
    ASSERT_EQ(diff.added.size(), 1u);
    EXPECT_EQ(diff.added[0].host, "h2");
    EXPECT_EQ(diff.added[0].port, 27001);
    EXPECT_TRUE(diff.removed.empty());
}

TEST(ConfigDiff, DetectsRemovedServer) {
    Config a, b;
    a.servers = { make_srv("h1", 27000), make_srv("h2", 27000) };
    b.servers = { make_srv("h1", 27000) };
    auto diff = diff_configs(a, b);
    ASSERT_EQ(diff.removed.size(), 1u);
    EXPECT_EQ(diff.removed[0].host, "h2");
    EXPECT_TRUE(diff.added.empty());
}

TEST(ConfigDiff, DetectsAddAndRemoveSimultaneously) {
    Config a, b;
    a.servers = { make_srv("old", 27000) };
    b.servers = { make_srv("new", 27000) };
    auto diff = diff_configs(a, b);
    EXPECT_EQ(diff.added.size(),   1u);
    EXPECT_EQ(diff.removed.size(), 1u);
    EXPECT_EQ(diff.added[0].host,   "new");
    EXPECT_EQ(diff.removed[0].host, "old");
}

TEST(ConfigDiff, NameChangeAloneDoesNotTriggerDiff) {
    // same_endpoint() uses host:port only, so a name rename is not a server change
    Config a, b;
    a.servers = { make_srv("h1", 27000, "primary") };
    b.servers = { make_srv("h1", 27000, "renamed") };
    auto diff = diff_configs(a, b);
    // The server is at the same host:port, so no add/remove
    EXPECT_TRUE(diff.added.empty());
    EXPECT_TRUE(diff.removed.empty());
}

TEST(ConfigDiff, DetectsPollIntervalChange) {
    Config a, b;
    a.poll_interval_sec = 30;
    b.poll_interval_sec = 60;
    auto diff = diff_configs(a, b);
    EXPECT_TRUE(diff.poll_interval_changed);
    EXPECT_FALSE(diff.failover_threshold_changed);
}

TEST(ConfigDiff, DetectsFailoverThresholdChange) {
    Config a, b;
    a.failover_threshold = 3;
    b.failover_threshold = 5;
    auto diff = diff_configs(a, b);
    EXPECT_TRUE(diff.failover_threshold_changed);
}

TEST(ConfigDiff, EmptyToNonEmpty) {
    Config a, b;
    b.servers = { make_srv("h1", 27000), make_srv("h2", 27000) };
    auto diff = diff_configs(a, b);
    EXPECT_EQ(diff.added.size(), 2u);
    EXPECT_TRUE(diff.removed.empty());
}

TEST(ConfigDiff, NonEmptyToEmpty) {
    Config a, b;
    a.servers = { make_srv("h1", 27000), make_srv("h2", 27000) };
    auto diff = diff_configs(a, b);
    EXPECT_EQ(diff.removed.size(), 2u);
    EXPECT_TRUE(diff.added.empty());
}

TEST(ConfigDiff, LargePoolPartialChange) {
    Config a, b;
    for (int i = 0; i < 100; ++i)
        a.servers.push_back(make_srv("host" + std::to_string(i), 27000));
    b.servers = a.servers;
    // Remove first 10, add 5 new ones
    b.servers.erase(b.servers.begin(), b.servers.begin() + 10);
    for (int i = 100; i < 105; ++i)
        b.servers.push_back(make_srv("host" + std::to_string(i), 27000));
    auto diff = diff_configs(a, b);
    EXPECT_EQ(diff.removed.size(), 10u);
    EXPECT_EQ(diff.added.size(),    5u);
}

// ── ServerEntry equality ──────────────────────────────────────────────────────

TEST(ServerEntry, EqualityByHostPortAndName) {
    auto s1 = make_srv("h", 27000, "primary");
    auto s2 = make_srv("h", 27000, "primary");
    auto s3 = make_srv("h", 27000, "secondary");
    EXPECT_EQ(s1, s2);
    EXPECT_NE(s1, s3);
}

TEST(ServerEntry, SameEndpointIgnoresName) {
    auto s1 = make_srv("h", 27000, "primary");
    auto s2 = make_srv("h", 27000, "renamed");
    EXPECT_TRUE(s1.same_endpoint(s2));
}

TEST(ServerEntry, SameEndpointDifferentPort) {
    auto s1 = make_srv("h", 27000);
    auto s2 = make_srv("h", 27001);
    EXPECT_FALSE(s1.same_endpoint(s2));
}
