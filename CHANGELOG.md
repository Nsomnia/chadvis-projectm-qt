# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2026-02-01

### Added
- **Non-Modal Suno Browser Widget**: Created `SunoBrowserWidget` as an embedded alternative to the modal `SunoCookieDialog`. Provides non-blocking authentication flow that can be docked or used as a floating panel.
- **Modern Sidebar Navigation**: Implemented `SidebarWidget` replacing the tab-based dock widget. Features icon-based navigation with collapsible sections, organized into Library, Visualizer, Recording, Overlays, Karaoke, Suno, and Settings panels.
- **Theme System Verification**: Verified theming system is fully operational with dark, gruvbox, and nord themes properly loading via QSS resource files.
- **Direct Lyrics Display Injection**: Refactored `onAlignedLyricsFetched` to immediately parse and display lyrics without waiting for database save. Lyrics now appear instantly when API response returns.
- **Direct Mapping Cache**: Added `directLyricsCache_` to store recently fetched lyrics, enabling instant display on track restarts during the same session.
- **Aggressive UUID Extraction**: Implemented `extractClipIdFromTrack()` helper with multi-source detection - checks filename, full path, parent directory, and title-based fuzzy matching.
- **Last Requested ID Fallback**: Added `lastRequestedClipId_` to handle track mapping during playlist transitions when extraction fails.
- **Sidecar File Loading**: `onTrackChanged` now loads lyrics from local `.srt` and `.json` files alongside MP3s, enabling offline lyrics display.
- **Detailed Trace Logging**: Added comprehensive tracing in `OverlayEngine::drawToCanvas` to diagnose why lyric lines might be skipped (timing gaps, before/after range, etc.).
- **Instrumental Tag Handling**: Enhanced `LyricsAligner::parseJson` to preserve instrumental sections with 🎵 placeholder while skipping other tags like [Verse] and [Chorus].

### Changed
- **MainWindow Layout Options**: Added View menu option "Use Sidebar Layout" to toggle between traditional dock-based tab interface and modern sidebar navigation.
- **SunoController Architecture**: Refactored `onAlignedLyricsFetched` into helper methods: `isCurrentlyPlaying()`, `parseAndDisplayLyrics()`, and `saveLyricsSidecar()` for cleaner separation of concerns.
- **Track-to-ID Mapping**: `onTrackChanged` now uses 4-priority lookup: 1) Direct cache, 2) Database, 3) Sidecar files, 4) API fetch.
- **Persistence Strategy**: Sidecar files (.json/.srt) are now saved immediately alongside local MP3s when available.

## [Unreleased - 1337 Chad GUI Update] - 2026-02-01

### Added
- **Modern 1337 Chad GUI Custom Controls** (P3.005): Created custom styled widgets for modern UI redesign
  - `CyanSlider`: Horizontal slider with cyan accent (#00bcd4), glow effects, and smooth animations
  - `GlowButton`: Glassmorphism button with animated glow border, press feedback, and modern aesthetics  
  - `ToggleSwitch`: Animated toggle switch with smooth sliding thumb, cyan/grey states, and optional labels
  - All controls follow the design system from MOCKUPS.md: dark background (#1a1a1a), cyan accent (#00bcd4), 8px border radius
- **Lyrics Tool Tab** (P1.003): Full integration of `LyricsPanel` into the sidebar
  - Added "Lyrics" panel to modern sidebar navigation (icon: text-x-generic)
  - Export functionality for subtitle files: SRT, LRC, and JSON formats
  - Export buttons integrated into LyricsPanel toolbar with cyan styling
  - `lyricsExported()` signal emitted on successful export with format and file path
  - Exports use existing `LyricsExport::toSrt()`, `toLrc()`, `toJson()` functions

### Technical Details
- Custom widgets located in `src/ui/widgets/` following project naming conventions
- All widgets use `namespace chadvis` for consistency with SidebarWidget
- LyricsPanel export functions check for empty lyrics before attempting export
- MainWindow now creates LyricsPanel instance and adds to sidebar alongside other panels

### Verification (2026-02-01)
- ✅ **LSP Diagnostics**: All 12 new/modified files pass diagnostics with 0 errors, 0 warnings
- ✅ **CMakeLists.txt**: All source files properly included (lines 331-340)
- ✅ **MainWindow Integration**: SidebarWidget and LyricsPanel integrated (lines 151-171)
- ✅ **Design System**: Full compliance with MOCKUPS.md color palette and spacing
- ✅ **Code Quality**: Modern C++20 features, Qt best practices, proper documentation
- ✅ **Task Registry**: P1.003 and P3.005 marked as completed

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
