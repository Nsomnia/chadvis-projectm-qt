#pragma once
// FileUtils.hpp - File system helpers
// Purpose: filesystem + formatting utilities (paths, sizes, durations).
// Does NOT own config parsing/serialization, audio processing, or UI concerns.

#include "Types.hpp"
#include "Result.hpp"
#include <QString>
#include <cstddef>
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

/// Hard ceiling on the total byte length of a sanitizeFilename() result,
/// extension included.
///
/// 255 BYTES is what ext4 and APFS allow (NAME_MAX), and 255 UTF-16 code units
/// is what a Win32 long-path component allows. UTF-8 never encodes a character
/// in fewer bytes than UTF-16 needs code units, so a byte budget of 255 is a
/// bound on all three at once. Exposed rather than kept private because the
/// boundary is part of the contract and a test that has to hard-code 255 is a
/// test that silently stops testing the real limit if the policy ever moves.
inline constexpr std::size_t maxFilenameBytes = 255;

/// Reduce arbitrary text to ONE path component, deterministically.
///
/// The result is always non-empty and always safe to concatenate onto a
/// directory (`dir / sanitizeFilename(x)` cannot escape `dir`). That guarantee
/// is the whole point of this function and it is a property of the RESULT, not
/// of how it was called -- see "Every interpolated piece" below.
///
/// Policy, in order (each step's rationale is in the implementation):
///   1. Decode as UTF-8 and normalize to NFC, so a composed and a decomposed
///      spelling of the same visible title produce byte-identical names --
///      measured requirement, because APFS and NTFS compare filenames in a
///      normalized form and would otherwise treat them as the same file.
///   2. Replace '/', '\\', C0 controls (including NUL), DEL and "*?"><|: with
///      '_', and a standalone multi-dot component ("..", "....") with as many
///      '_' as it had dots.
///   3. Trim trailing spaces and dots from the whole name AND from the stem
///      separately. The stem trim is what catches "CON .mp3": Win32 strips
///      trailing spaces from a component before matching the reserved-name
///      list, so the stem "CON " arrives at the filesystem as "CON".
///   4. Truncate to maxFilenameBytes on a UTF-8 sequence boundary, never
///      mid-sequence.
///   5. Prefix '_' if the final stem is a Win32 reserved device name
///      (CON, PRN, AUX, NUL, COM1-COM9, LPT1-LPT9), compared
///      case-insensitively. Checked after truncation, because cutting
///      "COM11" down to "COM1" creates a name that was not reserved a moment
///      earlier.
///
/// NOT covered, and every caller must handle it: this function sanitizes ONE
/// component. It cannot sanitize a filename assembled from several pieces of
/// remote data, because it only ever sees what it is given. SunoDownloader is
/// the live counter-example: it sanitizes the title and then interpolates a raw
/// remote `clip.id` into the same path (`src/suno/SunoDownloader.cpp:261`),
/// which re-opens the traversal this function closes. Sanitize every piece, or
/// pass the assembled name through here as the last step.
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