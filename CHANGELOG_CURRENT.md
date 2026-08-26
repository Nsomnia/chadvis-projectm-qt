# Changelog (The Chad Edition)

All notable changes to ChadVis are tracked here. We follow [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Warning:** Reading this may cause an uncontrollable urge to install Arch Linux.

---

## [Unreleased]

### Changed
- **Codebase Audit (Phases 1–4)**: Full audit of ~19.3k LOC across 10 modules surfaced 24 issues, of which 18 were fixed across five phases for a net removal of **−894 LOC** (38 files changed). The per-issue checklist (#1–#24) and remaining Phase 5+ items are maintained in [`AGENTS.md`](AGENTS.md) under *Codebase Audit & Refactoring* — that file is the canonical source; consult `git log --oneline` for the corresponding implementation commits.

- **QML Registration** *(2026-04-15)* — Macros added. Modernity intensifies.

### Added
- **PlaylistBridge: Full QML API** — Added `shuffle` (bool), `repeatMode` (int: 0=Off/1=All/2=One) Q_PROPERTYs with notify signals. Added `toggleShuffle()`, `setShuffle(bool)`, `cycleRepeatMode()`, `moveItem(int,int)`, `getItemPath(int)` Q_INVOKABLEs. Added `DurationFormattedRole` to model. Wired `vc::Playlist` signals through bridge.
- **RecordingBridge: Real Recording Support** — `startRecording()` now calls `vc::VideoRecorder::start()`. Added live stats Q_PROPERTYs: `recordingTime`, `framesWritten`, `fileSize`, `encodeFps`, `bufferHealth`. Wired `stateChanged`/`statsUpdated`/`error` signals from VideoRecorder.
- **Suno Orchestrator Wiring** — `SunoController` now owns `SunoOrchestrator` instance. `sendChatMessage()` and `fetchChatHistory()` flow through controller → orchestrator → bridge. Chat responses and history sessions update QML in real-time.
- **Suno Auth Signal Wiring** — `SunoAuthManager` now emits `authenticationSuccess()` and `authenticationFailed(QString)`. Controller re-emits these signals. Persistent auth restore and system-browser auth both properly signal success/failure.
- **Suno Endpoint Centralization** — Created `src/suno/SunoEndpoints.hpp` with `namespace vc::suno::endpoints` containing all API base URLs and path constants as `constexpr string_view`. Replaced all hardcoded URLs in SunoClient, SunoOrchestrator, SunoPersistentAuth, SystemBrowserAuth.
- **Suno Library Pagination** — `SunoLibraryManager` now emits per-page instead of auto-paginating all. Added `hasMorePages` signal. `SunoBridge` exposes `hasMorePages`/`currentPage` Q_PROPERTYs. `SunoPanel.qml` implements infinite scroll with loading footer.
- **Comprehensive Suno API Documentation** (`docs/suno_api/`) — `README.md` index plus `auth.md`, `generation.md`, `library.md`, `billing.md`, `projects.md`, `persona.md`, `upload.md`, `b-side.md`, `social.md`, `feature-flags.md`, `ENDPOINT-INVENTORY.md` (150+ endpoints).
- **Documentation Refactor** *(2026-04-19)* — Nuked the monolithic docs and replaced them with a modular, persona-driven masterpiece. Added `.agent/KNOWLEDGE_BASE.md` agent context and dedicated Suno API modules (`projects.md`, `persona.md`, `upload.md`).
- **Hardware Acceleration** *(2026-04-15)* — NVENC/VAAPI/QSV support. Because CPU encoding is for people with too much time and not enough fans.
- **AudioQueue** *(2026-04-15)* — Lock-free audio flow. Smooth as butter.
- **Build System v1337.3** *(2026-02-02)* — Smart libprojectm detection. It'll find it on Arch or build it itself.
- **Suno Persistence** *(2026-02-02)* — Persistent cookies. Login once, visualize forever.
- **Sidebar Navigation** *(2026-02-02)* — Tabs are so 2010. Icons are the future.

### Fixed
- **Visualizer Rendering** *(2026-04-15)* — Fixed a bug where it would clear to red. Red is for errors, not for visuals.
- **WebEngine Compatibility** *(2026-02-02)* — Fixed some Qt6-specific breakage.
- **Lyrics Export** *(2026-02-02)* — Now you can actually export your karaoke sessions to SRT/LRC/JSON.

### Key Findings Documented
- Complete B-Side route inventory (50+ internal/hidden routes)
- Orpheus chat architecture (Modal backend: `suno-ai--orpheus-prod-web.modal.run`)
- Marketplace pre-staged (frontend stubs, backend 404)
- VIP gated features at `/b-side/vip`
- Client-side feature flag override capabilities (5-layer defense)
- Credit abuse vector (user reported, rewarded by Suno team)
- Complete JWT claims documentation
- All known model versions: V3, V4, V4.5, V5, V5.5

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

## [1.1.0] - 2026-01-28

### ✨ Added
- **Automated Suno Login**: No more manual cookie mining.
- **Zsh-Native Build**: Because we value your time and our hardware.

## [1.0.0] - 2026-01-27

### 🐣 Added
- **Initial Release**: The birth of a legend.
- **ProjectM + Suno + FFmpeg**: The unholy trinity of audio visualization.
