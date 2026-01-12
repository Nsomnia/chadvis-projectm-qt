# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
