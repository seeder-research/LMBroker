#include "pool/lmutil_wrapper.h"
#include <cstdio>
#include <sstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <cerrno>
#include <cstring>
#include <sys/wait.h>

namespace pool {

// ── Server-down indicators ────────────────────────────────────────────────────

static const std::vector<std::string> kDownIndicators = {
    "Cannot connect to license server",
    "cannot connect to license server",
    "license server system does not support this feature",
    "The server (lmgrd) has not been started",
    "Connection refused",
    "License server machine is down",
    "Cannot find license file",
    "lmstat: Error",
    "-96,",
    "-15,",
};

bool LmutilWrapper::is_server_down(const std::string& output) {
    for (const auto& ind : kDownIndicators)
        if (output.find(ind) != std::string::npos) return true;
    return false;
}

// ── Parser ────────────────────────────────────────────────────────────────────

LmstatResult LmutilWrapper::parse_lmstat(const std::string& output) {
    LmstatResult result;

    if (output.empty()) {
        result.error_msg = "empty output";
        return result;
    }

    if (is_server_down(output)) {
        // Extract first non-blank line as the error message
        std::istringstream tmp(output);
        std::string line;
        while (std::getline(tmp, line)) {
            auto s = line.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) { result.error_msg = line.substr(s); break; }
        }
        return result; // server_up stays false
    }

    result.server_up = true;

    // Join continuation lines: "Users of FEAT:  (Total of N licenses\n issued; M ..."
    std::string normalised;
    {
        std::istringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto trimmed = line.substr(line.find_first_not_of(" \t") == std::string::npos
                                       ? 0 : line.find_first_not_of(" \t"));
            if (trimmed.rfind("issued;", 0) == 0 && !normalised.empty()) {
                if (normalised.back() == '\n') normalised.pop_back();
                normalised += ' ' + trimmed + '\n';
            } else {
                normalised += line + '\n';
            }
        }
    }

    // Regexes (compiled once per call — move to statics for hot paths)
    static const std::regex re_vendor_up(
        R"(^\s*(\w+):\s+UP\b)", std::regex::icase);
    static const std::regex re_uncounted(
        R"(Users of (\S+):\s+\(Uncounted\))", std::regex::icase);
    static const std::regex re_counted(
        R"(Users of (\S+):\s+\(Total of (\d+) licenses?\s+issued;\s+(\d+) licenses?\s+in use\))",
        std::regex::icase);
    static const std::regex re_queued(
        R"(\((\d+) licenses?\s+queued\))", std::regex::icase);
    static const std::regex re_feat_vendor(
        R"re("(\S+)"\s+v[\d.]+,\s+vendor:\s+(\w+))re", std::regex::icase);

    std::istringstream ss(normalised);
    std::string line;
    std::string current_vendor;
    FeatureCount* last = nullptr;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::smatch m;

        if (std::regex_search(line, m, re_vendor_up)) {
            current_vendor = m[1].str();
            last = nullptr;
            continue;
        }
        if (std::regex_search(line, m, re_uncounted)) {
            FeatureCount fc;
            fc.feature   = m[1].str();
            fc.vendor    = current_vendor;
            fc.uncounted = true;
            fc.total     = -1;
            fc.available = -1;
            result.features.push_back(fc);
            last = &result.features.back();
            continue;
        }
        if (std::regex_search(line, m, re_counted)) {
            FeatureCount fc;
            fc.feature   = m[1].str();
            fc.vendor    = current_vendor;
            fc.total     = std::stoi(m[2].str());
            fc.in_use    = std::stoi(m[3].str());
            fc.available = fc.total - fc.in_use;
            result.features.push_back(fc);
            last = &result.features.back();
            continue;
        }
        if (last && std::regex_search(line, m, re_queued)) {
            last->queued = std::stoi(m[1].str());
            continue;
        }
        if (last && std::regex_search(line, m, re_feat_vendor)) {
            // Always prefer explicit vendor from feature detail line over
            // daemon-section heuristic (handles multi-daemon blocks correctly)
            last->vendor = m[2].str();
        }
    }

    return result;
}

// ── lmutil invocation ─────────────────────────────────────────────────────────

LmstatResult LmutilWrapper::lmstat(const std::string& host, uint16_t port) {
    std::string cmd = "lmutil lmstat -a -c "
                    + std::to_string(port) + "@" + host + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        spdlog::error("[lmutil] popen failed for {}:{}: {}", host, port, strerror(errno));
        LmstatResult r; r.error_msg = "popen failed"; return r;
    }
    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int rc = pclose(pipe);
    if (WIFEXITED(rc) && WEXITSTATUS(rc) == 127) {
        spdlog::error("[lmutil] lmutil not found on PATH — cannot probe {}:{}", host, port);
        LmstatResult r; r.error_msg = "lmutil not found on PATH"; return r;
    }
    if (rc != 0)
        spdlog::debug("[lmutil] lmstat exit={} for {}:{}", WEXITSTATUS(rc), host, port);
    return parse_lmstat(output);
}

} // namespace pool
