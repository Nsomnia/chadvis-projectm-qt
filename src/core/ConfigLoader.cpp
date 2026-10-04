#include "ConfigLoader.hpp"
#include <fstream>
#include <system_error>
// Q_OS_WIN must be known before the platform block below, so this is explicit
// rather than inherited from whatever Qt header happens to be included first.
#include <QtGlobal>
#include "Config.hpp"
#include "ConfigParsers.hpp"
#include "Logger.hpp"
#include "util/FileUtils.hpp"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vc {

namespace {

/// Flush a file's contents to stable storage so a crash immediately after the
/// rename cannot leave a half-written config behind. std::ofstream exposes no
/// fsync and no handle, so the path is reopened instead: both flush primitives
/// require write access, and neither flag below truncates or writes anything --
/// the bytes are already in the page cache from the ofstream that just closed.
void flushToDisk(const fs::path& path) {
#ifdef Q_OS_WIN
    // FlushFileBuffers fails with ERROR_ACCESS_DENIED on a read-only handle.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_WARN("Config save: could not reopen {} to flush it: error {}",
                 path.string(), static_cast<int>(::GetLastError()));
        return;
    }
    const BOOL flushed = ::FlushFileBuffers(h);
    // Captured before CloseHandle, which may itself clobber the last error.
    const DWORD flushError = ::GetLastError();
    ::CloseHandle(h);
    if (!flushed)
        LOG_WARN("Config save: could not flush {}: error {}", path.string(),
                 static_cast<int>(flushError));
#else
    // fsync(2) is specified to fail with EBADF on a descriptor that is not
    // open for writing, so this must be O_WRONLY -- and must NOT carry
    // O_TRUNC, which would empty the file we are about to rename into place.
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        LOG_WARN("Config save: could not reopen {} to flush it: errno {}",
                 path.string(), errno);
        return;
    }
    const int rc = ::fsync(fd);
    const int syncErrno = errno;
    ::close(fd);
    if (rc != 0)
        LOG_WARN("Config save: could not flush {}: errno {}", path.string(),
                 syncErrno);
#endif
}

/// Rename `temp` over `dest`, replacing an existing destination where the
/// platform insists on being told. std::filesystem::rename is atomic on POSIX,
/// but on Windows it fails when the destination exists -- which broke every
/// settings save after the very first one.
bool replaceFile(const fs::path& temp, const fs::path& dest,
                 std::error_code& ec) {
#ifdef Q_OS_WIN
    if (::MoveFileExW(temp.c_str(), dest.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec.clear();
        return true;
    }
    ec = std::error_code(static_cast<int>(::GetLastError()),
                         std::system_category());
    return false;
#else
    // The error_code overload returns void; the success signal is a cleared
    // ec, not a bool.
    fs::rename(temp, dest, ec);
    return !ec;
#endif
}

/// Drop a temp file, reporting rather than hiding a failed cleanup.
void discardTemp(const fs::path& temp) {
    std::error_code ec;
    fs::remove(temp, ec);
    if (ec)
        LOG_WARN("Config save: could not remove stale temp file {}: {}",
                 temp.string(), ec.message());
}

/// The runtime paths that only the first-run branch of loadDefault resolves.
/// A config file that cannot be parsed leaves them empty, which is not a usable
/// default state, so both paths funnel through here.
void resolveFirstRunPaths(Config& config) {
    const auto dataDir = file::dataDir();
    config.visualizer().presetPath = file::presetsDir();
    config.recording().outputDirectory = dataDir / "recordings";
    (void)file::ensureDir(config.recording().outputDirectory);
}

} // namespace

