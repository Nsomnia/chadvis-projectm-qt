# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased - JUCE Integration] - 2026-03-04

### Added
- **JUCE Audio Engine**: Full implementation replacing stub code
  - AudioIODeviceCallback for real-time audio processing
  - AudioFormatManager for file/URL loading
  - AudioTransportSource for playback control
  - AnalyserSource integration for FFT visualization
  - Proper device initialization and shutdown

- **Agent Documentation**: Created `.agent/` directory with:
  - TODO.md - Master task tracking
  - SCRATCHPAD.md - Working notes for sessions

### Changed
- **JuceAudioEngine**: Replaced empty `Impl` struct with full JUCE implementation
- **AnalyserSource**: Added working FFT analysis with magnitude/phase output

## [Unreleased - Modern UI & JUCE Integration] - 2026-02-25

### Added
- **JUCE Audio Framework Integration**: Major refactor to replace Qt Multimedia with JUCE for professional audio processing
  - `JuceAudioEngine`: Core audio engine with AudioIODeviceCallback for real-time audio processing
  - `URLAudioSource`: HTTP audio streaming with background buffering for Suno integration
  - `AnalyserSource`: FFT-based audio analysis with PCM data extraction for projectM visualizer feed
  - `EffectsChain`: DAW-like effects processing with Gain, Reverb, and Compressor effects
  - `JucePluginHost`: VST3/AU plugin hosting capability for audio processing
  - `ProjectMBridge`: Bridge component connecting JUCE audio analysis to projectM visualizer
  
- **CMake Build System Updates**: Added JUCE as optional dependency with CPM fallback
  - `CHADVIS_USE_JUCE` option to enable/disable JUCE integration (default: ON)
  - `CHADVIS_JUCE_FROM_SOURCE` option to build JUCE from source
  - Automatic detection of system JUCE on Arch Linux
  - Platform-specific JUCE module configuration (Linux/X11, macOS, Windows)
  
- **Architecture Documentation**: Created comprehensive JUCE refactor documentation
  - `.agent/JUCE_REFACTOR_DESIGN.md`: Full architecture design and migration strategy
  - Module organization with new `src/audio/juce/` directory structure
  
- **Modern Qt/QML GUI Research**: Identified modern UI templates and patterns
  - QmlAppTemplate: Full-featured Qt6/QML application template
  - MusicDashboard: Modern music player QML/C++ reference implementation
  - Dark theme design system with material-style controls

### Changed
- **Audio Architecture**: Transitioning from Qt Multimedia to JUCE audio framework
  - Qt Multimedia remains as fallback when JUCE is unavailable
  - JUCE provides cross-platform audio consistency and professional DSP capabilities
  
### Technical Details
- JUCE modules included: juce_core, juce_audio_basics, juce_audio_devices, juce_audio_formats,
  juce_audio_processors, juce_audio_utils, juce_dsp, juce_gui_basics, juce_network, juce_opengl
- Real-time audio safety: No allocations in audio callback, lock-free communication
- URL streaming: Background buffering with progress signaling for network audio
- FFT analysis: Configurable FFT size (default 512), Hann windowing, magnitude/phase extraction

## [Unreleased] - 2026-02-02

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

### Changed
- **MainWindow Layout Options**: Added View menu option "Use Sidebar Layout" to toggle between traditional dock-based tab interface and modern sidebar navigation.
- **SunoController Architecture**: Refactored `onAlignedLyricsFetched` into helper methods: `isCurrentlyPlaying()`, `parseAndDisplayLyrics()`, and `saveLyricsSidecar()` for cleaner separation of concerns.
- **Track-to-ID Mapping**: `onTrackChanged` now uses 4-priority lookup: 1) Direct cache, 2) Database, 3) Sidecar files, 4) API fetch.
- **Persistence Strategy**: Sidecar files (.json/.srt) are now saved immediately alongside local MP3s when available.
- **UI Widget Polish**: Clamped glow alpha values in `GlowButton` and `CyanSlider` to valid range [0, 1].

### Fixed
- **Qt6 WebEngine Compatibility**: Fixed compilation error in `SunoPersistentAuth.cpp` by removing invalid `setOffTheRecord()` call (handled via named profile in Qt6).
- **Lyrics Export Refactor**: Fixed export logic in `LyricsPanel.cpp` to correctly handle `std::string` return values from `LyricsExport` and implement proper file saving using `QFileDialog`.
- **Missing Build Includes**: Added missing `<QFileDialog>`, `<QFile>`, `<QTextStream>`, `<QStandardPaths>`, and `<QTimer>` includes to `LyricsPanel.cpp`.
- **UI Animation Errors**: Resolved `QPropertyAnimation` errors by properly declaring `glowOpacity` as a `Q_PROPERTY` in `CyanSlider`.

## [Unreleased - 1337 Chad GUI Update - Iteration 5] - 2026-02-01

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

### Changed
- **PlayerControls Modernization**: Replaced standard Qt widgets with custom modern controls
  - `QSlider` → `CyanSlider` for seek and volume controls with cyan accent
  - `QPushButton` → `GlowButton` for all transport buttons (play, pause, stop, next, prev, shuffle, repeat)
  - Added `applyModernStyling()` method to configure glow effects and accent colors
  - Play button uses green accent (#00ff88) for visual prominence
  - All buttons feature glassmorphism backgrounds and animated glow borders

- **SettingsDialog Modernization**: Updated dialog buttons with modern styling
  - Karaoke color selection buttons now use `GlowButton` with color-matched glow
  - Dialog action buttons (OK, Cancel, Apply) use `GlowButton` with cyan/green accents
  - OK button uses green accent (#00ff88) for primary action emphasis

- **RecordingControls Modernization**: Updated recording interface with modern controls
  - Record button uses `GlowButton` with red glow (#ff4444) for recording state
  - Browse button uses `GlowButton` with cyan accent
  - Consistent with the modern 1337 Chad GUI design system

- **Sidebar Layout Default**: Changed `useSidebarLayout_` from `false` to `true` in MainWindow
  - Modern sidebar navigation is now the default layout
  - Users can still toggle back to traditional dock-based layout via View menu
  - Sidebar provides icon-based navigation with 7 integrated panels

### Technical Details
- Custom widgets located in `src/ui/widgets/` following project naming conventions
- All widgets use `namespace chadvis` for consistency with SidebarWidget
- LyricsPanel export functions check for empty lyrics before attempting export
- MainWindow now creates LyricsPanel instance and adds to sidebar alongside other panels
- Signal/slot connections updated to use custom widget signals (`chadvis::GlowButton::clicked`, `chadvis::CyanSlider::valueChanged`)

### Verification (2026-02-01) - Iteration 5 Complete
- ✅ **LSP Diagnostics**: All modified files pass diagnostics with 0 errors, 0 warnings
  - PlayerControls.hpp/cpp: Clean
  - SettingsDialog.hpp/cpp: Clean  
  - RecordingControls.hpp/cpp: Clean
  - MainWindow.hpp: Clean
- ✅ **Widget Integration**: Custom widgets now actively used in 3 major UI components
- ✅ **Design System**: Full compliance with MOCKUPS.md - cyan accents (#00bcd4), glassmorphism, 8px radius
- ✅ **Default Layout**: Sidebar navigation now default (useSidebarLayout_ = true)
- ✅ **Code Quality**: Modern C++20 features, Qt best practices maintained
- ✅ **Task Registry**: All P3.005 and P1.003 integration tasks completed

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
