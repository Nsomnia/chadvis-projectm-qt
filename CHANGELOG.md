# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-01-28

### Added
- **Automated Suno Authentication**: Implemented email/password login flow using Clerk API, eliminating manual cookie extraction.
- **Intelligent ProjectM v4 Detection**: Added robust CMake logic to find system `libprojectm` (v4) or build from source via CPM as a fallback.
- **Arch Linux Dependency Check**: Enhanced build script to detect missing Arch packages and provide one-liner install commands.
- **Zsh-Native Build System**: Refactored build scripts to use Zsh built-ins for hardware detection, timing, and logging.

### Changed
- **Build System Overhaul**: Rewrote `build.sh` and `scripts/build.zsh` for better performance, visual feedback, and LLM-friendly logging.
- **Configuration**: Added `email` and `password` fields to Suno configuration.

### Fixed
- **ProjectM Include Path**: Resolved issues with `projectM-4/projectM.h` not being found on fresh installs.

## [1.0.0] - 2026-01-27

### Added
- Initial release of ChadVis ProjectM Qt.
- Basic Suno AI integration with manual cookie support.
- Real-time audio visualization using ProjectM.
- Karaoke lyrics overlay.
