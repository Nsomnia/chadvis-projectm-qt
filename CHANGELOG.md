# Changelog (The Chad Edition)

All notable changes to ChadVis are tracked here. We follow [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Warning:** Reading this may cause an uncontrollable urge to install Arch Linux.

---

## [Unreleased] - 2026-04-19

### 🚀 Added
- **Documentation Refactor (The Chad Refresh)**: Nuked the monolithic docs and replaced them with a modular, persona-driven masterpiece.
- **Agent Lore**: Added `.agent/KNOWLEDGE_BASE.md` because even our AI servants deserve decent context.

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