Result<void> ConfigLoader::load(Config& config, const fs::path& path) {
    try {
        auto tbl = toml::parse_file(path.string());

        if (auto gen = tbl["general"].as_table()) {
            // toml::table_view::get() returns a raw pointer that is nullptr for
            // a missing key, so the old `get("debug")->value_or(false)` was a
            // null dereference on any config with a [general] table but no
            // debug key. contains()/operator[] is the null-safe spelling.
            bool debug = false;
            if (auto* flag = (*gen)["debug"].as_boolean())
                debug = flag->get();
            config.setDebug(debug);
        }

        ConfigParsers::parseAudio(tbl, config.audio());
        ConfigParsers::parseVisualizer(tbl, config.visualizer());
        ConfigParsers::parseRecording(tbl, config.recording());
        ConfigParsers::parseOverlay(tbl, config.overlay());
        ConfigParsers::parseUI(tbl, config.ui());
        ConfigParsers::parseKeyboard(tbl, config.keyboard());
        ConfigParsers::parseSuno(tbl, config.suno());
        ConfigParsers::parseKaraoke(tbl, config.karaoke());

        config.markClean();
        LOG_INFO("Config loaded from: {}", path.string());
        return Result<void>::ok();
    } catch (const toml::parse_error& err) {
        LOG_ERROR("Config parse error in {}: {}", path.string(), err.what());
        return Result<void>::err(std::string("Config parse error: ") +
                                 err.what());
    } catch (const std::exception& err) {
        // Backstop, not a second parser path. A hand-edited value used to throw
        // from deep inside a conversion -- Color::fromHex used std::stoi, which
        // throws std::invalid_argument on a 6- or 8-character non-hex value
        // (`accent_color = 'zzzzzz'`), and that escaped a
        // catch(toml::parse_error) to terminate the app. It no longer throws:
        // parseHexColor in util/Color.cpp is allocation-free and returns a
        // Result, and Color::fromHex is a shim over it that logs the defect.
        // This catch stays because the next conversion someone adds can throw
        // too, and a diagnostic beats a dead process. Note load() is not
        // transactional: sections parsed before the throw are already applied.
        LOG_ERROR("Failed to read config {}: {}", path.string(), err.what());
        return Result<void>::err(std::string("Failed to read config: ") +
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
    // Best-effort directory creation; failures are non-fatal on load.
    (void)file::ensureDir(dataDir);
    (void)file::ensureDir(cacheDir);
    (void)file::ensureDir(dataDir / "presets");
    (void)file::ensureDir(cacheDir / "logs");

    if (fs::exists(defaultPath)) {
        config.configPath_ = defaultPath;
        if (auto loaded = load(config, defaultPath))
            return loaded;

        // A config that cannot be parsed must not leave empty runtime paths
        // behind -- the caller continues on in-memory defaults, and an empty
        // preset path or recording directory is not a usable default state.
        // The bad file is deliberately NOT overwritten here: it may be
        // hand-recoverable, and the debounced autosave will replace it once
        // the user changes anything.
        LOG_ERROR("Config at {} is unreadable - continuing with defaults; it will be rewritten on the next settings save",
                  defaultPath.string());
        resolveFirstRunPaths(config);
        config.markDirty();
        return Result<void>::ok();
    }

    // Probe for an installed system template before generating one. Only
    // distro packaging on Linux ships the template at a fixed FHS path; on
    // other platforms there is no guaranteed location, so skip straight to
    // the generated default instead of probing a path that cannot exist.
#ifdef __linux__
    constexpr fs::path systemDefault =
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
#endif

    // Generate default config with 1337 comments
    LOG_INFO("No config found - generating fresh default config with Arch-tier quality");
    resolveFirstRunPaths(config);
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
    toml::table tbl;
    try {
        tbl = ConfigParsers::serialize(config.audio(),
                                       config.visualizer(),
                                       config.recording(),
                                       config.ui(),
                                       config.keyboard(),
                                       config.suno(),
                                       config.karaoke(),
                                       config.overlay(),
                                       config.debug());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to serialize config: {}", e.what());
        return Result<void>::err(std::string("Failed to serialize config: ") +
                                 e.what());
    }

    fs::path tempPath = path;
    tempPath += ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            // A .tmp left by an earlier crash would sit here forever otherwise.
            discardTemp(tempPath);
            LOG_ERROR("Failed to open temp config file for writing: {}",
                      tempPath.string());
            return Result<void>::err("Failed to open temp config file for writing: " +
                                     tempPath.string());
        }
        file << tbl;
        file.flush();
        // Close before replacing anything: a partially flushed stream renamed
        // over the real config is exactly the corruption this dance prevents.
        const bool complete = static_cast<bool>(file);
        file.close();
        if (!complete) {
            discardTemp(tempPath);
            LOG_ERROR("Failed while writing config to {}", tempPath.string());
            return Result<void>::err("Failed while writing config to " +
                                     tempPath.string());
        }
    }

    // Owner-only rather than the 0644 a default umask produces. The config no
    // longer carries a credential, but it does carry a stable install
    // identifier, and a settings file has no reason to be world-readable.
    std::error_code permEc;
    fs::permissions(tempPath,
                    fs::perms::owner_read | fs::perms::owner_write, permEc);
    if (permEc)
        LOG_WARN("Config save: could not restrict permissions on {}: {}",
                 tempPath.string(), permEc.message());

    flushToDisk(tempPath);

    std::error_code ec;
    if (!replaceFile(tempPath, path, ec)) {
        discardTemp(tempPath);
        LOG_ERROR("Failed to replace {} with {}: {}", path.string(),
                  tempPath.string(), ec.message());
        return Result<void>::err("Failed to replace " + path.string() + ": " +
                                 ec.message());
    }

    LOG_DEBUG("Config saved to: {}", path.string());
    return Result<void>::ok();
}

} // namespace vc
