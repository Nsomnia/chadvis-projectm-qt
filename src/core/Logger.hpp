#pragma once
// Logger.hpp - Logging wrapper
// Because printf debugging is for the weak

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string_view>

namespace vc {

class Logger {
public:
    static void init(std::string_view appName = "chadvis-projectm-qt", bool debug = false);
    static void shutdown();
    
    static std::shared_ptr<spdlog::logger>& get();
    
    // Convenience macros below
private:
    static std::shared_ptr<spdlog::logger> logger_;
};

// Macros for convenient logging with source location
// Use these instead of calling Logger::get() directly

#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(vc::Logger::get(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(vc::Logger::get(), __VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_LOGGER_INFO(vc::Logger::get(), __VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_LOGGER_WARN(vc::Logger::get(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(vc::Logger::get(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(vc::Logger::get(), __VA_ARGS__)

} // namespace vc