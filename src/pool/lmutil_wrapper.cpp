#include "pool/lmutil_wrapper.h"
#include <cstdio>
#include <sstream>
#include <regex>
#include <spdlog/spdlog.h>

namespace pool {

std::vector<FeatureCount> LmutilWrapper::lmstat(const std::string& host,
                                                 uint16_t port) {
    std::string cmd = "lmutil lmstat -a -c "
                    + std::to_string(port) + "@" + host + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        spdlog::error("[lmutil] popen failed for {}:{}", host, port);
        return {};
    }
    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int rc = pclose(pipe);
    if (rc != 0) {
        spdlog::warn("[lmutil] lmstat exited {} for {}:{}", rc, host, port);
        return {};
    }
    return parse_lmstat(output);
}

// Parse lmstat -a output.
//
// Relevant lines look like:
//   Users of FEATURE_NAME:  (Total of N licenses issued;  M licenses in use)
//
// This regex handles variations in whitespace and capitalisation.
std::vector<FeatureCount> LmutilWrapper::parse_lmstat(const std::string& output) {
    std::vector<FeatureCount> result;

    // Match: "Users of <feature>:  (Total of <total> licenses issued;  <inuse> licenses in use)"
    static const std::regex re(
        R"(Users of (\S+):\s+\(Total of (\d+) licenses? issued;\s+(\d+) licenses? in use\))",
        std::regex::icase);

    auto begin = std::sregex_iterator(output.begin(), output.end(), re);
    auto end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const auto& m = *it;
        FeatureCount fc;
        fc.feature   = m[1].str();
        fc.total     = std::stoi(m[2].str());
        fc.in_use    = std::stoi(m[3].str());
        fc.available = fc.total - fc.in_use;
        result.push_back(fc);
    }
    return result;
}

} // namespace pool
