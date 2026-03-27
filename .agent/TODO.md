# MASTER TODO.md — QML Modern GUI Refactor

> **Branch:** `refactor/qml-modern-gui`
> **Last Updated:** 2026-03-27
> **Version:** 1.0.0

---

## AGENT DIRECTIVES — READ BEFORE WORKING

> These rules are **permanent**. Agents must follow them at all times. Violations will be reverted.

1. **DO NOT REMOVE ANY ITEMS.** Only the **user** may mark items as complete (`[x]`) or remove them. Agents are free to:
   - Rank and re-rank items (P0/P1/P2/P3)
   - Rearrange ordering within a priority tier
   - Refactor item descriptions for clarity
   - Add sub-items or implementation notes beneath existing items
   - Add entirely new items with a proposed rank
   - Mark items as `[~]` (in-progress/partially implemented) with a note on status

2. **File Versioning:** Every file modified or created MUST include `@version` and a datetime stamp in its header (e.g., `@version 2.0.1 - 2026-03-27 01:33:00 MDT`).

3. **Git Operations:** Make frequent, atomic commits. Keep `docs/CHANGELOG.md` updated. If CHANGELOG grows too large, archive older entries into `docs/history/`.

4. **Agent Workspace:** Agents are encouraged to create supplemental `AGENTS.md` files in any subdirectory to track architectural decisions, diagrams, or persistent memory across work loops.

5. **Branch Focus:** All work on this branch targets the **QML GUI refactor**. The previous Qt Widgets codebase is the source of truth for feature parity. Reference `src/ui/` (Widgets) to understand what must be reimplemented in `src/qml/` and `src/qml_bridge/`.

6. **Build System:** Build is slow on the user's system. Prefer to batch changes and let the user compile. Only run `cmake`/`build.sh` for small targeted fixes or when explicitly finishing a task.

7. **Destructive Operations:** NEVER use `rm`. Move files to `.backup_graveyard/` with timestamps instead. See `AGENTS.md` for the full protocol.

8. **Suno API Endpoints:** The user will provide locally sniffed Suno API endpoints in an agent coding desktop program. When these are available, parse them into the implementation plan. Reference `.agent/SUNO_API_ENDPOINTS.md` for the canonical endpoint list and `.agent/SUNO_API_REFERENCE.md` for integration patterns.

---

## Ranking System

| Rank | Meaning |
|------|---------|
| **P0** | Critical / Architectural Blocker — must be done first. Stability, thread-safety, or framework compliance issues. |
| **P1** | High Impact / Performance — major optimizations or essential feature parity with the Widgets app. |
| **P2** | Enhancements / Features — QoL improvements, new integrations, valuable additions. |
| **P3** | Tech Debt / Cleanup — refactoring, modernization, non-urgent housekeeping. |

---

## [P0] Critical Fixes & Architecture Blockers

- [ ] **Gut and Rewrite Video Recording Pipeline:** The current recording implementation is non-functional. Completely rip it out. Build a new, versatile FFmpeg pipeline that handles automation, conflict resolution (file locks, name collisions), and allows starting/stopping at any point in an audio track.
- [ ] **Background Video Finalization:** Ensure the new video recorder safely finalizes and muxes incomplete videos in the background if aborted, or auto-deletes them cleanly without leaving corrupted `.mp4` files on disk.
- [ ] **Window Resizing / Render Dimensions:** Fix the bug where resizing the Qt window fails to properly update the projectM OpenGL render dimensions or framebuffer size.
- [ ] **Implement QQuickFramebufferObject:** Wrap the `VisualizerRenderer` logic inside a `QQuickFramebufferObject` to ensure QML/RHI compatibility and prevent OpenGL context-collision segfaults. Qt 6 QML defaults to RHI (Vulkan/Metal/D3D11), but projectM requires raw OpenGL 3.3. QFBO provides a safe renderer thread with guaranteed GL context.
- [ ] **Replace Mutexes in Audio Callbacks with Lock-Free Queues:** `audioMutex_` is locked inside audio processing callbacks (`pushAudioSamples`). Audio threads are real-time; mutex acquisition causes priority inversion and dropouts. Introduce `moodycamel::ReaderWriterQueue` (via CPM) for SPSC lock-free audio chunk passing.
- [ ] **Modernize QML Registration:** Replace manual `qmlRegisterSingletonType` with Qt 6 declarative macros (`QML_ELEMENT`, `QML_SINGLETON`). Use `qt_add_qml_module` in CMake. This enables `qmltc` (QML Type Compiler) for ahead-of-time compilation and massive UI performance gains.
- [ ] **Standardize Signal/Slot Mechanics:** The codebase mixes `Q_OBJECT` with custom `vc::Signal`. Qt's native `signals:` handle thread-safe cross-thread queuing automatically. The custom `Signal` lacks queued connections, which is dangerous for cross-thread emissions (e.g., `VideoRecorderThread` → GUI).
- [ ] **Reimplement Audio Playlist:** Restore the pre-QML audio playlist functionality with robust, gapless-capable handling of both local audio files and remote HTTP Suno streams.

