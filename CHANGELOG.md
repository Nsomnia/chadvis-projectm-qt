# Changelog (The Chad Edition)

All notable changes to ChadVis are tracked here. We follow [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Warning:** Reading this may cause an uncontrollable urge to install Arch Linux.

---

## [Unreleased]
### Changed
- **Codebase Audit (Phases 1-4)**: Full audit of 19,294 LOC across 10 modules. 24 issues found, 18 fixed. Net impact: **-894 LOC removed** (38 files changed, 2022 deletions, 1128 insertions).
- **Lyrics Unification** (#1/#12): `LyricsFactory` is now the canonical parser. `SunoLyrics` delegates and converts at boundaries. Removed dead `LyricAligner.hpp`. Exposed `alignWordsToLines()` publicly.
- **SettingsBridge Macro System** (#3): X-macro table + `SettingMacros.hpp` reduced boilerplate from 453→~120 LOC. `resetToDefaults()` signal emissions auto-generated.
- **CLI Argument Table** (#5/#13): Per-type X-macro table (`CliArgs.inc`) + `applyOverride<T>()` templates replaced 250 LOC if/else chain + 80 LOC override boilerplate. Application.cpp: 687→584 LOC.
- **VisualizerBridge Wired** (#10): All stubs now delegate to real `VisualizerWindow`. Added `visualizerWindow` Q_PROPERTY + `actualFps()` accessor.
- **SunoController Lyrics Dedup** (#6/#23): Removed 84-line fallback JSON parser. `onTrackChanged()` uses `LyricsFactory` directly.
- **SunoDatabase Dedup** (#2): Extracted `clipFromQuery()` helper (3 identical blocks → 1).
- **SunoDownloader Dedup** (#7/#8): Extracted `getDownloadDir()` (5x) + `sanitizeFilename()` (4x).
- **Format Utilities Unified** (#9/#22): `formatDurationQString()` + `humanSizeQString()` in `vc::file` namespace. Removed 3 local duplicates.
- **Namespace Migration** (#11): `chadvis` → `vc::ui` in SunoPersistentAuth/SystemBrowserAuth.
- **Lerp Consolidation** (#15): Single `vc::lerp()` in Types.hpp. Removed 3 duplicates.

### Fixed
- **Broken QML Theme Refs** (#4): `KaraokeMaster.qml` + `KaraokeSettings.qml` referenced non-existent Theme properties (`onSurface`, `fontSizeMedium`, etc.). Fixed to use actual Theme.qml API.
- **Duplicate GL State** (#18): Removed redundant glViewport/glDisable/glEnable in `VisualizerQFBO::render()`.
- **Stale CMake Ref** (#24): Removed `${KISSFFT_INCLUDE_DIRS}` (project uses PFFFT).
- **Orphaned License Block** (#16): Removed MIT license header from `SunoAuthManager.hpp` (only file with it).
- **Duplicate Include** (#17): Removed redundant `#include <QtSql/QSqlDatabase>` from `SunoDatabase.hpp`.

### Removed
- **Dead LyricsLoader.hpp** (#19): Header-only, no .cpp, not in CMakeLists, zero includes. Archived to `.backup_graveyard/lyrics/`.
- **Orphaned OverlayEngine forward-decl** (#21): Class doesn't exist. Removed from `Types.hpp` + `Application.hpp`.

### Added
- **PlaylistBridge: Full QML API** — Added `shuffle` (bool), `repeatMode` (int: 0=Off/1=All/2=One) Q_PROPERTYs with notify signals. Added `toggleShuffle()`, `setShuffle(bool)`, `cycleRepeatMode()`, `moveItem(int,int)`, `getItemPath(int)` Q_INVOKABLEs. Added `DurationFormattedRole` to model. Wired `vc::Playlist` signals through bridge.
- **RecordingBridge: Real Recording Support** — `startRecording()` now calls `vc::VideoRecorder::start()`. Added live stats Q_PROPERTYs: `recordingTime`, `framesWritten`, `fileSize`, `encodeFps`, `bufferHealth`. Wired `stateChanged`/`statsUpdated`/`error` signals from VideoRecorder.
- **Suno Orchestrator Wiring** — `SunoController` now owns `SunoOrchestrator` instance. `sendChatMessage()` and `fetchChatHistory()` flow through controller → orchestrator → bridge. Chat responses and history sessions update QML in real-time.
- **Suno Auth Signal Wiring** — `SunoAuthManager` now emits `authenticationSuccess()` and `authenticationFailed(QString)`. Controller re-emits these signals. Persistent auth restore and system-browser auth both properly signal success/failure.
- **Suno Endpoint Centralization** — Created `src/suno/SunoEndpoints.hpp` with `namespace vc::suno::endpoints` containing all API base URLs and path constants as `constexpr string_view`. Replaced all hardcoded URLs in SunoClient, SunoOrchestrator, SunoPersistentAuth, SystemBrowserAuth.
- **Suno Library Pagination** — `SunoLibraryManager` now emits per-page instead of auto-paginating all. Added `hasMorePages` signal. `SunoBridge` exposes `hasMorePages`/`currentPage` Q_PROPERTYs. `SunoPanel.qml` implements infinite scroll with loading footer.
- **Comprehensive Suno API Documentation** (`docs/suno_api/`):
  - `README.md` - API overview, base URLs, auth flow, model versions
  - `auth.md` - Clerk auth, JWT exchange, session endpoints, cookies, headers
  - `generation.md` - Music/lyrics generation, concat, extend, cover, remix, aligned lyrics, WAV conversion
  - `library.md` - Feed v1/v2/v3, clips, playlists, search, trending
  - `billing.md` - Credits, plans, subscription management, credit abuse notes
  - `projects.md` - Projects API, Studio endpoints, collaboration
  - `persona.md` - Persona creation, custom models, voice cloning (V5.5)
  - `upload.md` - Audio/image/video upload, stem processing, video generation
  - `b-side.md` - B-Side internal routes, Labs features, Orpheus chat, Marketplace, VIP
  - `social.md` - Profiles, comments, notifications, sharing, social feed
   - `feature-flags.md` - Statsig integration, feature gates, override scripts
   - `ENDPOINT-INVENTORY.md` - Complete catalog of 150+ endpoints in CSV-style table

### Key Findings Documented
- Complete B-Side route inventory (50+ internal/hidden routes)
- Orpheus chat architecture (Modal backend: `suno-ai--orpheus-prod-web.modal.run`)
- Marketplace pre-staged (frontend stubs, backend 404)
- VIP gated features at `/b-side/vip`
- Client-side feature flag override capabilities (5-layer defense)
- Credit abuse vector (user reported, rewarded by Suno team)
- Complete JWT claims documentation
- All known model versions: V3, V4, V4.5, V5, V5.5

## [Unreleased] - 2026-04-19

### 🚀 Added
- **Documentation Refactor (The Chad Refresh)**: Nuked the monolithic docs and replaced them with a modular, persona-driven masterpiece.
- **Agent Lore**: Added `.agent/KNOWLEDGE_BASE.md` because even our AI servants deserve decent context.
- **Suno API Modules**: Added dedicated `projects.md`, `persona.md`, and `upload.md` docs for reverse-engineered project, voice, and processing endpoints.

---

## [2.1.0] - 2026-04-20

### Added
- **Overlay System**: Implemented hardware-accelerated QML text overlays.
  - New `VisualizerOverlay.qml` component for rendering overlays over the OpenGL canvas.
  - New `OverlayBridge` C++ class for managing overlay state and persistence via `overlays.json`.
  - Added shadow shader (`shadow.frag`) for improved text readability.
  - Supported animations: Fade Pulse, Scroll Left, Scroll Right, Bounce.

### Fixed
- **Recording Bridge**: Fixed missing `outputPathChanged` signal causing compilation failure.
- **Audio Logic**: Decoupled FFT analysis from the audio thread using a background worker and lock-free queues.
- **Overlay Panel**: Ported `OverlayPanel.qml` to use `OverlayBridge` for state management instead of local QML variables.

## [2.0.0] - 2026-04-19


### Changed
- **Enforced C++23 Standard**: Updated `CMakeLists.txt` to require `CXX_STANDARD 23` and `CXX_STANDARD_REQUIRED ON`.
  - Updated documentation to reflect C++23 as the project standard.
  - Verified build compatibility with GCC on Arch Linux.

## [Unreleased] - 2026-04-15

### ⚡ Added
- **Hardware Acceleration**: NVENC/VAAPI/QSV support. Because CPU encoding is for people with too much time and not enough fans.
- **AudioQueue**: Lock-free audio flow. Smooth as butter.

### 🛠️ Changed
- **Visualizer Rendering**: Fixed a bug where it would clear to red. Red is for errors, not for visuals.
- **QML Registration**: Macros added. Modernity intensifies.

---

## [Unreleased] - 2026-02-02

### 🏗️ Added
- **Build System v1337.3**: Smart libprojectm detection. It'll find it on Arch or build it itself. It's smarter than your average sysadmin.
- **Suno Persistence**: Persistent cookies. Login once, visualize forever.
- **Sidebar Navigation**: Tabs are so 2010. Icons are the future.

### 🩹 Fixed
- **WebEngine Compatibility**: Fixed some Qt6-specific breakage.
- **Lyrics Export**: Now you can actually export your karaoke sessions to SRT/LRC/JSON.

---

## [1.1.0] - 2026-01-28

### ✨ Added
- **Automated Suno Login**: No more manual cookie mining.
- **Zsh-Native Build**: Because we value your time and our hardware.

---

## [1.0.0] - 2026-01-27

### 🐣 Added
- **Initial Release**: The birth of a legend.
- **ProjectM + Suno + FFmpeg**: The unholy trinity of audio visualization.
