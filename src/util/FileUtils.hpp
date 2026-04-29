#pragma once
// FileUtils.hpp - File system helpers
// std::filesystem is great but verbose

#include "Types.hpp"
#include "Result.hpp"
#include <vector>
#include <set>

class QString;

namespace vc::file {

// Get standard paths
fs::path configDir();          // ~/.config/chadvis-projectm-qt
fs::path dataDir();            // ~/.local/share/chadvis-projectm-qt
fs::path cacheDir();           // ~/.cache/chadvis-projectm-qt
fs::path presetsDir();         // /usr/share/projectM/presets or similar

// Ensure directory exists
Result<void> ensureDir(const fs::path& path);

// Read entire file to string
Result<std::string> readText(const fs::path& path);

// Write string to file (atomic)
Result<void> writeText(const fs::path& path, std::string_view content);

// Read binary file
Result<std::vector<u8>> readBinary(const fs::path& path);

// List files with extension filter
std::vector<fs::path> listFiles(const fs::path& dir, 
                                 const std::set<std::string>& extensions = {},
                                 bool recursive = false);

// Supported audio extensions
inline const std::set<std::string> audioExtensions = {
    ".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac", ".wma"
};

// Supported video extensions (for output)
inline const std::set<std::string> videoExtensions = {
    ".mp4", ".mkv", ".webm", ".avi", ".mov"
};

// Preset extensions
inline const std::set<std::string> presetExtensions = {
    ".milk", ".prjm"
};

// Generate unique filename (avoids overwriting)
fs::path uniquePath(const fs::path& desired);

// Human-readable file size
std::string humanSize(std::uintmax_t bytes);

// Human-readable file size (QString overload for QML bridges)
QString humanSizeQString(vc::u64 bytes);

// Format duration as HH:MM:SS (zero-padded hours) or MM:SS
std::string formatDuration(Duration dur);

// Format duration as H:MM:SS (non-zero-padded hours) or M:SS (QString for QML bridges)
QString formatDurationQString(vc::i64 ms);

// Parse duration from string
std::optional<Duration> parseDuration(std::string_view str);

} // namespace vc::file