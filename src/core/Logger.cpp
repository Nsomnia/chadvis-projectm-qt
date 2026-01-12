#include "Logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include "util/FileUtils.hpp"

namespace vc {

std::shared_ptr<spdlog::logger> Logger::logger_;

void Logger::init(std::string_view appName, bool debug) {
    try {
        spdlog::drop(std::string(appName));

        std::vector<spdlog::sink_ptr> sinks;

        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_pattern("%^[%H:%M:%S.%e] [%l]%$ %v");
        sinks.push_back(console);

        auto logDir = file::cacheDir() / "logs";
        file::ensureDir(logDir);

        auto logFile = logDir / (std::string(appName) + ".log");
        auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFile.string(), 1024 * 1024 * 5, 3);
        file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        sinks.push_back(file);

        logger_ = std::make_shared<spdlog::logger>(
                std::string(appName), sinks.begin(), sinks.end());

        auto level = debug ? spdlog::level::debug : spdlog::level::info;
        logger_->set_level(level);
        logger_->flush_on(level);

        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);

        LOG_INFO("Logger initialized. Debug mode: {}", debug);
        LOG_DEBUG("Log file: {}", logFile.string());

    } catch (const spdlog::spdlog_ex& ex) {
        spdlog::drop(std::string(appName));
        logger_ = spdlog::stdout_color_mt(std::string(appName));
        auto level = debug ? spdlog::level::debug : spdlog::level::info;
        logger_->set_level(level);
        logger_->warn("Failed to create file logger: {}", ex.what());
    }
}

void Logger::shutdown() {
    if (logger_) {
        logger_->flush();
    }
    spdlog::shutdown();
}

std::shared_ptr<spdlog::logger>& Logger::get() {
    if (!logger_) {
        init();
    }
    return logger_;
}

} // namespace vc
