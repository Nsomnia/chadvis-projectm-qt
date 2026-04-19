# MASTER TODO.md — QML Modern GUI Refactor & C++23 Modernization

> **Branch:** `refactor/qml-modern-gui`
> **Last Updated:** 2026-04-14
> **Version:** 2.0.0

---

## 🤖 AGENT DIRECTIVES — READ BEFORE WORKING

> These rules are **permanent**. Agents must follow them at all times. Violations will be reverted.

1. **DO NOT REMOVE ANY ITEMS.** Only the **user** may mark items as complete (`[x]`) or remove them. Agents are free to:
   - Rank and re-rank items (P0/P1/P2/P3)
   - Rearrange ordering within a priority tier
   - Refactor item descriptions for clarity
   - Add sub-items or implementation notes beneath existing items
   - Add entirely new items with a proposed rank
   - Mark items as `[-]` (in-progress/partially implemented) with a note on status

2. **File Versioning:** Every file modified or created MUST include `@version` and a datetime stamp in its header (e.g., `// Version: 2.0.1 - 2026-04-14 13:33:00 MDT`).

3. **Git Operations:** Make frequent, atomic commits. Keep `docs/CHANGELOG.md` updated. If CHANGELOG grows too large, archive older entries into `docs/history/`.

4. **Agent Workspace:** Agents are encouraged to create supplemental `AGENTS.md` files in any subdirectory to track architectural decisions, diagrams, or persistent memory across work loops.

5. **Branch Focus:** All work on this branch targets the **QML GUI refactor** and **C++23 Modernization**. The previous Qt Widgets codebase is the source of truth for feature parity found under the main branch (as well as in `../chadvis-projectm-qt_main-branch` locally) but this project has evolved and this branh will become the new main master branch when all features are reimplemnted alongside the new ones and changed as well given the power of modern models.

6. **Build System:** build.sh script that calls scripts/build.zsh has been optomized for quick unoptomized builds due to the users system performance.

7. **Destructive Operations:** NEVER use `rm` on critical references. Move outdated files to `.backup_graveyard/` with timestamps instead. See `AGENTS.md` for the full protocol or create it if missing which contains only these core drectives.

---

## 📊 Ranking System

| Rank | Meaning |
|------|---------|
| **P0** | Critical / Architectural Blocker — must be done first. Stability, thread-safety, or framework compliance issues. |
| **P1** | High Impact / Performance — major optimizations or essential feature parity with the Widgets app. |
| **P2** | Enhancements / Features — QoL improvements, new integrations, valuable additions. |
| **P3** | Tech Debt / Cleanup — refactoring, modernization, non-urgent housekeeping. |

---

## [P0] Critical Fixes & Architecture Blockers

- [ ] **Enforce C++23 Standard:** Update `CMakeLists.txt` to require `CXX_STANDARD 23`. Use modern standard library features across the board.
- [ ] **Gut and Rewrite Video Recording Pipeline:** The current recording implementation is non-functional. Completely rip it out. Build a new, versatile FFmpeg pipeline that handles automation, conflict resolution (file locks, name collisions), and allows starting/stopping at any point. **[2026-04-15]** HW accel (NVENC/VAAPI) implemented. AudioQueue wired. Recording path now functional.
- [-] **Implement QQuickFramebufferObject:** Wrap the `VisualizerRenderer` logic inside a `QQuickFramebufferObject` to ensure QML/RHI compatibility and prevent OpenGL context-collision segfaults. Ensure zero-copy rendering into Qt Quick scenegraph. **[2026-04-15]** `VisualizerQFBO` implemented, render() calls projectM correctly. Needs user testing.
- [-] **Lock-Free Queues for Audio/ProjectM (Partially Done):** `moodycamel::ReaderWriterQueue` has been implemented in `AudioQueue.hpp`. Ensure `audioMutex_` is removed from audio processing callbacks (`pushAudioSamples`). Avoid priority inversion on real-time audio threads. **[2026-04-15]** AudioQueue wired to VideoRecorderThread for recording.
- [ ] **Window Resizing / Render Dimensions:** Fix the bug where resizing the Qt window fails to properly update the projectM OpenGL render dimensions or framebuffer size.
- [-] **Modernize QML Registration:** Replace manual `qmlRegisterSingletonType` with Qt 6 declarative macros (`QML_ELEMENT`, `QML_SINGLETON`). Use `qt_add_qml_module` in CMake to enable `qmltc` (QML Type Compiler) for AOT UI compilation. **[2026-04-15]** Added `QML_ELEMENT` to VisualizerItem, VisualizerQFBO. Removed redundant manual registration.
- [-] **Standardize Signal/Slot Mechanics:** The codebase mixes `Q_OBJECT` with custom `vc::Signal`. Qt's native `signals:` handle thread-safe cross-thread queuing automatically. Remove custom `Signal` for cross-thread emissions. **[2026-04-15]** Verified vc::Signal handlers use QMetaObject::invokeMethod for thread safety.
- [ ] **Reimplement Audio Playlist:** Restore the pre-QML audio playlist functionality with robust, gapless-capable handling of both local audio files and remote HTTP Suno streams.
- [-] **Force Quit Causes SIGSEGV Fault:** Fix the segmentation fault error when force quitting (ctrl+c) via terminal or window manager.

