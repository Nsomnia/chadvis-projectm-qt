#include "FileUtils.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <format>
#include <regex>

namespace vc::file {

fs::path configDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return fs::path(xdg) / "chadvis-projectm-qt";
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".config" / "chadvis-projectm-qt";
    }
    return fs::current_path() / ".chadvis-projectm-qt";
}

fs::path dataDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        return fs::path(xdg) / "chadvis-projectm-qt";
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".local" / "share" / "chadvis-projectm-qt";
    }
    return fs::current_path() / ".chadvis-projectm-qt-data";
}

fs::path cacheDir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        return fs::path(xdg) / "chadvis-projectm-qt";
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".cache" / "chadvis-projectm-qt";
    }
    return fs::temp_directory_path() / "chadvis-projectm-qt";
}

fs::path presetsDir() {
    // Check common locations
    std::vector<fs::path> candidates = {
        "/usr/share/projectM/presets",
        "/usr/local/share/projectM/presets",
        "/usr/share/projectm-presets",
        dataDir() / "presets"
    };
    
    for (const auto& p : candidates) {
        if (fs::exists(p) && fs::is_directory(p)) {
            return p;
        }
    }
    
    // Fallback to data dir
    return dataDir() / "presets";
}

Result<void> ensureDir(const fs::path& path) {
    std::error_code ec;
    if (fs::exists(path)) {
        if (!fs::is_directory(path)) {
            return Result<void>::err("Path exists but is not a directory: " + path.string());
        }
        return Result<void>::ok();
    }
    
    if (!fs::create_directories(path, ec)) {
        return Result<void>::err("Failed to create directory: " + path.string() + " - " + ec.message());
    }
    return Result<void>::ok();
}

Result<std::string> readText(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return Result<std::string>::err("Failed to open file: " + path.string());
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    return Result<std::string>::ok(ss.str());
}

Result<void> writeText(const fs::path& path, std::string_view content) {
    // Write to temp file first, then rename (atomic)
    auto tempPath = path;
    tempPath += ".tmp";
    
    {
        std::ofstream file(tempPath);
        if (!file) {
            return Result<void>::err("Failed to open file for writing: " + tempPath.string());
        }
        file << content;
        if (!file) {
            return Result<void>::err("Failed to write to file: " + tempPath.string());
        }
    }
    
    std::error_code ec;
    fs::rename(tempPath, path, ec);
    if (ec) {
        fs::remove(tempPath);
        return Result<void>::err("Failed to rename temp file: " + ec.message());
    }
    
    return Result<void>::ok();
}

Result<std::vector<u8>> readBinary(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result<std::vector<u8>>::err("Failed to open file: " + path.string());
    }
    
    auto size = file.tellg();
    file.seekg(0);
    
    std::vector<u8> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    return Result<std::vector<u8>>::ok(std::move(data));
}

std::vector<fs::path> listFiles(const fs::path& dir, 
                                 const std::set<std::string>& extensions,
                                 bool recursive) {
    std::vector<fs::path> result;
    
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return result;
    }
    
    auto matches = [&extensions](const fs::path& p) {
        if (extensions.empty()) return true;
        auto ext = p.extension().string();
        // Lowercase comparison
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return extensions.contains(ext);
    };
    
    std::error_code ec;
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && matches(entry.path())) {
                result.push_back(entry.path());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && matches(entry.path())) {
                result.push_back(entry.path());
            }
        }
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

fs::path uniquePath(const fs::path& desired) {
    if (!fs::exists(desired)) {
        return desired;
    }
    
    auto stem = desired.stem().string();
    auto ext = desired.extension().string();
    auto parent = desired.parent_path();
    
    for (int i = 1; i < 10000; ++i) {
        auto candidate = parent / (stem + "_" + std::to_string(i) + ext);
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }
    
    // Give up, user has too many files
    return desired;
}

std::string humanSize(std::uintmax_t bytes) {
    constexpr std::array<const char*, 5> units = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        ++unit;
    }
    
    if (unit == 0) {
        return std::format("{} {}", bytes, units[unit]);
    }
    return std::format("{:.1f} {}", size, units[unit]);
}

std::string formatDuration(Duration dur) {
    auto total = dur.count();
    auto hours = total / 3600000;
    auto minutes = (total % 3600000) / 60000;
    auto seconds = (total % 60000) / 1000;
    
    if (hours > 0) {
        return std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
    }
    return std::format("{:02}:{:02}", minutes, seconds);
}

std::optional<Duration> parseDuration(std::string_view str) {
    std::regex pattern(R"((?:(\d+):)?(\d+):(\d+))");
    std::cmatch match;
    
    if (std::regex_match(str.begin(), str.end(), match, pattern)) {
        i64 hours = match[1].matched ? std::stoll(match[1].str()) : 0;
        i64 minutes = std::stoll(match[2].str());
        i64 seconds = std::stoll(match[3].str());
        
        return Duration((hours * 3600 + minutes * 60 + seconds) * 1000);
    }
    
    return std::nullopt;
}

} // namespace vc::file

// Color implementation
namespace vc {

Color Color::fromHex(std::string_view hex) {
    Color c;
    std::string h(hex);
    
    // Remove # if present
    if (!h.empty() && h[0] == '#') {
        h = h.substr(1);
    }
    
    if (h.length() == 6) {
        c.r = static_cast<u8>(std::stoi(h.substr(0, 2), nullptr, 16));
        c.g = static_cast<u8>(std::stoi(h.substr(2, 2), nullptr, 16));
        c.b = static_cast<u8>(std::stoi(h.substr(4, 2), nullptr, 16));
        c.a = 255;
    } else if (h.length() == 8) {
        c.r = static_cast<u8>(std::stoi(h.substr(0, 2), nullptr, 16));
        c.g = static_cast<u8>(std::stoi(h.substr(2, 2), nullptr, 16));
        c.b = static_cast<u8>(std::stoi(h.substr(4, 2), nullptr, 16));
        c.a = static_cast<u8>(std::stoi(h.substr(6, 2), nullptr, 16));
    }
    
    return c;
}

std::string Color::toHex() const {
    return std::format("#{:02X}{:02X}{:02X}{:02X}", r, g, b, a);
}

} // namespace vc