# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2026-01-31

### Added
- **Direct Lyrics Display Injection**: Refactored `onAlignedLyricsFetched` to immediately parse and display lyrics without waiting for database save. Lyrics now appear instantly when API response returns.
- **Direct Mapping Cache**: Added `directLyricsCache_` to store recently fetched lyrics, enabling instant display on track restarts during the same session.
- **Aggressive UUID Extraction**: Implemented `extractClipIdFromTrack()` helper with multi-source detection - checks filename, full path, parent directory, and title-based fuzzy matching.
- **Last Requested ID Fallback**: Added `lastRequestedClipId_` to handle track mapping during playlist transitions when extraction fails.
- **Sidecar File Loading**: `onTrackChanged` now loads lyrics from local `.srt` and `.json` files alongside MP3s, enabling offline lyrics display.
- **Detailed Trace Logging**: Added comprehensive tracing in `OverlayEngine::drawToCanvas` to diagnose why lyric lines might be skipped (timing gaps, before/after range, etc.).
- **Instrumental Tag Handling**: Enhanced `LyricsAligner::parseJson` to preserve instrumental sections with 🎵 placeholder while skipping other tags like [Verse] and [Chorus].

### Changed
- **SunoController Architecture**: Refactored `onAlignedLyricsFetched` into helper methods: `isCurrentlyPlaying()`, `parseAndDisplayLyrics()`, and `saveLyricsSidecar()` for cleaner separation of concerns.
- **Track-to-ID Mapping**: `onTrackChanged` now uses 4-priority lookup: 1) Direct cache, 2) Database, 3) Sidecar files, 4) API fetch.
- **Persistence Strategy**: Sidecar files (.json/.srt) are now saved immediately alongside local MP3s when available.

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
