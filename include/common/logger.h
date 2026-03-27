#pragma once
#include <memory>
#include "config.h"

namespace spdlog { class logger; }

namespace common {

struct Logger {
    static std::shared_ptr<spdlog::logger> init(const Config& cfg);
};

} // namespace common
