# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Browser Extension Authentication**: Added TokenServer for seamless Suno auth via Chrome extension
  - Local HTTP server on 127.0.0.1:38945 receives tokens from browser extension
  - Chrome extension automatically extracts Clerk JWT tokens from suno.com
  - Auto-refresh based on token expiry (JWT-aware scheduling)
  - No need for manual cookie extraction - just install extension and login to Suno
  - Extension located in `resources/browser_extension/`
- **Build System Enhancement (v1337.3)**: Enhanced dependency management with intelligent libprojectm handling
  - Automatic dependency installation for Arch Linux via `pacman` with `-y` flag
  - Version-aware libprojectm detection (minimum version: 4.1.0)
  - `--system-projectm` flag to force use of system/pacman libprojectm
  - `--cpm-projectm` flag to force building libprojectm from source via CPM
  - Smart auto-detection: uses system libprojectm if recent enough, otherwise falls back to CPM
  - `CHADVIS_FORCE_CPM_PROJECTM` CMake option to bypass system detection

### Fixed
- **KaraokeVideoExporter**: Fixed compilation errors
  - Corrected namespace references (`LyricsData` in `vc` namespace)
  - Fixed `AudioEngine::duration()` return type handling (Duration → milliseconds)
  - Updated to use `VideoRecorder` and `EncoderSettings` APIs correctly
  - Added proper `VideoRecorderCore.hpp` include
- **AudioBridge**: Fixed namespace issues
  - Changed `vc::visualizer::projectm::Engine` to `vc::pm::Engine`
  - Updated `setPCM()` to `addPCMDataInterleaved()` for correct ProjectM API
- **Application**: Fixed `AudioBridge` namespace
  - Forward declaration now uses `vc::audio::AudioBridge`
  - Member and getter updated to use correct namespace
- **KaraokeVideoExporter**: Added missing `#include <QColor>`

## [1.1.0] - 2026-01-29

### Added
- **Karaoke/Lyrics System**: Complete rewrite with 60fps word-level highlighting
  - New `LyricsData` with binary search for O(log n) line lookup
  - `LyricsSync` engine for smooth time synchronization
  - `KaraokeRenderer` with glow effects and instrumental break display
  - `LyricsPanel` canvas tab with search and click-to-seek
  - SRT/LRC subtitle export support
- **CLI Enhancements** ("Rizz Mode"):
  - Colorized output with TTY detection (respects `NO_COLOR`)
  - 20+ new CLI flags for complete settings coverage
  - Multi-stage help system (`--help <topic>`, `--help-topics`)
  - Smart error messages with typo suggestions
  - Shell completion script generation (bash, zsh, fish)
- **Documentation**:
  - Comprehensive `AGENTS.md` guide for AI developers
  - Settings system unification documentation

### Changed
- **Settings System**: Full parity across CLI, config file, and GUI
  - All configurable options accessible via all interfaces
  - CLI flags override config file, which overrides defaults
  - Automatic override logging for debugging

## [1.0.2-RC1] - 2026-01-11

### Fixed
- **Rendering**: Resolved the "Black Screen" issue during recording by implementing a shader-based blit that forces alpha to 1.0.
- **Stability**: Fixed an infinite recording loop caused by the `recordEntireSong` flag incorrectly triggering auto-starts.
- **Stability**: Fixed application hangs and `SIGSEGV` during shutdown by migrating `VideoRecorder` to `std::jthread` and adding explicit OpenGL resource cleanup.
- **Build**: Fixed `kissfft` fetch failure by correcting the git tag versioning.
- **Build**: Fixed `build.zsh` logic error that misreported valid commands as "unknown" on failure.

### Added
- **Features**: Implemented "Record Entire Song", "Restart Track on Start", and "Stop at Track End" recording options.
- **Features**: Added support for multiple `texture_paths` in configuration.
- **Integration**: Switched to native ProjectM v4 callbacks for more reliable preset synchronization.
- **Optimization**: Added support for `mold` linker, `sccache`/`ccache`, and Unity Builds to the `build.zsh` script.
- **Developer Experience**: Added `build.sh` wrapper and enhanced agent-readable logging in `.agent/`.

## [1.0.1] - 2026-01-08

### Fixed
- **Memory Safety**: Resolved `SIGSEGV` (Address boundary error) in `VisualizerWindow::feedAudio` by adding support for mono and multi-channel audio upmixing.
- **Stability**: Fixed a persistent crash on application exit by reordering component destruction in `Application.hpp` and ensuring the `VideoRecorder` encoding thread is joined during destruction.
- **UI/UX**: Fixed "stuck preset" bug where navigation would stop working after certain preset selection events.
- **UI/UX**: Improved `MarqueeLabel` to correctly inherit colors from stylesheets and smoothly loop long preset names.

### Added
- **Navigation**: Implemented Preset Navigation History. The "Previous" button now reliably returns to the last seen preset in a 100-item stack.
- **Documentation**: Initialized `CHANGELOG.md` and updated `AGENT.md` with versioning guidelines.

## [1.0.0] - 2026-01-01

### Initial Release
- projectM v4 integration with Qt6.
- Real-time audio analysis and visualization.
- FFmpeg-based video recording.
- Text overlay engine.
- Suno AI integration.
