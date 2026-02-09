#include "ConfigLoader.hpp"
#include <fstream>
#include "Config.hpp"
#include "ConfigParsers.hpp"
#include "Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

Result<void> ConfigLoader::load(Config& config, const fs::path& path) {
    try {
        auto tbl = toml::parse_file(path.string());

        if (auto gen = tbl["general"].as_table()) {
            config.setDebug(gen->get("debug")->value_or(false));
        }

        ConfigParsers::parseAudio(tbl, config.audio());
        ConfigParsers::parseVisualizer(tbl, config.visualizer());
        ConfigParsers::parseRecording(tbl, config.recording());
        ConfigParsers::parseOverlay(tbl, config.overlayElements());
        ConfigParsers::parseUI(tbl, config.ui());
        ConfigParsers::parseKeyboard(tbl, config.keyboard());
        ConfigParsers::parseSuno(tbl, config.suno());
        ConfigParsers::parseKaraoke(tbl, config.karaoke());

        config.markClean();
        LOG_INFO("Config loaded from: {}", path.string());
        return Result<void>::ok();
    } catch (const toml::parse_error& err) {
        return Result<void>::err(std::string("Config parse error: ") +
                                 err.what());
    }
}

Result<void> ConfigLoader::loadDefault(Config& config) {
    auto configDir = file::configDir();
    auto defaultPath = configDir / "config.toml";

    // Always ensure config directory exists first
    if (auto result = file::ensureDir(configDir); !result) {
        LOG_ERROR("Failed to create config directory: {}", result.error().message);
        // Continue anyway - we'll use in-memory defaults
    }

    // Ensure subdirectories exist
    auto dataDir = file::dataDir();
    auto cacheDir = file::cacheDir();
    file::ensureDir(dataDir);
    file::ensureDir(cacheDir);
    file::ensureDir(dataDir / "presets");
    file::ensureDir(cacheDir / "logs");

    if (fs::exists(defaultPath)) {
        config.configPath_ = defaultPath;
        return load(config, defaultPath);
    }

    // Try to copy system default if available
    fs::path systemDefault =
            "/usr/share/chadvis-projectm-qt/config/default.toml";
    if (fs::exists(systemDefault)) {
        std::error_code ec;
        fs::copy_file(systemDefault, defaultPath, ec);
        if (!ec) {
            LOG_INFO("Created default config from system template");
            config.configPath_ = defaultPath;
            return load(config, defaultPath);
        }
    }

    // Generate default config with 1337 comments
    LOG_INFO("No config found - generating fresh default config with Arch-tier quality");
    config.visualizer().presetPath = file::presetsDir();
    config.recording().outputDirectory = dataDir / "recordings";
    file::ensureDir(config.recording().outputDirectory);
    config.configPath_ = defaultPath;
    
    if (auto result = save(config, defaultPath); !result) {
        LOG_WARN("Failed to save default config: {} - using in-memory defaults", 
                 result.error().message);
    } else {
        LOG_INFO("Fresh config generated at: {}", defaultPath.string());
    }
    
    return Result<void>::ok();
}

Result<void> ConfigLoader::save(const Config& config, const fs::path& path) {
    try {
        auto tbl = ConfigParsers::serialize(config.audio(),
                                            config.visualizer(),
                                            config.recording(),
                                            config.ui(),
                                            config.keyboard(),
                                            config.suno(),
                                            config.karaoke(),
                                            config.overlayElements(),
                                            config.debug());
        fs::path tempPath = path;
        tempPath += ".tmp";
        {
            std::ofstream file(tempPath);
            if (!file)
                return Result<void>::err("Failed to open temp config file");
            file << tbl;
        }
        fs::rename(tempPath, path);
        LOG_DEBUG("Config saved to: {}", path.string());
        return Result<void>::ok();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save config: {}", e.what());
        return Result<void>::err(std::string("Failed to save config: ") +
                                 e.what());
    }
}

} // namespace vc
