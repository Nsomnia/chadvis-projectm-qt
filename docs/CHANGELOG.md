# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **QML Modern GUI Refactor**: Complete Qt Widgets → QML/Qt Quick transition
- New QML bridge architecture with singleton pattern (AudioBridge, PlaylistBridge, VisualizerBridge, RecordingBridge)
- VisualizerItem QQuickItem for ProjectM OpenGL rendering in QML
- Theme.qml singleton with glassmorphism styling and cyan accent (#00bcd4)
- AccordionPanel/AccordionContainer components for collapsible sidebar UI
- PlaybackPanel.qml and PlaylistPanel.qml with model bindings
- `--qml` CLI flag to launch QML interface instead of Qt Widgets
- CMake integration with `qt_add_qml_module` and QML cache compilation
- **Build System Enhancement (v1337.3)**: Enhanced dependency management with intelligent libprojectm handling
- Automatic dependency installation for Arch Linux via `pacman` with `-y` flag
- Version-aware libprojectm detection (minimum version: 4.1.0)
- `--system-projectm` flag to force use of system/pacman libprojectm
- `--cpm-projectm` flag to force building libprojectm from source via CPM
- Smart auto-detection: uses system libprojectm if recent enough, otherwise falls back to CPM
- `CHADVIS_FORCE_CPM_PROJECTM` CMake option to bypass system detection

### Changed
- **Visualizer**: ProjectM now renders continuously with silent PCM data when idle,
ensuring visualizations remain active even with no audio playing
- **Idle State**: Removed placeholder overlay (music note icon) that was covering
the visualizer canvas. The visualizer is now always visible, rendering idle
patterns when no audio is playing via `VisualizerItem::feedSilentAudio()`
- **OpenGL Integration**: Fixed `VisualizerItem` to inherit from `QOpenGLFunctions_3_3_Core`
(matching `VisualizerRenderer`) for proper projectM v4 OpenGL 3.3 compatibility
- **Render Target**: Fixed early return bug in `VisualizerRenderer::renderFrame()` that
prevented rendering when render target wasn't valid (only needed for recording mode)
- **Initialization**: Added proper error logging for render target creation failures
- **Qt 6 RHI Compatibility**: Added `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)`
to force OpenGL backend for projectM compatibility with Qt 6's RHI abstraction
- **External Commands**: Wrapped OpenGL rendering with `beginExternalCommands()`/`endExternalCommands()`
as required by Qt 6 for proper scene graph integration
- **AudioBridge Signals**: Fixed missing `connectSignals()` call in BridgeRegistration that
prevented playback state from syncing to QML UI

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
