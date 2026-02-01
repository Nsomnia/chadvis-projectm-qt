# AGENTS.md - Guide for AI Agents

> **For:** AI agents working on the chadvis-projectm-qt project  
> **Version:** 1337.0 (Arch Linux Certified)  
> **Last Updated:** 2026-01-29

Welcome, agent. This document contains essential information for working effectively with this codebase.

---

## 🚀 Quick Start

### Build Commands
Very slow on the users potato system. You can wait until your done your loop/work and then the user will compile and report bugs if desired. For small changes or if neeed to finish tasks you are free to run cmake, optionally with a build system/accelerator.
```bash
bash build.sh --help
bash build.sh build
```
OR manually
```bash
# Standard build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j1

# Debug build (for development)
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(nproc)

# Run tests
./build/tests/unit/unit_tests
./build/tests/integration/integration_tests
```

### Project Structure
```
src/
├── core/           # Config, logging, CLI, Application
├── audio/          # AudioEngine, FFmpeg decoder, playlist
├── visualizer/     # ProjectM bridge, preset management
├── ui/             # Qt widgets, controllers, dialogs
├── suno/           # Suno API integration, database
├── lyrics/         # Karaoke/Lyrics system (NEW)
├── recorder/       # Video recording (FFmpeg)
└── overlay/        # Text overlays
```

---

## 📝 Development Guidelines

### Code Style

**C++20 Features:**
- Use `std::optional`, `std::expected` (or our `Result<>` type)
- Use structured bindings: `auto [x, y] = getPoint();`
- Use designated initializers where appropriate
- Prefer `std::string_view` over `const std::string&` for parameters

**Naming Conventions:**
```cpp
// Classes: PascalCase
class AudioEngine { };

// Functions: camelCase
void processAudioBuffer();

// Member variables: trailing underscore
class Foo {
    int privateMember_;
};

// Constants/Enums: UPPER_SNAKE or kCamelCase
constexpr int MAX_BUFFER_SIZE = 4096;
enum class Color { Red, Green, Blue };

// Private impl: trailing underscore with Impl
class MyClass {
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

### Error Handling

**Use the Result<T> type for fallible operations:**
```cpp
// Good
Result<Data> loadData(const fs::path& path) {
    if (!fs::exists(path)) {
        return Result<Data>::err("File not found: " + path.string());
    }
    // ... load data
    return Result<Data>::ok(data);
}

// Usage
auto result = loadData("file.txt");
if (!result) {
    LOG_ERROR("Failed to load: {}", result.error().message);
    return;
}
auto data = std::move(*result);
```

**Never use raw exceptions for expected failures.** Only use exceptions for truly exceptional conditions (programmer errors, OOM, etc).

### Documentation

**File headers:**
```cpp
/**
 * @file FileName.hpp
 * @brief One-line description
 *
 * Detailed description if needed. Mention patterns used.
 *
 * @author Name (optional)
 * @version X.X.X
 */
```

**Function documentation:**
```cpp
/**
 * @brief Brief description
 * @param param1 Description
 * @param param2 Description
 * @return Description of return value
 * @throws Never (if applicable)
 */
```

---

## 🔄 Git Workflow

### Branch Naming
```
feat/short-description        # New features
fix/short-description         # Bug fixes  
refactor/short-description    # Code restructuring
docs/short-description        # Documentation
chore/short-description       # Maintenance
test/short-description        # Test changes
```

### Commit Messages
```
type(scope): concise description

- Additional context if needed
- Can span multiple lines
- Reference issues: fixes #123

Types: feat, fix, docs, style, refactor, test, chore
```

**Examples:**
```
feat(recorder): add hardware acceleration support

Adds NVENC and VAAPI codec options with automatic
detection. Falls back to software encoding if HW
unavailable.

Closes #42
```

```
fix(ui): resolve race condition in KaraokeWidget

The sync timer wasn't stopped before destruction,
causing crashes on exit. Added proper cleanup.
```

---

## 🛡️ Security Protocols

### Sensitive Data Protection

**ALWAYS create backups before editing sensitive files:**
```bash
# Required before editing any of these:
# - Config files with credentials
# - API keys, tokens, secrets
# - OAuth files
# - .env files

cp "/path/to/sensitive-file" "/path/to/sensitive-file.backup.$(date +%Y%m%d_%H%M%S)"
```

**Sensitive locations:**
- `~/.local/share/opencode/` (API/OAuth keys)
- `~/.config/chadvis-projectm-qt/config.toml` (Suno tokens)
- Any files containing `token`, `secret`, `password`, `api_key`

### Never Commit Secrets
- Use `.gitignore` for local config
- Use environment variables for runtime secrets
- Never log sensitive data (even in debug mode)

---

## 🎯 Key Subsystems

### Configuration System

**Three-tier configuration:**
1. **Defaults** - Hardcoded in `ConfigData.hpp`
2. **Config file** - TOML at `~/.config/chadvis-projectm-qt/config.toml`
3. **CLI overrides** - Command-line arguments

**CLI → Config precedence:**
- CLI flags override config file
- Config file overrides defaults
- All changes are logged: `LOG_INFO("CLI override: key = value")`

**Adding new config options:**
1. Add field to appropriate struct in `ConfigData.hpp`
2. Add CLI option in `Application.cpp` (`AppOptions` + `parseArgs()`)
3. Add GUI control in `SettingsDialog.cpp`
4. Add TOML parser in `ConfigParsers.cpp`

### Lyrics/Karaoke System

**Architecture:**
```
LyricsData → LyricsSync → LyricsRenderer → QWidget
    ↑            ↑            ↑
