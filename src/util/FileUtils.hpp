#pragma once
// FileUtils.hpp - File system helpers
// Purpose: filesystem + formatting utilities (paths, sizes, durations).
// Does NOT own config parsing/serialization, audio processing, or UI concerns.

#include "Types.hpp"
#include "Result.hpp"
#include <QString>
#include <vector>
#include <set>

namespace vc::file {

// Get standard paths
fs::path configDir();          // ~/.config/chadvis-projectm-qt
fs::path dataDir();            // ~/.local/share/chadvis-projectm-qt
fs::path cacheDir();           // ~/.cache/chadvis-projectm-qt
fs::path presetsDir();         // /usr/share/projectM/presets or similar

// Ensure directory exists
[[nodiscard]] Result<void> ensureDir(const fs::path& path);

// Read entire file to string
[[nodiscard]] Result<std::string> readText(const fs::path& path);

// Write string to file (atomic)
[[nodiscard]] Result<void> writeText(const fs::path& path, std::string_view content);

// Read binary file
[[nodiscard]] Result<std::vector<u8>> readBinary(const fs::path& path);

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

// Sanitize a string for use as a filename: replaces path separators ('/' '\\')
// with '_' so the result can never escape its intended directory.
std::string sanitizeFilename(const std::string& name);

// Human-readable file size
std::string humanSize(std::uintmax_t bytes);

// Human-readable file size (QString overload for QML bridges)
QString humanSizeQString(vc::u64 bytes);

// Format duration as HH:MM:SS (zero-padded hours) or MM:SS
std::string formatDuration(Duration dur);

// Format duration as H:MM:SS (non-zero-padded hours) or M:SS (QString for QML bridges)
QString formatDurationQString(vc::i64 ms);

// Format milliseconds as SRT subtitle timecode: HH:MM:SS,mmm (negative clamps to 00:00:00,000)
QString srtTimecode(qint64 milliseconds);

// Parse duration from string
std::optional<Duration> parseDuration(std::string_view str);

} // namespace vc::file