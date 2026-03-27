#pragma once
#include <string>
#include <vector>
#include "pool/pool_manager.h"

namespace pool {

class LmutilWrapper {
public:
    // Invokes: lmutil lmstat -a -c port@host
    // Returns parsed feature list; empty vector on failure / timeout.
    static std::vector<FeatureCount> lmstat(const std::string& host,
                                            uint16_t port);

    // Parse raw lmstat output text into FeatureCount entries.
    // Exposed separately to allow unit testing without invoking lmutil.
    static std::vector<FeatureCount> parse_lmstat(const std::string& output);
};

} // namespace pool