Suno API    AudioEngine   KaraokeWidget
Database    Position      LyricsPanel
```

**Key classes:**
- `LyricsData` - Immutable data structures with binary search
- `LyricsSync` - 60fps time synchronization
- `LyricsRenderer` - Base class for different renderers
- `KaraokeWidget` - Visual overlay with word highlighting
- `LyricsPanel` - Scrollable lyrics panel with search

### Video Recording

**Thread model:**
- Main thread: Frame capture from OpenGL PBO
- Recorder thread: FFmpeg encoding (async)

**Adding new codecs:**
1. Add codec name to `EncoderSettings.hpp`
2. Update codec validation in `EncoderSettings.cpp`
3. Add to SettingsDialog combo box

### Suno Integration

**Auth flow:**
1. User logs in via `QWebEngineView` (`SunoCookieDialog`)
2. Captures `__session` and `__client` cookies
3. Clerk API provides JWT tokens
4. API calls use Bearer token auth

**API rate limits:**
- Library fetch: max 1 req/5sec
- Lyrics fetch: max 1 req/2sec
- Queue system handles this automatically

---

## 🐛 Debugging

### Debug Logging
```cpp
LOG_TRACE("Very detailed");  // Function entry/exit
LOG_DEBUG("Debug info");     // State changes
LOG_INFO("Normal events");   // User actions
LOG_WARN("Recoverable issues");
LOG_ERROR("Critical errors");
```

### Common Issues

**OpenGL/Visualizer not rendering:**
- Check `QT_QPA_PLATFORM=xcb` (Wayland issues)
- Verify projectM preset path exists
- Check `glxinfo | grep "OpenGL version"`

**Audio not playing:**
- Check `pactl list | grep -A5 "Name:"` for device names
- Verify buffer size isn't too large (causes latency)
- Try different sample rates (44100 vs 48000)

**Recording fails:**
- Check FFmpeg codec availability: `ffmpeg -encoders | grep <codec>`
- Verify output directory is writable
- For NVENC: ensure NVIDIA drivers loaded
- For VAAPI: check `vainfo` output

**Suno auth issues:**
- Cookies expire after ~24 hours
- Use Settings dialog to re-authenticate
- Check `__session` cookie format (should start with "eyJ")

---

## 🧪 Testing

### Unit Tests
```cpp
// tests/unit/test_something.cpp
TEST_CASE("Feature description") {
    REQUIRE(condition);
    REQUIRE_EQ(expected, actual);
}
```

### Manual Testing Checklist

**Basic playback:**
- [ ] Load audio file (MP3, FLAC, WAV, OGG)
- [ ] Play/pause/stop
- [ ] Playlist navigation (next/prev)
- [ ] Volume control
- [ ] Seek bar

**Visualizer:**
- [ ] Presets load and render
- [ ] Hard cut detection works
- [ ] Preset switching (manual and auto)
- [ ] Fullscreen toggle

**Recording:**
- [ ] Start/stop recording
- [ ] Output file is valid
- [ ] Audio sync correct
- [ ] Hardware codecs (if available)

**Karaoke:**
- [ ] Lyrics display with Suno songs
- [ ] Word-level highlighting
- [ ] Click-to-seek
- [ ] Search within lyrics
- [ ] SRT/LRC export

**Suno:**
- [ ] Authentication flow
- [ ] Library sync
- [ ] Song download
- [ ] Lyrics fetch

---

## 📚 Resources

### Documentation Files
- `docs/ARCHITECTURE.md` - System design
- `docs/VIDEO_RECORDING.md` - Recording subsystem
- `docs/SUNO_INTEGRATION.md` - Suno API details
- `docs/CPM_INTEGRATION.md` - Dependency management
- `docs/CHANGELOG.md` - Version history

### External Links
- [projectM v4 docs](https://github.com/projectM-visualizer/projectm)
- [Qt6 docs](https://doc.qt.io/qt-6/)
- [FFmpeg encoding](https://trac.ffmpeg.org/wiki/Encode/H.264)
- [TOML spec](https://toml.io/en/)

---

## 💡 Tips for Agents

1. **Read before writing** - Check existing patterns in similar files
2. **Build frequently** - C++ compile errors are easier to fix immediately
3. **Test edge cases** - Empty files, invalid paths, network failures
4. **Document as you go** - Update this file when adding new patterns
5. **Respect NO_COLOR** - CLI output should respect the environment
6. **Keep functions small** - Single responsibility principle
7. **Use the Result<T> type** - Don't ignore error cases
8. **Check for existing utilities** - Look in `util/` before writing new code

---

## 🆘 Emergency Contacts

**When stuck:**
1. Check `docs/` for subsystem documentation
2. Search codebase for similar implementations
3. Review recent commits for patterns
4. Check `.agent/TODO.md` for current priorities

**Never:**
- Commit to `main` or `master` directly
- Push secrets (even if "just for testing")
- Delete `.agent/TODO.md` entries (mark complete instead)
- Ignore LSP errors (fix or suppress with reason)

---

## File deleteion

- Do not delete any files but rather mv --no-clobber into `.backup_graveyard/`

---

## Ralph-loops and freedoms
- You may continue working on any and all TODO items either directly listed or inferred from documents in any file in .agent/ or AGENTS.md
- Keep the CHANGELOG.md updated. If its gets unmanagable in length then create a history directory to contain changelogs in the docs/ directory.

*"Maximum effort. No compromises. Ship it like it's Arch Linux."* 🚀