---

## [P1] High Impact / Performance & Feature Parity

- [ ] **Migrate Overlays to QML:** Delete `OverlayRenderer.cpp` and `OverlayEngine.cpp`. Reimplement watermarks, karaoke text, and animations using QtQuick `Text`, `MultiEffect`, and `NumberAnimation` layered on top of the QFBO visualizer. QML's hardware-accelerated scenegraph replaces legacy C++ GL overlay code.
- [ ] **Build QML Settings Window:** Create comprehensive `Settings.qml` with sidebar layout (mimicking `SidebarWidget.cpp`). Expose `ConfigData.hpp` sections to QML via a unified `ConfigBridge` singleton with `Q_PROPERTY` wrappers or `QVariantMap` per section.
- [ ] **Build Suno Library Table:** Implement `SunoLibraryModel : public QAbstractTableModel` in C++. Expose to QML and consume via Qt 6 `TableView` for lazy-loading, zero-cost sorting, and smooth scrolling with 10,000+ clips.
- [ ] **Implement Drag & Drop:** Add a global QML `DropArea` to catch incoming audio files and route them to the C++ playlist manager via `AudioBridge`.
- [ ] **Explore ProjectM-4 Headers:** Investigate `/usr/include/projectM-4/` (`playlist.h`, `projectM.h`) to implement native playlist transitions and deeper shader control.
- [ ] **Automated Blacklisting:** Detect broken projectM shaders (GL compilation failures, rendering crashes) and automatically blacklist them.
- [ ] **Manual Blacklisting + Favorites:** Add UI/API to manually blacklist shaders and implement a favorites/star-rating system. Add toggle to only select from favorites or above a rating threshold.
- [ ] **Zero-Copy Hardware Video Encoding:** Intercept the QFBO texture directly. Use OpenGL-to-CUDA/VAAPI interop to pass the texture handle to FFmpeg's `hw_frames_ctx`. Eliminates the current PBO round-trip (GPU→CPU→GPU).
- [ ] **Centralize Circular Buffers:** Unify the multiple custom `CircularBuffer` implementations (`AudioAnalyzer.hpp`, `VisualizerRenderer.hpp`) into a single template `src/util/RingBuffer.hpp` with `alignas(64)` memory alignment.
- [ ] **Fast JSON Parsing:** Replace `QJsonDocument` (heap-heavy) with `nlohmann/json` or `simdjson` (via CPM) for Suno API responses. Use `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` for automatic struct mapping.
- [ ] **Local Audio Alignment (whisper.cpp):** Import `ggerganov/whisper.cpp` via CPM. Implement forced-alignment engine to generate precise `.srt`/`.lrc` word-level timestamps when Suno API doesn't provide aligned lyrics.
- [ ] **Evaluate Audio Backend / Remove Dead Code:** Remove `SDLMixerAudioSource.cpp` and `FFmpegAudioSource.cpp` (dead code). Evaluate replacing `QMediaPlayer` with **miniaudio** for native gapless playback and direct PCM float extraction without thread-locking.
- [ ] **Mass Recording Automation:** Create a mass-recording mode that handles recording multiple playlist items (Suno remote, local audio, or mixed) consecutively with clean transitions and conflict handling.
- [ ] **Audio Library Database + ID3 Tags:** Implement reading/writing ID3 tags for local audio. Remote Suno library tracks stored in local database with cleanup function for orphaned entries.

