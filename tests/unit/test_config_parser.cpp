#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "common/config.h"

static std::string write_temp_config(const std::string& content) {
    char path[] = "/tmp/broker_test_XXXXXX.conf";
    int fd = mkstemps(path, 5);
    write(fd, content.data(), content.size());
    close(fd);
    return path;
}

TEST(ConfigParser, LoadsServerEntries) {
    auto path = write_temp_config(R"(
[server.1]
host = srv1.example.com
port = 27000
name = primary

[server.2]
host = srv2.example.com
port = 27001
name = secondary
)");
    auto cfg = common::Config::load(path);
    std::remove(path.c_str());
    ASSERT_EQ(cfg.servers.size(), 2u);
    EXPECT_EQ(cfg.servers[0].host, "srv1.example.com");
    EXPECT_EQ(cfg.servers[1].port, 27001);
}

TEST(ConfigParser, LoadsBrokerAndApiSection) {
    auto path = write_temp_config(R"(
[broker]
port = 28000

[api]
port = 9090
token = mytoken
)");
    auto cfg = common::Config::load(path);
    std::remove(path.c_str());
    EXPECT_EQ(cfg.broker_port, 28000);
    EXPECT_EQ(cfg.api_port,    9090);
    EXPECT_EQ(cfg.api_token,   "mytoken");
}

TEST(ConfigParser, MissingFileReturnsDefaults) {
    auto cfg = common::Config::load("/nonexistent/path/broker.conf");
    EXPECT_EQ(cfg.broker_port, 27000);
    EXPECT_EQ(cfg.api_port,    8080);
    EXPECT_TRUE(cfg.servers.empty());
}
