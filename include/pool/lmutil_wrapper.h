#pragma once
#include <string>
#include <vector>
#include "pool/pool_manager.h"

namespace pool {

// Result of a single lmstat invocation
struct LmstatResult {
    bool                      server_up{false}; // false = could not contact server
    std::string               error_msg;        // populated when server_up == false
    std::vector<FeatureCount> features;
};

class LmutilWrapper {
public:
    // Invokes: lmutil lmstat -a -c port@host
    // Always returns a result; check result.server_up for success.
    static LmstatResult lmstat(const std::string& host, uint16_t port);

    // Parse raw lmstat output text into an LmstatResult.
    // Exposed for unit testing without invoking lmutil.
    static LmstatResult parse_lmstat(const std::string& output);

    // Returns true if the output contains known server-down indicators
    static bool is_server_down(const std::string& output);
};

} // namespace pool