---

## [P2] Enhancements & Feature Additions

- [ ] **Parse Suno API Endpoints:** Read `.agent/SUNO_API_ENDPOINTS.md` and the user's sniffed endpoint data to implement the full Suno API surface.
- [ ] **Official Suno Features:** Implement robust HTTP wrappers for standard Suno generation, lyrics generation, and track extending.
- [ ] **B-Side & Experimental Endpoints:** Map and implement the undocumented `/b-side` and `/experimental` API endpoints.
- [ ] **Session Keep-Alive Worker:** Build a background worker to maintain Clerk.dev JWT and session cookies, preventing auth timeouts during mass generation.
- [ ] **Robust HTTP Client (cpr):** Consider replacing `QNetworkAccessManager` with `libcpr/cpr` (via CPM) for cleaner async/await API flows with built-in session/cookie/token handling.
- [ ] **High-Performance FFT:** Evaluate replacing KissFFT with `FFTW3` or `muFFT` for SIMD-accelerated spectrum analysis at 60fps.
- [ ] **Karaoke Renderer Upgrades:** Add Ruby Text (Furigana-style phonetic guides) for international lyrics. Implement vertex-shader-based text glow instead of CPU-side `QPainter` shadow drawing.
- [ ] **Hardware Audio Resampling:** Ensure `libswresample` uses AVX/NEON optimizations, or switch to `libsoxr` for higher quality during video generation.
- [ ] **Configuration Versioning:** Add `config_version = N` to TOML so future struct changes don't break existing user configs.

---

## [P3] Tech Debt & Modernization

- [ ] **Migrate to C++23:** Update `CMakeLists.txt` to `CMAKE_CXX_STANDARD 23`. Leverage `std::expected` (replace custom `Result<T>`), `std::mdspan` (audio buffers), `std::print` (formatting), deducing `this` (CRTP simplification), and ranges/views (JSON/frame parsing).
- [ ] **Replace Custom Result with std::expected:** Swap `vc::Result` for `std::expected` with monadic operations (`and_then`, `transform`, `or_else`).
- [ ] **Add File Versioning:** Pass through codebase adding `@version` and datetime stamps to all header and source files.
- [ ] **Purge QtWebEngine:** Delete `SunoBrowserWidget.cpp`, `SunoCookieDialog.cpp`, `stealth.js`, and `SunoPersistentAuth.cpp`. Promote `SystemBrowserAuth.cpp` as the sole auth mechanism. Remove `Qt6::WebEngineWidgets` from `CMakeLists.txt` — saves ~150MB binary size.
- [ ] **Graveyard Cleanup & Directory Restructure:** Clean `.backup_graveyard`. Rename `src/ui/controllers` to `src/qml_bridge/controllers`. Move auth classes to `src/suno/auth/`.
- [ ] **Delete Legacy Overlay Code:** Once QML overlay migration is complete, remove `src/overlay/` entirely (after backing up to `.backup_graveyard/`).
- [ ] **Create Suno Auth Subdirectory:** Consolidate `SystemBrowserAuth` and any remaining auth logic under `src/suno/auth/`.
- [ ] **Update AGENTS.md:** Reflect QML architecture, new directory structure, and updated build/test procedures.

---

## Implementation Phases (Suggested Order)

### Phase 1: Foundation (P0)
QFBO wrapping, signal standardization, lock-free audio queues, QML registration modernization, playlist restoration.

### Phase 2: Rendering & Overlays (P0-P1)
Migrate overlays to QML, build settings window, Suno library model, drag & drop, projectM header exploration.

### Phase 3: Recording Rewrite (P0-P1)
Gut old pipeline, build new FFmpeg pipeline with background finalization, zero-copy encoding, mass recording mode.

### Phase 4: Suno API & Features (P1-P2)
Endpoint parsing, HTTP wrappers, whisper.cpp alignment, session keep-alive, B-Side/experimental endpoints.

### Phase 5: Modernization (P2-P3)
C++23 migration, purge QtWebEngine, audio backend consolidation, directory restructure, tech debt cleanup.

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
