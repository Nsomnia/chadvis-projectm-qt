#include "FileUtils.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>
#include <QByteArray>
#include <QStandardPaths>
#include <QString>
#include "core/Logger.hpp"

namespace vc::file {

namespace {

// Resolve a generic per-user base directory via QStandardPaths and append the
// app-specific directory name ourselves.
//
// The Generic* locations are used deliberately: they derive purely from the
// environment (XDG_* vars / HOME on Unix, %APPDATA%/%LOCALAPPDATA% on Windows)
// and do NOT consult the application name, so they are safe before
// QCoreApplication exists or before setApplicationName() has run. On Unix they
// honor XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_CACHE_HOME with the standard HOME
// fallbacks, preserving the exact on-disk layout of the previous hand-rolled
// resolution (~/.config|~/.local/share|~/.cache + chadvis-projectm-qt).
fs::path genericBaseDir(QStandardPaths::StandardLocation location,
                        const fs::path& lastResort) {
    const QString base = QStandardPaths::writableLocation(location);
    if (base.isEmpty())
        return lastResort;
    return fs::path(base.toStdString()) / "chadvis-projectm-qt";
}

// ── ByteSizeFormatter ────────────────────────────────────────────────
// Single 1024-ladder shared by humanSize()/humanSizeQString(); the public
// wrappers own only their output formatting.
struct ByteSizeComponent {
    double value;
    int unitIndex; // 0=B .. 4=TB
};

constexpr std::array<const char*, 5> kByteUnits = {"B", "KB", "MB", "GB", "TB"};

ByteSizeComponent byteSizeComponents(std::uintmax_t bytes) {
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        ++unit;
    }
    return {size, unit};
}

// ── TimecodeFormatter ────────────────────────────────────────────────
// Single h/m/s decomposition shared by formatDuration()/formatDurationQString().
struct TimecodeComponents {
    long long hours;
    long long minutes;
    long long seconds;
};

TimecodeComponents timecodeComponents(long long totalMs) {
    const long long t = totalMs > 0 ? totalMs : 0;
    return {t / 3600000, (t % 3600000) / 60000, (t % 60000) / 1000};
}

// ── Filename safety ──────────────────────────────────────────────────
// Helpers for sanitizeFilename(); see that function for the policy they serve.

/// Length in bytes of the UTF-8 sequence whose lead byte is `lead`.
///
/// Returns 1 for an ASCII byte, for a continuation byte (10xxxxxx) and for
/// anything that is not a valid lead byte, so a caller stepping through a
/// buffer one sequence at a time always makes progress. Valid UTF-8 from
/// QString::toUtf8() never exercises the last two cases; the value is
/// defensive rather than load-bearing.
constexpr std::size_t utf8SequenceLength(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

/// True when `stem` is a Win32 reserved device name, compared case-insensitively.
///
/// The comparison is on the STEM, not on the whole name, because that is how
/// Win32 matches: "CON", "con.mp3" and "LPT1.txt" all name a device. The
/// lowercasing is ASCII-only on purpose -- tolower() is locale-dependent and
/// the reserved list is ASCII, so a non-ASCII byte can never equal one of them
/// and must not be folded into one.
bool isReservedDeviceStem(const std::string& stem) {
    static const std::set<std::string> reservedDeviceNames = {
            "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
            "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::string lowerStem;
    lowerStem.reserve(stem.size());
    for (const unsigned char c : stem) {
        lowerStem.push_back(static_cast<char>(std::tolower(c)));
    }
    return reservedDeviceNames.contains(lowerStem);
}

} // namespace

fs::path configDir() {
    return genericBaseDir(QStandardPaths::GenericConfigLocation,
                          fs::current_path() / ".chadvis-projectm-qt");
}

fs::path dataDir() {
    return genericBaseDir(QStandardPaths::GenericDataLocation,
                          fs::current_path() / ".chadvis-projectm-qt-data");
}

fs::path cacheDir() {
    return genericBaseDir(QStandardPaths::GenericCacheLocation,
                          fs::temp_directory_path() / "chadvis-projectm-qt");
}

fs::path presetsDir() {
    std::vector<fs::path> candidates = {"/usr/share/projectM/presets",
                                        "/usr/local/share/projectM/presets",
                                        "/opt/homebrew/share/projectM/presets",
#ifdef _WIN32
                                        fs::path(qEnvironmentVariable("LOCALAPPDATA").toStdString()) /
                                                "projectM" / "presets",
#endif
                                        "/usr/share/projectm-presets",
                                        dataDir() / "presets"};

    for (const auto& p : candidates) {
        if (fs::exists(p) && fs::is_directory(p)) {
            return p;
        }
    }

    return dataDir() / "presets";
}

Result<void> ensureDir(const fs::path& path) {
    std::error_code ec;
    if (fs::exists(path)) {
        if (!fs::is_directory(path)) {
            return Result<void>::err("Path exists but is not a directory: " +
                                     path.string());
        }
        return Result<void>::ok();
    }

    if (!fs::create_directories(path, ec)) {
        return Result<void>::err("Failed to create directory: " +
                                 path.string() + " - " + ec.message());
    }
    return Result<void>::ok();
}

Result<std::string> readText(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return Result<std::string>::err("Failed to open file: " +
                                        path.string());
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return Result<std::string>::ok(ss.str());
}

Result<void> writeText(const fs::path& path, std::string_view content) {
    auto tempPath = path;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath);
        if (!file) {
            return Result<void>::err("Failed to open file for writing: " +
                                     tempPath.string());
        }
        file << content;
        if (!file) {
            return Result<void>::err("Failed to write to file: " +
                                     tempPath.string());
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
        return Result<std::vector<u8>>::err("Failed to open file: " +
                                            path.string());
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

    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        LOG_WARN("listFiles: directory does not exist: {}", dir.string());
        return result;
    }

    auto matches = [&extensions](const fs::path& p) {
        if (extensions.empty())
            return true;
        std::error_code ignore;
        if (!fs::is_regular_file(p, ignore))
            return false;

        std::string ext = p.extension().string();
        if (ext.empty())
            return false;
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return extensions.contains(ext);
    };

    try {
        if (recursive) {
            for (auto it = fs::recursive_directory_iterator(
                         dir,
                         fs::directory_options::skip_permission_denied,
                         ec);
                 it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec) {
                    LOG_DEBUG("listFiles: error during iteration at {}: {}",
                              it->path().string(),
                              ec.message());
                    ec.clear();
                    continue;
                }

                std::error_code ignore;
                if (fs::is_regular_file(it->path(), ignore) &&
                    matches(it->path())) {
                    result.push_back(it->path());
                }
            }
        } else {
            for (auto it = fs::directory_iterator(dir, ec);
                 it != fs::directory_iterator();
                 it.increment(ec)) {
                if (ec) {
                    LOG_DEBUG("listFiles: error during iteration at {}: {}",
                              it->path().string(),
                              ec.message());
                    ec.clear();
                    continue;
                }

                std::error_code ignore;
                if (fs::is_regular_file(it->path(), ignore) &&
                    matches(it->path())) {
                    result.push_back(it->path());
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("listFiles: Exception during iteration in {}: {}",
                  dir.string(),
                  e.what());
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

    return desired;
}

std::string sanitizeFilename(const std::string& name) {
    if (name.empty()) return "_";

    // ── Step 1: decode as UTF-8 and normalize to NFC ─────────────────────────
    // Two measured reasons, both about collisions rather than about exotic
    // input:
    //
    //  * APFS and NTFS both store filenames in a normalized, comparison-
    //    insensitive form, so a composed title ("café" as U+00E9) and the
    //    decomposed spelling of the same visible string (U+0065 U+0301) are
    //    DIFFERENT BYTES but the SAME FILE on those filesystems. Handing the
    //    filesystem two byte strings that it will consider equal is how one
    //    silently clobbers the other. Emitting NFC makes the two spellings
    //    byte-identical before they ever reach the filesystem.
    //
    //  * Invalid UTF-8 becomes U+FFFD here rather than surviving to the next
    //    decode. Measured: two raw invalid bytes become six bytes of
    //    replacement characters, so a mangled name is repaired instead of
    //    propagating.
    //
    // The length argument is explicit so an embedded NUL is treated as data --
    // QString(const char*) would stop at it and silently discard the tail.
    //
    // This uses Qt's own normalization, not a new dependency: QtCore links ICU
    // on this machine (verified with otool -L) and falls back to its built-in
    // tables where ICU is absent, so NFC is available either way and there is
    // nothing to add to cmake/Dependencies.cmake.
    const QString decoded = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
                                    .normalized(QString::NormalizationForm_C);
    const QByteArray normalized = decoded.toUtf8();
    const std::string src(normalized.constData(), static_cast<std::size_t>(normalized.size()));

    // ── Step 2: character-level replacement, byte-for-byte 1:1 ──────────────
    std::string safe;
    safe.reserve(src.size());

    // Replace every path separator, shell-forbidden character, C0/DEL control,
    // and dot in a standalone multi-dot path component with one underscore.
    // Dots inside names and a final extension are preserved; neutralizing the
    // complete dot component prevents ".." and "...." traversal semantics.
    for (std::size_t i = 0; i < src.size();) {
        const unsigned char c = static_cast<unsigned char>(src[i]);

        if (c == '/' || c == '\\') {
            safe.push_back('_');
            ++i;
            continue;
        }

        if (c == '.') {
            std::size_t end = i + 1;
            while (end < src.size() && src[end] == '.') {
                ++end;
            }
            const bool componentStart = i == 0 || src[i - 1] == '/' || src[i - 1] == '\\';
            const bool componentEnd = end == src.size() || src[end] == '/' || src[end] == '\\';
            if (end - i >= 2 && componentStart && componentEnd) {
                safe.append(end - i, '_');
            } else {
                safe.append(src, i, end - i);
            }
            i = end;
            continue;
        }

        // NUL is in this set for a reason beyond tidiness: a filename
        // containing one is a lie, because every POSIX open() stops at the
        // first NUL and the rest of the string is discarded by the kernel.
        // Mapping it to '_' keeps the visible length and cannot be silently
        // truncated into a collision by a later syscall.
        if (c <= 0x1F || c == 0x7F || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == ':') {
            safe.push_back('_');
        } else {
            safe.push_back(static_cast<char>(c));
        }
        ++i;
    }

    // Windows rejects trailing spaces and dots; trimming also handles inputs
    // made entirely of otherwise-allowed characters.
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) {
        safe.pop_back();
    }
    if (safe.empty()) {
        return "_";
    }

    // Extract stem (part before the last dot, or whole name if no dot)
    size_t dotPos = safe.find_last_of('.');
    std::string stem = (dotPos != std::string::npos) ? safe.substr(0, dotPos) : safe;
    std::string extension = (dotPos != std::string::npos) ? safe.substr(dotPos) : "";

    // ── Step 3: strip trailing spaces/dots from the STEM ─────────────────────
    // This is the whole reason "CON .mp3" used to survive the reserved-name
    // check while "CON" did not. Win32 removes trailing spaces and dots from
    // every path component before it looks at the name, so the stem "CON "
    // reaches the filesystem as "CON" -- a reserved device name, whatever
    // extension follows. Trimming only the END OF THE WHOLE STRING cannot see
    // this, because the trailing character here is '3', not a space.
    //
    // Never trim to empty: "..test.." splits to stem "." / ext ".test", and
    // emptying the stem would silently rewrite that to ".test". A stem that
    // trims to nothing is kept as it was.
    {
        std::size_t end = stem.size();
        while (end > 1 && (stem[end - 1] == ' ' || stem[end - 1] == '.')) {
            --end;
        }
        stem.resize(end);
    }

    // ── Step 4: byte budget ─────────────────────────────────────────────────
    // NAME_MAX is 255 BYTES on ext4 and APFS, and 255 UTF-16 code units on
    // Win32 -- a UTF-8 byte count is never smaller than the UTF-16 length of
    // the same string, so one budget in bytes is a bound on all three. An
    // unbounded title (a 300-byte track title is entirely ordinary) produced a
    // 300-byte name, open() failed with ENAMETOOLONG, and DownloadQueue's
    // diagnostic said only "warning" with no reason the user could act on.
    //
    // An "extension" this long is not an extension, it is a name containing
    // dots; folding it back into the stem keeps the total in budget without
    // inventing a rule for splitting it.
    if (extension.size() >= maxFilenameBytes) {
        stem += extension;
        extension.clear();
    }
    const std::size_t stemBudget = maxFilenameBytes - extension.size();

    // Truncation happens on the byte string, but never mid-sequence: a partial
    // UTF-8 sequence is not a shorter name, it is an invalid one, and the very
    // next decode of it turns it into U+FFFD. `e` therefore only advances by
    // whole sequences that fit entirely inside the budget.
    if (stem.size() > stemBudget) {
        std::size_t e = 0;
        while (e < stemBudget) {
            const std::size_t seq = utf8SequenceLength(static_cast<unsigned char>(stem[e]));
            if (e + seq > stemBudget) break;
            e += seq;
        }
        stem.resize(e);
        // The cut can expose a trailing space or dot, which the whole-string
        // trim above already dealt with and must therefore be re-applied.
        while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) {
            stem.pop_back();
        }
    }

    // ── Step 5: Windows reserved device names, checked LAST ─────────────────
    // Last, not first, because both earlier steps can create one: truncating
    // "COM11" to three characters yields "COM1", which is reserved. Checking
    // before the budget would ship exactly the bug this ordering avoids.
    if (isReservedDeviceStem(stem)) {
        stem.insert(0, "_");
        // The guard byte is the only reason the result can exceed the budget,
        // so pay for it here rather than budgeting for it in every name.
        if (stem.size() + extension.size() > maxFilenameBytes) {
            stem.erase(stem.size() - 1);
            while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) {
                stem.pop_back();
            }
        }
    }

    if (stem.empty()) {
        // An extension on its own (".mp3") is a valid, non-reserved filename
        // on all three filesystems; a fully empty result is not, and the
        // caller concatenates this straight onto a directory.
        return extension.empty() ? "_" : extension;
    }

    return stem + extension;
}

std::string humanSize(std::uintmax_t bytes) {
    const auto comp = byteSizeComponents(bytes);
    if (comp.unitIndex == 0) {
        return std::format("{} {}", bytes, kByteUnits[comp.unitIndex]);
    }
    return std::format("{:.1f} {}", comp.value, kByteUnits[comp.unitIndex]);
}

std::string formatDuration(Duration dur) {
    const auto t = timecodeComponents(dur.count());
    if (t.hours > 0) {
        return std::format("{:02}:{:02}:{:02}", t.hours, t.minutes, t.seconds);
    }
    return std::format("{:02}:{:02}", t.minutes, t.seconds);
}

QString humanSizeQString(vc::u64 bytes) {
  const auto comp = byteSizeComponents(bytes);
  // QML-facing variant historically capped at GB; keep that ceiling.
  const auto unit = std::min(comp.unitIndex, 3);
  if (unit == 0) {
    return QStringLiteral("%1 B").arg(bytes);
  }
  return QStringLiteral("%1 %2")
      .arg(comp.value, 0, 'f', 1)
      .arg(QLatin1String(kByteUnits[unit]));
}

QString formatDurationQString(vc::i64 ms) {
  if (ms <= 0) {
    return QStringLiteral("0:00");
  }

  const auto t = timecodeComponents(ms);
  if (t.hours > 0) {
    return QStringLiteral("%1:%2:%3")
      .arg(static_cast<qlonglong>(t.hours))
      .arg(static_cast<int>(t.minutes), 2, 10, QLatin1Char('0'))
      .arg(static_cast<int>(t.seconds), 2, 10, QLatin1Char('0'));
  }

  return QStringLiteral("%1:%2")
    .arg(static_cast<int>(t.minutes))
    .arg(static_cast<int>(t.seconds), 2, 10, QLatin1Char('0'));
}

QString srtTimecode(qint64 milliseconds) {
  if (milliseconds < 0) {
    milliseconds = 0;
  }
  const auto hours = milliseconds / 3600000;
  const auto minutes = (milliseconds % 3600000) / 60000;
  const auto seconds = (milliseconds % 60000) / 1000;
  const auto millis = milliseconds % 1000;
  return QStringLiteral("%1:%2:%3,%4")
      .arg(static_cast<int>(hours), 2, 10, QLatin1Char('0'))
      .arg(static_cast<int>(minutes), 2, 10, QLatin1Char('0'))
      .arg(static_cast<int>(seconds), 2, 10, QLatin1Char('0'))
      .arg(static_cast<int>(millis), 3, 10, QLatin1Char('0'));
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

// Color's hex codec used to live here, which is why this file needed a
// <regex>, an allocating std::stoi and a second reason to exist. It is now
// src/util/Color.{hpp,cpp}: see that header for the contract and for the list
// of call sites still using the legacy Color::fromHex shim.
