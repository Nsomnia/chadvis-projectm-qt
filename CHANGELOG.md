# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Older versions archived in [`docs/changelogs/`](docs/changelogs/).

## [Unreleased] - 2026-02-23

### Added
- **QML Workspace Dock**: Added a modern Qt Quick workspace focused on remote Suno playback in `resources/qml/MainWorkspace.qml`.
- **Accordion Navigation Component**: Added `resources/qml/components/AccordionSection.qml` for large, mobile-style vertical navigation.
- **Single-Purpose QML Bridge Layer** (`src/ui/qml/`):
  - `PlaybackViewModel`
  - `PlaylistTrackModel`
  - `SunoClipListModel`
  - `SunoRemoteLibraryViewModel`
  - `RecordingViewModel`
  - `VisualizerViewModel`
  - `QmlWorkspaceHost`

### Changed
- **Main Window Layout**: Replaced the previous right-side tab canvas with a QML accordion workspace dock while keeping the visualizer as the central canvas.
- **Signal Wiring Ownership**: Main window now directly handles recorder frame/audio wiring and visualizer audio feed for clearer responsibility boundaries.
- **Build Configuration**: Added Qt Quick dependencies (`Qml`, `Quick`, `QuickControls2`, `QuickWidgets`) and registered new QML/UI source files.
- **Resource Packaging**: Added QML resources to `resources/chadvis-projectm-qt.qrc`.

## [1.1.0-alpha] - 2026-02-12

### Added
- **Build System Enhancement (v1337.3)**: Enhanced dependency management with intelligent libprojectm handling
  - Added automatic dependency installation for Arch Linux via `pacman`
  - Implemented version-aware libprojectm detection with minimum version check (4.1.0)
  - Added `--system-projectm` flag to force use of system/pacman libprojectm
  - Added `--cpm-projectm` flag to force building libprojectm from source via CPM
  - Smart auto-detection: uses system libprojectm if recent enough, otherwise falls back to CPM
  - Version comparison logic ensures compatibility with system requirements
  - Added `CHADVIS_FORCE_CPM_PROJECTM` CMake option to bypass system detection
- **Build System Enhancement (v1337.2)**: Upgraded the Zsh build system for better transparency and automation
  - Implemented dual-output logging: Build steps now show live in console AND are saved to `logs/build.log`
  - Integrated automated dependency checking and installation for Arch Linux
  - Added `-y` / `--yes` flag to auto-install missing packages via `pacman`
  - Fixed pipe status handling to correctly detect failures in `tee` pipelines
  - Added unbuffered output for real-time visibility of CMake and Ninja progress
  - Added `sync` calls to ensure build logs are flushed to disk on failure.
- **Persistent Suno Authentication**: Implemented persistent cookie storage for Suno authentication to prevent session loss during library updates (~2500 songs)
  - Created `SunoPersistentAuth` class to manage persistent WebEngine profile with cookie storage
  - Updated `SunoBrowserWidget` and `SunoCookieDialog` to use `ForcePersistentCookies` policy
  - Session data stored in `~/.config/chadvis-projectm-qt/webengine/suno/`
  - Cookies persist across application restarts, eliminating need to re-login each session
  - JWT token extraction and user ID parsing from session cookies
  - Logout functionality to clear persistent session data
  - Added modern Chrome User Agent to bypass Google OAuth "browser not supported" errors.
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
- **Modern 1337 Chad GUI**: Custom styled widgets with glassmorphism and cyan accents
  - `CyanSlider`: Animated slider with glow effects
  - `GlowButton`: Glassmorphism buttons with animated glow borders
  - `ToggleSwitch`: Animated toggle with smooth sliding thumb
- **Sidebar Navigation**: Modern icon-based sidebar replacing tab-based dock widgets
- **Lyrics Tool Tab**: Export to SRT, LRC, JSON formats

### Changed
- Default layout changed to sidebar navigation (`useSidebarLayout_ = true`)
- Refactored `SunoController` architecture for cleaner separation
- Track-to-ID mapping uses 4-priority lookup (cache → db → sidecar → API)

### Fixed
- Qt6 WebEngine compatibility in `SunoPersistentAuth.cpp`
- Lyrics export logic for proper `std::string` handling
- `QPropertyAnimation` errors in `CyanSlider`
- Missing includes in `LyricsPanel.cpp`

---

## [1.1.0] - 2026-01-28

### Added
- **Karaoke/Lyrics System**: Complete rewrite with 60fps word-level highlighting
  - `LyricsData` with O(log n) binary search line lookup
  - `LyricsSync` engine for smooth time synchronization
  - `KaraokeRenderer` with glow effects
  - `LyricsPanel` with search and click-to-seek
  - SRT/LRC subtitle export
- **CLI "Rizz Mode"**: Colorized output, 20+ new flags, shell completions
- **Automated Suno Authentication**: Email/password login via Clerk API
- **Intelligent ProjectM v4 Detection**: CMake finds system lib or builds from source
- **Arch Linux Dependency Check**: Auto-detect and install missing packages
- **Zsh-Native Build System**: Hardware detection, timing, logging

### Changed
- Settings system: Full parity across CLI, config file, and GUI
- Build system overhauled for LLM-friendly logging

### Fixed
- ProjectM include path issues on fresh installs

---

[Older versions](docs/changelogs/)
