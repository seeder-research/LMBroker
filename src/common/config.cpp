#include "common/config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <regex>

// Minimal INI parser.
// Sections: [broker], [api], [pool], [database], [logging], [server.N]
namespace common {

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    auto end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

} // anon

Config Config::load(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg;  // use defaults when file missing

    std::string section;
    std::string line;
    ServerEntry current_server;
    bool in_server_section = false;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            // Save previous server block
            if (in_server_section && !current_server.host.empty()) {
                cfg.servers.push_back(current_server);
                current_server = {};
            }
            section = trim(line.substr(1, line.size() - 2));
            in_server_section = (section.rfind("server.", 0) == 0);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(line.substr(0, eq));
        auto val = trim(line.substr(eq + 1));

        if (section == "broker") {
            if (key == "host") cfg.broker_host = val;
            else if (key == "port") cfg.broker_port = static_cast<uint16_t>(std::stoi(val));
        } else if (section == "api") {
            if (key == "host") cfg.api_host = val;
            else if (key == "port") cfg.api_port = static_cast<uint16_t>(std::stoi(val));
            else if (key == "token") cfg.api_token = val;
        } else if (section == "pool") {
            if (key == "poll_interval_sec") cfg.poll_interval_sec = std::stoi(val);
            else if (key == "failover_threshold") cfg.failover_threshold = std::stoi(val);
        } else if (section == "database") {
            if (key == "connstr") cfg.db_connstr = val;
        } else if (section == "logging") {
            if (key == "level") cfg.log_level = val;
            else if (key == "file") cfg.log_file = val;
        } else if (in_server_section) {
            if (key == "host") current_server.host = val;
            else if (key == "port") current_server.port = static_cast<uint16_t>(std::stoi(val));
            else if (key == "name") current_server.name = val;
        }
    }

    if (in_server_section && !current_server.host.empty())
        cfg.servers.push_back(current_server);

    return cfg;
}

} // namespace common
