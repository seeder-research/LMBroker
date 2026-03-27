#include "common/logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace common {

std::shared_ptr<spdlog::logger> Logger::init(const Config& cfg) {
    std::shared_ptr<spdlog::logger> logger;
    if (cfg.log_file.empty()) {
        logger = spdlog::stdout_color_mt("broker");
    } else {
        logger = spdlog::basic_logger_mt("broker", cfg.log_file);
    }
    logger->set_level(spdlog::level::from_str(cfg.log_level));
    spdlog::set_default_logger(logger);
    return logger;
}

} // namespace common
