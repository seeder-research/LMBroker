#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace common {

struct ServerEntry {
    std::string host;
    uint16_t    port{27000};
    std::string name;

    // Identity is host:port only — name changes don't trigger a remove/re-add
    bool same_endpoint(const ServerEntry& o) const {
        return host == o.host && port == o.port;
    }
    bool operator==(const ServerEntry& o) const {
        return host == o.host && port == o.port && name == o.name;
    }
    bool operator!=(const ServerEntry& o) const { return !(*this == o); }
};

struct Config {
    // Broker listener
    uint16_t    broker_port{27000};
    std::string broker_host{"0.0.0.0"};

    // REST API
    uint16_t    api_port{8080};
    std::string api_host{"0.0.0.0"};
    std::string api_token;

    // Backend pool
    std::vector<ServerEntry> servers;
    int poll_interval_sec{30};
    int failover_threshold{3};

    // PostgreSQL
    std::string db_connstr;

    // Logging
    std::string log_level{"info"};
    std::string log_file;

    static Config load(const std::string& path);
};

// Diff between two Config instances — drives hot reload logic
struct ConfigDiff {
    std::vector<ServerEntry> added;    // present in new_cfg, absent in old_cfg
    std::vector<ServerEntry> removed;  // present in old_cfg, absent in new_cfg

    bool poll_interval_changed{false};
    bool failover_threshold_changed{false};

    bool empty() const {
        return added.empty() && removed.empty()
            && !poll_interval_changed
            && !failover_threshold_changed;
    }
};

ConfigDiff diff_configs(const Config& old_cfg, const Config& new_cfg);

} // namespace common
