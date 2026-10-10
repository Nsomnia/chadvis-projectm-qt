#include "Logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include "util/FileUtils.hpp"

namespace vc {

/// Process-wide logger cell. Held by a function-local static (Meyers
/// singleton) inside holder(), so the first touch — from whichever thread —
/// runs the Holder constructor exactly once; C++11+ "magic statics"
/// serialize that first construction. The old namespace-scope
/// `static std::shared_ptr logger_` had no such guarantee: two threads in
/// get() before init() both saw null, both lazily ran init(), and raced the
/// pointer assignment and spdlog::register_logger.
struct Logger::Holder {
    std::shared_ptr<spdlog::logger> logger;

    Holder()
        : logger(std::make_shared<spdlog::logger>(
                  "chadvis.preinit", std::make_shared<spdlog::sinks::null_sink_mt>())) {
        // Fallback for LOG_* before init() (or if init() never runs, e.g. an
        // early unit-test suite): usable from any thread, deliberately
        // silent, and deliberately NOT registered with the spdlog registry.
        logger->set_level(spdlog::level::info);
    }
};

Logger::Holder& Logger::holder() {
    static Holder instance;
    return instance;
}

void Logger::init(std::string_view appName, bool debug) {
    try {
        spdlog::drop(std::string(appName));

        std::vector<spdlog::sink_ptr> sinks;

        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_pattern("%^[%H:%M:%S.%e] [%l]%$ %v");
        sinks.push_back(console);

        auto logDir = file::cacheDir() / "logs";
        (void)file::ensureDir(logDir); // Best-effort; sink open failure is handled below.

        auto logFile = logDir / (std::string(appName) + ".log");
        // Use rotating file sink to prevent massive log files
        // max_size: 5MB, max_files: 3
        auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile.string(),
                                                                           1024 * 1024 * 5, 3);
        file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        sinks.push_back(file);

        auto logger =
                std::make_shared<spdlog::logger>(std::string(appName), sinks.begin(), sinks.end());

        auto level = debug ? spdlog::level::debug : spdlog::level::info;
        logger->set_level(level);
        logger->flush_on(level);

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);

        // Publish only after the logger is fully built and registered, so a
        // get() racing this init() keeps the previous (fallback) logger
        // rather than observing a half-built one.
        holder().logger = std::move(logger);

        LOG_INFO("Logger initialized. Debug mode: {}", debug);
        LOG_DEBUG("Log file: {}", logFile.string());

    } catch (const spdlog::spdlog_ex& ex) {
        spdlog::drop(std::string(appName));
        auto fallback = spdlog::stdout_color_mt(std::string(appName));
        auto level = debug ? spdlog::level::debug : spdlog::level::info;
        fallback->set_level(level);
        holder().logger = std::move(fallback);
        holder().logger->warn("Failed to create file logger: {}", ex.what());
    }
}

void Logger::shutdown() {
    if (auto& logger = holder().logger; logger) {
        logger->flush();
    }
    spdlog::shutdown();
}

std::shared_ptr<spdlog::logger>& Logger::get() {
    // No lazy init() here — that was the race. The holder always contains a
    // usable logger (the null-sink fallback until init() replaces it).
    return holder().logger;
}

} // namespace vc