---

## [P1] High Impact / Performance & Feature Parity

- [x] **Asynchronous Frame Readback & FBO Lifecycle:** Use **Pixel Buffer Objects (PBOs)** with double-buffering for asynchronous frame recording to avoid `glReadPixels` pipeline stalls. Ensure `VisualizerQFBO` manages QSG textures optimally. **[2026-04-15]** ALREADY IMPLEMENTED in `VisualizerRenderer::captureAsync()`. The `AsyncFrameGrabber` class in FrameGrabber.cpp is dead code (duplicate).
- [ ] **Decouple FFT Analysis from Audio Thread:** Background FFT. Process `AudioAnalyzer::analyze()` using a lock-free queue on a dedicated background worker to prevent audio dropouts (underruns).
- [ ] **Migrate Overlays & CPU Rasterization to QML:** Delete `OverlayRenderer.cpp` and `OverlayEngine.cpp`. Moving overlay rendering from CPU `QPainter` to QML `Text` and hardware-accelerated shaders (Scene Graph).
- [ ] **Remove Redundant Audio Sources:** Remove `FFmpegAudioSource.cpp` and `SDLMixerAudioSource.cpp`. Rely on `QMediaPlayer` + `QAudioBufferOutput` or **miniaudio**.
- [ ] **Consolidate Metadata Parsing:** Investigate dropping `TagLib` and relying on Qt's native `QMediaMetaData` to reduce dependencies.
- [ ] **Pseudo-Mobile Desktop Layout:** Update QML to use `ApplicationWindow`, `Drawer`, `SplitView`, and `ToolBar` for a responsive design where the ProjectM canvas takes maximum real estate.
- [ ] **Accordion & Layout Nesting Optimization:** Flatten deep `ColumnLayout`/`RowLayout` hierarchies. Use `ListView` with delegates for performance.
- [ ] **Large SVG Optimization:** Ensure all QML `Image` tags loading SVGs explicitly define `sourceSize.width` and `sourceSize.height`.
- [ ] **Throttled QML Bridge Updates:** Limit UI updates (AudioSpectrum/LyricsSync) crossing the C++/QML bridge to prevent Qt event loop flooding (e.g., using `QTimer` with dirty flags).
- [ ] **Build QML Settings Window:** Create comprehensive `Settings.qml`.
- [ ] **Build Suno Library Table:** Implement `SunoLibraryModel : public QAbstractTableModel` for efficient lazy-loading of massive library lists.
- [ ] **Automated Blacklisting & Favorites:** Auto-blacklist crashing shaders. Add UI for favorites and manual blacklisting.
- [ ] **Fast JSON Parsing:** Integrate `nlohmann/json` or `glaze` via CPM to replace heap-heavy `QJsonDocument`.
- [ ] **Centralize Circular Buffers:** Unify custom ring buffers into `src/util/RingBuffer.hpp` (64-byte aligned).
- [ ] **Implement Drag & Drop:** Global QML `DropArea` routing incoming files to the C++ playlist manager.
- [ ] **Local Audio Alignment (whisper.cpp):** Import `ggerganov/whisper.cpp` via CPM for precise `.srt`/`.lrc` word-level karaoke timestamps.

