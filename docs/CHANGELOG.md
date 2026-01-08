# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
