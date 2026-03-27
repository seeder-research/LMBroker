#include "broker/virtual_license.h"
#include <sstream>
#include <iomanip>

namespace broker {

std::string VirtualLicense::render() const {
    std::ostringstream oss;

    // SERVER line — clients connect here (broker acts as lmgrd)
    oss << "SERVER " << broker_host << " ANY " << broker_port << "\n";
    oss << "VENDOR flexlm_broker\n\n";

    // One FEATURE line per aggregated entry.
    // The SIGN value is a placeholder — real FlexLM signatures are
    // cryptographically bound to the vendor daemon; in a full
    // implementation the vendor daemon generates these.
    for (const auto& f : features) {
        oss << "FEATURE " << f.name
            << " "  << f.vendor
            << " "  << f.version
            << " permanent " << f.count
            << " SIGN=000000000000\n";
    }
    return oss.str();
}

} // namespace broker