---

## [P2] Enhancements & Feature Additions

- [ ] **Parse Suno API Endpoints:** Map endpoints from `.agent/SUNO_API_ENDPOINTS.md` into the API orchestration layer. Implement standard Suno features (gen, extend, fetch).
- [ ] **B-Side & Experimental Endpoints:** Implement undocumented `/b-side` and `/experimental` API endpoints.
- [ ] **Session Keep-Alive Worker:** Background worker for Clerk.dev JWT / session cookies.
- [ ] **Robust HTTP Client:** Consider replacing `QNetworkAccessManager` with `libcpr/cpr` (CPM) for simpler async code flows.
- [ ] **High-Performance FFT:** Evaluate replacing KissFFT with `FFTW3` or `muFFT` for SIMD-accelerated spectrum analysis.
- [ ] **Karaoke Renderer Upgrades:** Add Ruby Text (Furigana) and QML shader-based glowing text.
- [ ] **Hardware Audio Resampling:** Ensure libswresample uses AVX/NEON or use libsoxr.
- [ ] **Configuration Versioning:** Add `config_version = N` to TOML definitions.

---

## [P3] Tech Debt & Cleanup (C++23 Modernization)

- [ ] **Replace Custom `Result<T>` with `std::expected`:** Fully migrate to C++23 `std::expected`.
- [ ] **Standardized Formatting:** Replace `fmt::format` / `fmt::print` with C++23 `std::print` and `std::println`.
- [ ] **Modernize Ranges & Algorithms:** Utilize C++20/23 `std::ranges` and `std::views` (e.g., `std::views::filter`).
- [ ] **Leverage `std::mdspan`:** Use `std::mdspan` for zero-overhead 2D mesh grid access in visualizer config mathematical matrices.
- [ ] **Modern Threading:** Swap raw `std::thread` for C++20 `std::jthread` with `std::stop_token`.
- [ ] **Deducing `this`:** Implement C++23 explicit object parameters for recursive callbacks or CRTP structures where applicable.
- [ ] **Purge QtWebEngine:** Delete legacy web engine components (stealth browser, cookie dialogs). Rely entirely on native browser flow.
- [ ] **Delete Legacy Overlay Code:** Purge `src/overlay/` once QML refactor is complete.
- [ ] **Graveyard Cleanup & Directory Restructure:** Re-org `src/ui/controllers` to `src/qml_bridge/controllers`. Move auth classes to `src/suno/auth/`.
- [ ] **Add File Versioning Headers:** Pass through the codebase and add `// Version: X.Y.Z - YYYY-MM-DD` headers.
- [ ] **Update AGENTS.md:** Reflect new directory structure and refined tasks when significant milestones are completed.

---

## Reference Documents

| Document | Purpose |
|----------|---------|
| `AGENTS.md` (project root) | Agent operational guide, build commands, coding standards |
| `.agent/SUNO_API_ENDPOINTS.md` | Canonical Suno API endpoint reference |
| `.agent/SUNO_API_REFERENCE.md` | Suno integration patterns for C++/Qt6 |
| `.agent/QML_REFACTOR_GUIDE.md` | QML architecture decisions and patterns |
| `docs/ARCHITECTURE.md` | System architecture (pre-QML, maps features) |
| `docs/VIDEO_RECORDING.md` | Recording subsystem documentation |
| `docs/SUNO_INTEGRATION.md` | Suno auth flow and data persistence |
| `docs/CHANGELOG.md` | Version history |

---

*"Maximum effort. No compromises. Ship it like it's Arch Linux."*
