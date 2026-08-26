#include "FileUtils.hpp"
#include <algorithm>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>
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
    std::string safe;
    safe.reserve(name.size());
    for (const char c : name) {
        // Replace path separators so a crafted title can never escape the
        // intended directory.
        if (c == '/' || c == '\\') {
            safe.push_back('_');
        } else {
            safe.push_back(c);
        }
    }
    return safe;
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

namespace vc {

Color Color::fromHex(std::string_view hex) {
    Color c;
    std::string h(hex);

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
