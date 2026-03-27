#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace broker {

// Generates a synthetic FlexLM LICENSE file presented to clients.
// The SERVER line points to this broker; FEATURE lines are
// aggregated counts from the backend pool.
struct VirtualLicense {
    std::string broker_host;
    uint16_t    broker_port{27000};

    struct Feature {
        std::string name;
        std::string vendor;
        std::string version{"1.000"};
        int         count{0};
    };
    std::vector<Feature> features;

    // Returns a formatted LICENSE file string.
    std::string render() const;
};

} // namespace broker
