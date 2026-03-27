#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace common {

struct ServerEntry {
    std::string host;
    uint16_t    port{27000};
    std::string name;       // friendly label
};

struct Config {
    // Broker listener
    uint16_t    broker_port{27000};
    std::string broker_host{"0.0.0.0"};

    // REST API
    uint16_t    api_port{8080};
    std::string api_host{"0.0.0.0"};
    std::string api_token;  // Bearer token

    // Backend pool
    std::vector<ServerEntry> servers;
    int poll_interval_sec{30};
    int failover_threshold{3};  // consecutive failures before removal

    // PostgreSQL
    std::string db_connstr; // e.g. "host=localhost dbname=flexlm user=broker password=x"

    // Logging
    std::string log_level{"info"};
    std::string log_file;   // empty = stdout

    static Config load(const std::string& path);
};

} // namespace common
