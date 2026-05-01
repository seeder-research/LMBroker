#include "common/logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace common {

std::shared_ptr<spdlog::logger> Logger::init(const Config& cfg) {
    std::shared_ptr<spdlog::logger> logger;
    if (cfg.log_file.empty()) {
        logger = spdlog::stdout_color_mt("broker");
    } else {
        namespace fs = std::filesystem;

        // Create parent directory if needed
        fs::path log_path(cfg.log_file);
        if (auto dir = log_path.parent_path(); !dir.empty() && !fs::exists(dir)) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            if (ec)
                throw std::runtime_error("cannot create log directory '"
                                         + dir.string() + "': " + ec.message());
        }

        // Create the log file if it doesn't exist
        if (!fs::exists(log_path)) {
            std::ofstream f(log_path);
            if (!f)
                throw std::runtime_error("cannot create log file '"
                                         + cfg.log_file + "'");
        }

        logger = spdlog::basic_logger_mt("broker", cfg.log_file);
    }
    logger->set_level(spdlog::level::from_str(cfg.log_level));
    spdlog::set_default_logger(logger);
    return logger;
}

} // namespace common
