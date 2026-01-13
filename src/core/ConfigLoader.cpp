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

    if (fs::exists(defaultPath))
        return load(config, defaultPath);

    fs::path systemDefault =
            "/usr/share/chadvis-projectm-qt/config/default.toml";
    if (fs::exists(systemDefault)) {
        file::ensureDir(configDir);
        std::error_code ec;
        fs::copy_file(systemDefault, defaultPath, ec);
        if (!ec)
            return load(config, defaultPath);
    }

    LOG_WARN("No config file found, using built-in defaults");
    config.visualizer().presetPath = file::presetsDir();
    save(config, defaultPath);
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
        return Result<void>::ok();
    } catch (const std::exception& e) {
        return Result<void>::err(std::string("Failed to save config: ") +
                                 e.what());
    }
}

} // namespace vc
