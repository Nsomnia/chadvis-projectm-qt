#include "Config.hpp"
#include "Logger.hpp"
#include "util/FileUtils.hpp"
#include <fstream>

namespace vc {

namespace {

// Helper to get value with default
template<typename T>
T get(const toml::table& tbl, std::string_view key, T defaultVal) {
    if (auto node = tbl[key]) {
        if constexpr (std::is_same_v<T, std::string>) {
            if (auto val = node.value<std::string>()) return *val;
        } else if constexpr (std::is_same_v<T, bool>) {
            if (auto val = node.value<bool>()) return *val;
        } else if constexpr (std::is_same_v<T, f32>) {
            if (auto val = node.value<double>()) return static_cast<f32>(*val);
        } else if constexpr (std::is_integral_v<T>) {
            if (auto val = node.value<i64>()) return static_cast<T>(*val);
        }
    }
    return defaultVal;
}

Vec2 parseVec2(const toml::table& tbl, Vec2 defaultVal = {}) {
    return {
        get(tbl, "x", defaultVal.x),
        get(tbl, "y", defaultVal.y)
    };
}

fs::path expandPath(std::string_view path) {
    std::string p(path);
    if (p.starts_with("~/")) {
        if (const char* home = std::getenv("HOME")) {
            p = std::string(home) + p.substr(1);
        }
    }
    return fs::path(p);
}

} // namespace

Config& Config::instance() {
    static Config instance;
    return instance;
}

Result<void> Config::load(const fs::path& path) {
    std::lock_guard lock(mutex_);
    
    try {
        auto tbl = toml::parse_file(path.string());
        configPath_ = path;
        
        // General
        if (auto gen = tbl["general"].as_table()) {
            debug_ = get(*gen, "debug", false);
        }
        
        parseAudio(tbl);
        parseVisualizer(tbl);
        parseRecording(tbl);
        parseOverlay(tbl);
        parseUI(tbl);
        parseKeyboard(tbl);
        
        dirty_ = false;
        LOG_INFO("Config loaded from: {}", path.string());
        return Result<void>::ok();
        
    } catch (const toml::parse_error& err) {
        return Result<void>::err(std::string("Config parse error: ") + err.what());
    }
}

Result<void> Config::loadDefault() {
    auto configDir = file::configDir();
    auto defaultPath = configDir / "config.toml";
    
    if (fs::exists(defaultPath)) {
        return load(defaultPath);
    }
    
    // Try system default
    fs::path systemDefault = "/usr/share/chadvis-projectm-qt/config/default.toml";
    if (fs::exists(systemDefault)) {
        // Copy to user config
        file::ensureDir(configDir);
        std::error_code ec;
        fs::copy_file(systemDefault, defaultPath, ec);
        if (!ec) {
            LOG_INFO("Copied default config to user directory: {}", defaultPath.string());
            return load(defaultPath);
        } else {
            LOG_WARN("Failed to copy system default config: {}", ec.message());
        }
    }
    
    // Check if project default.toml exists in the source tree (for development)
    // This handles the case where running from source without installation
    fs::path projectDefault = fs::path(__FILE__).parent_path().parent_path().parent_path() / "config" / "default.toml";
    if (fs::exists(projectDefault)) {
        file::ensureDir(configDir);
        std::error_code ec;
        fs::copy_file(projectDefault, defaultPath, ec);
        if (!ec) {
            LOG_INFO("Copied project default config to user directory: {}", defaultPath.string());
            return load(defaultPath);
        } else {
            LOG_WARN("Failed to copy project default config: {}", ec.message());
        }
    }
    
    // Use built-in defaults
    LOG_WARN("No config file found, using built-in defaults");
    configPath_ = defaultPath;
    
    // Set sensible defaults
    visualizer_.presetPath = file::presetsDir();
    recording_.outputDirectory = expandPath("~/Videos/ChadVis");
    
    // Save the defaults to create the config file
    if (auto result = save(defaultPath); !result) {
        LOG_WARN("Failed to save default config: {}", result.error().message);
    } else {
        LOG_INFO("Created default config file: {}", defaultPath.string());
    }
    
    return Result<void>::ok();
}

Result<void> Config::save(const fs::path& path) const {
    std::lock_guard lock(mutex_);
    
    try {
        auto tbl = serialize();
        
        std::ofstream file(path);
        if (!file) {
            return Result<void>::err("Failed to open config file for writing");
        }
        
        file << tbl;
        LOG_INFO("Config saved to: {}", path.string());
        return Result<void>::ok();
        
    } catch (const std::exception& e) {
        return Result<void>::err(std::string("Failed to save config: ") + e.what());
    }
}

void Config::parseAudio(const toml::table& tbl) {
    if (auto audio = tbl["audio"].as_table()) {
        audio_.device = get(*audio, "device", std::string("default"));
        audio_.bufferSize = get(*audio, "buffer_size", 2048u);
        audio_.sampleRate = get(*audio, "sample_rate", 44100u);
    }
}

void Config::parseVisualizer(const toml::table& tbl) {
    if (auto viz = tbl["visualizer"].as_table()) {
        auto pathStr = get(*viz, "preset_path", std::string("/usr/share/projectM/presets"));
        visualizer_.presetPath = expandPath(pathStr);
        visualizer_.width = get(*viz, "width", 1920u);
        visualizer_.height = get(*viz, "height", 1080u);
        visualizer_.fps = get(*viz, "fps", 60u);
        visualizer_.beatSensitivity = get(*viz, "beat_sensitivity", 1.0f);
        visualizer_.presetDuration = get(*viz, "preset_duration", 30u);
        visualizer_.smoothPresetDuration = get(*viz, "smooth_preset_duration", 5u);
        visualizer_.shufflePresets = get(*viz, "shuffle_presets", true);
        visualizer_.forcePreset = get(*viz, "force_preset", std::string());
    }
}

void Config::parseRecording(const toml::table& tbl) {
    if (auto rec = tbl["recording"].as_table()) {
        recording_.enabled = get(*rec, "enabled", true);
        auto outDir = get(*rec, "output_directory", std::string("~/Videos/ChadVis"));
        recording_.outputDirectory = expandPath(outDir);
        recording_.defaultFilename = get(*rec, "default_filename", std::string("chadvis-projectm-qt_{date}_{time}"));
        recording_.container = get(*rec, "container", std::string("mp4"));
        
        if (auto video = (*rec)["video"].as_table()) {
            recording_.video.codec = get(*video, "codec", std::string("libx264"));
            recording_.video.crf = get(*video, "crf", 18u);
            recording_.video.preset = get(*video, "preset", std::string("medium"));
            recording_.video.pixelFormat = get(*video, "pixel_format", std::string("yuv420p"));
            recording_.video.width = get(*video, "width", 1920u);
            recording_.video.height = get(*video, "height", 1080u);
            recording_.video.fps = get(*video, "fps", 60u);
        }
        
        if (auto audio = (*rec)["audio"].as_table()) {
            recording_.audio.codec = get(*audio, "codec", std::string("aac"));
            recording_.audio.bitrate = get(*audio, "bitrate", 320u);
        }
    }
}

void Config::parseOverlay(const toml::table& tbl) {
    overlayElements_.clear();
    
    if (auto overlay = tbl["overlay"].as_table()) {
        // Check for enabled flag (optional)
        // Default to true if not specified
        
        if (auto elements = (*overlay)["elements"].as_array()) {
            for (const auto& elem : *elements) {
                if (auto elemTbl = elem.as_table()) {
                    OverlayElementConfig cfg;
                    cfg.id = get(*elemTbl, "id", std::string("element"));
                    cfg.text = get(*elemTbl, "text", std::string(""));
                    
                    if (auto pos = (*elemTbl)["position"].as_table()) {
                        cfg.position = parseVec2(*pos);
                    }
                    
                    cfg.fontSize = get(*elemTbl, "font_size", 32u);
                    
                    auto colorStr = get(*elemTbl, "color", std::string("#FFFFFF"));
                    cfg.color = Color::fromHex(colorStr);
                    
                    cfg.opacity = get(*elemTbl, "opacity", 1.0f);
                    cfg.animation = get(*elemTbl, "animation", std::string("none"));
                    cfg.animationSpeed = get(*elemTbl, "animation_speed", 1.0f);
                    cfg.anchor = get(*elemTbl, "anchor", std::string("left"));
                    cfg.visible = get(*elemTbl, "visible", true);
                    
                    overlayElements_.push_back(std::move(cfg));
                }
            }
        }
    }
}

void Config::parseUI(const toml::table& tbl) {
    if (auto uiTbl = tbl["ui"].as_table()) {
        ui_.theme = get(*uiTbl, "theme", std::string("dark"));
        ui_.showPlaylist = get(*uiTbl, "show_playlist", true);
        ui_.showPresets = get(*uiTbl, "show_presets", true);
        ui_.showDebugPanel = get(*uiTbl, "show_debug_panel", false);
        
        auto bgColor = get(*uiTbl, "visualizer_background", std::string("#000000"));
        ui_.backgroundColor = Color::fromHex(bgColor);
        
        auto accentColor = get(*uiTbl, "accent_color", std::string("#00FF88"));
        ui_.accentColor = Color::fromHex(accentColor);
    }
}

void Config::parseKeyboard(const toml::table& tbl) {
    if (auto kb = tbl["keyboard"].as_table()) {
        keyboard_.playPause = get(*kb, "play_pause", std::string("Space"));
        keyboard_.nextTrack = get(*kb, "next_track", std::string("N"));
        keyboard_.prevTrack = get(*kb, "prev_track", std::string("P"));
        keyboard_.toggleRecord = get(*kb, "toggle_record", std::string("R"));
        keyboard_.toggleFullscreen = get(*kb, "toggle_fullscreen", std::string("F"));
        keyboard_.nextPreset = get(*kb, "next_preset", std::string("Right"));
        keyboard_.prevPreset = get(*kb, "prev_preset", std::string("Left"));
    }
}

toml::table Config::serialize() const {
    toml::table root;
    
    // General
    root.insert("general", toml::table{
        {"debug", debug_}
    });
    
    // Audio
    root.insert("audio", toml::table{
        {"device", audio_.device},
        {"buffer_size", static_cast<i64>(audio_.bufferSize)},
        {"sample_rate", static_cast<i64>(audio_.sampleRate)}
    });
    
    // Visualizer
    root.insert("visualizer", toml::table{
        {"preset_path", visualizer_.presetPath.string()},
        {"width", static_cast<i64>(visualizer_.width)},
        {"height", static_cast<i64>(visualizer_.height)},
        {"fps", static_cast<i64>(visualizer_.fps)},
        {"beat_sensitivity", static_cast<double>(visualizer_.beatSensitivity)},
        {"preset_duration", static_cast<i64>(visualizer_.presetDuration)},
        {"smooth_preset_duration", static_cast<i64>(visualizer_.smoothPresetDuration)},
        {"shuffle_presets", visualizer_.shufflePresets},
        {"force_preset", visualizer_.forcePreset}
    });
    
    // Recording
    toml::table recVideo{
        {"codec", recording_.video.codec},
        {"crf", static_cast<i64>(recording_.video.crf)},
        {"preset", recording_.video.preset},
        {"pixel_format", recording_.video.pixelFormat},
        {"width", static_cast<i64>(recording_.video.width)},
        {"height", static_cast<i64>(recording_.video.height)},
        {"fps", static_cast<i64>(recording_.video.fps)}
    };
    
    toml::table recAudio{
        {"codec", recording_.audio.codec},
        {"bitrate", static_cast<i64>(recording_.audio.bitrate)}
    };
    
    root.insert("recording", toml::table{
        {"enabled", recording_.enabled},
        {"output_directory", recording_.outputDirectory.string()},
        {"default_filename", recording_.defaultFilename},
        {"container", recording_.container},
        {"video", recVideo},
        {"audio", recAudio}
    });
    
    // Overlay elements
    toml::array elementsArr;
    for (const auto& elem : overlayElements_) {
        toml::table elemTbl{
            {"id", elem.id},
            {"text", elem.text},
            {"position", toml::table{{"x", elem.position.x}, {"y", elem.position.y}}},
            {"font_size", static_cast<i64>(elem.fontSize)},
            {"color", elem.color.toHex()},
            {"opacity", static_cast<double>(elem.opacity)},
            {"animation", elem.animation},
            {"animation_speed", static_cast<double>(elem.animationSpeed)},
            {"anchor", elem.anchor},
            {"visible", elem.visible}
        };
        elementsArr.push_back(elemTbl);
    }
    
    root.insert("overlay", toml::table{
        {"enabled", true},
        {"elements", elementsArr}
    });
    
    // UI
    root.insert("ui", toml::table{
        {"theme", ui_.theme},
        {"show_playlist", ui_.showPlaylist},
        {"show_presets", ui_.showPresets},
        {"show_debug_panel", ui_.showDebugPanel},
        {"visualizer_background", ui_.backgroundColor.toHex()},
        {"accent_color", ui_.accentColor.toHex()}
    });
    
    // Keyboard
    root.insert("keyboard", toml::table{
        {"play_pause", keyboard_.playPause},
        {"next_track", keyboard_.nextTrack},
        {"prev_track", keyboard_.prevTrack},
        {"toggle_record", keyboard_.toggleRecord},
        {"toggle_fullscreen", keyboard_.toggleFullscreen},
        {"next_preset", keyboard_.nextPreset},
        {"prev_preset", keyboard_.prevPreset}
    });
    
    return root;
}

void Config::addOverlayElement(OverlayElementConfig elem) {
    std::lock_guard lock(mutex_);
    overlayElements_.push_back(std::move(elem));
    markDirty();
}

void Config::removeOverlayElement(const std::string& id) {
    std::lock_guard lock(mutex_);
    std::erase_if(overlayElements_, [&id](const auto& e) { return e.id == id; });
    markDirty();
}

OverlayElementConfig* Config::findOverlayElement(const std::string& id) {
    for (auto& elem : overlayElements_) {
        if (elem.id == id) return &elem;
    }
    return nullptr;
}

} // namespace vc
