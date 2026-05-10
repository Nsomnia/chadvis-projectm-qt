# CODEBASE IMPROVEMENTS AUDIT

**Model:** openrouter/owl-alpha
**Date:** 2026-05-08
**Scope:** Full read-only audit of all ~20,000 LOC across 10 modules + QML + build + tests
**Format:** Structured by severity — P0=critical bug, P1=high, P2=medium, P3=low, P4=cosmetic, P5=opportunistic

---

## P0 — CRITICAL BUGS (will cause incorrect behavior or crashes)

### [P0-1] Brace mismatch — audio never encoded during recording
- **File:** `src/recorder/VideoRecorderThread.cpp:85-94`
- **Issue:** The inner `else` block at line 90-92 is missing its closing brace. Line 93's `}` closes the outer `if(hasVideo)` instead of the inner `else`, and line 94 closes the `while` loop prematurely. Audio encoding (lines 96-111), dropped-frames update (114-117), stop condition (120-122), stats update (124-129), and error emission (132-134) all execute **outside** the while loop.
- **Impact:** Audio is never encoded during recording. Dropped frames never updated. Stop condition checked only once after loop exits.
- **Fix:** Add closing brace after line 92's `LOG_WARN`. The inner `else` needs its own `}`, then the outer `if` needs `}`, then the `while` continues.

### [P0-2] Potential nullptr dereference in writePacket()
- **File:** `src/recorder/VideoRecorderFFmpeg.cpp:461-463`
- **Issue:** `writePacket()` uses ternary `stream == videoStream_ ? videoCodecCtx_->time_base : audioCodecCtx_->time_base` but if `audioStream_` is null and the packet is audio, `audioCodecCtx_` is nullptr → nullptr dereference. Triggered when audio encoding is active but `audioStream_` failed to initialize (silently at line 363-364).
- **Fix:** Add null check before dereferencing `audioCodecCtx_`. Return false if null.

### [P0-3] Out-of-bounds access in LyricsOverlayRenderer
- **File:** `src/lyrics/LyricsRenderer.cpp` (overlay render method)
- **Issue:** `LyricsOverlayRenderer::render()` accesses `lyrics_.lines[position_.lineIndex]` without bounds checking. If `lineIndex` is out of range → undefined behavior.
- **Fix:** Add bounds check before accessing `lyrics_.lines[]`. Return early or clamp.

### [P0-4] submitAudioSamples() is a complete no-op stub
- **File:** `src/recorder/VideoRecorderCore.cpp:~108`
- **Issue:** `submitAudioSamples()` has an empty body. Callers (BridgeRegistration, RecordingBridge) have no way to know samples are silently discarded. Misleading API surface.
- **Fix:** Either implement (forward to AudioQueue) or remove the method signature entirely and document that audio flows through AudioQueue.

---

## P1 — HIGH PRIORITY (structural issues, risk of bugs, significant improvements)

### [P1-1] Duplicate forward declaration
- **File:** `src/visualizer/VisualizerRenderer.hpp:35-44`
- **Issue:** `AudioQueue` is forward-declared twice in the same `vc` namespace (lines 35-37 and 41-43). Both blocks are identical.
- **Fix:** Remove one of the two duplicate forward declaration blocks (lines 41-44).

### [P1-2] Thread-unsafe PFFFT global initialization
- **File:** `src/audio/AudioAnalyzer.cpp:10`
- **Issue:** Global `static PFFFT_Setup* g_pffft_setup` is lazily initialized without thread safety. Race condition if two `AudioAnalyzer` instances are created concurrently.
- **Fix:** Use `std::call_once` or `std::atomic` with double-checked locking, or change to a Meyer's singleton.

### [P1-3] Result<T>::value() has no bounds checking
- **File:** `src/util/Result.hpp:79-95`
- **Issue:** Calling `value()` on an Error result is UB (`std::get<T>` on variant holding Error). Comment says "check first or face UB, junior" — footgun-prone.
- **Fix:** Add `std::abort()` or throw in debug builds when accessing wrong variant. Or migrate to `std::expected`.

### [P1-4] Config parser defaults don't match struct defaults
- **Files:** `src/core/ConfigParsers.cpp` vs `src/core/ConfigData.hpp`
- **Issue:** Karaoke `yPosition`: parser default 0.85 vs struct default 0.5. Recording video: parser defaults width=1280, height=720, fps=30, preset="ultrafast", crf=23 vs struct defaults 1920x1080, fps=60, medium, crf=18.
- **Fix:** Align all parser defaults with `ConfigData.hpp` struct defaults. Single source of truth should be `ConfigData.hpp`. Othre model markdown file has some better and easier to maintain given large number of user configurable options using some form of map or table. 

### [P1-5] PULSEAUDIO_FOUND checked but never searched
- **File:** `CMakeLists.txt:439-442`
- **Issue:** `PULSEAUDIO_FOUND` is checked but `find_package(PulseAudio)` is never called. Dead code block.
- **Fix:** Either add `find_package(PulseAudio)` or remove the dead conditional block. Pulse is lower priority as most reasonable end-users and operating systems have moved onto pipewire, with windows being anothrer target platform further into the development timeline and potentially mobile much further into development.

### [P1-6] Qt6::WebEngineWidgets may be unused
- **File:** `CMakeLists.txt:52`
- **Issue:** `Qt6::WebEngineWidgets` is in `find_package` and `COMMON_LIBS` but TODO on line 51 asks "is web engine needed anymore?" Heavyweight unused dependency.
- **Fix:** Determine if `SunoPersistentAuth` (uses `QWebEngineProfile`) still needs it. If not, remove.
- **User Notes:** Confirming that the WebEngine implementation is no longer needed currently as it was found to not handle auth capture at all and further investigation of the endpoints and burp suite show how authentication flow can be handled, later even with a callback url which should be able to be redirected to lcoalhost in additon to the standard JWT auth flow.

### [P1-7] Icons registered twice (duplicate resource registration)
- **File:** `CMakeLists.txt:399-431` vs `resources/chadvis-projectm-qt.qrc`
- **Issue:** Icons are registered in both the `.qrc` file (compiled into `project_lib` at line 373) AND in `qt_add_qml_module RESOURCES`. Causes duplicate resource registration and potential conflicts.
- **Fix:** Remove individual icon `RESOURCES` from `qt_add_qml_module` and rely on the `.qrc`, OR remove the `.qrc` and use only `qt_add_qml_module RESOURCES`.

### [P1-8] test_PresetScanner.cpp not in CMakeLists
- **File:** `tests/unit/CMakeLists.txt`
- **Issue:** `test_PresetScanner.cpp` (68 LOC, fully implemented) is NOT listed in `add_executable` sources. Won't compile or run.
- **Fix:** Add `test_PresetScanner.cpp` to sources. Add `runTestPresetScanner` entry point in `test_main.cpp`.

### [P1-9] test_projectm_render.cpp not in CMakeLists
- **File:** `tests/integration/CMakeLists.txt`
- **Issue:** `test_projectm_render.cpp` is NOT listed. Integration `test_main.cpp` is a no-op (returns 0).
- **Fix:** Add to sources or remove the file. Add actual integration tests to `test_main.cpp`.

### [P1-10] build.sh delegates to non-existent build.zsh
- **File:** `build.sh:20`
- **Issue:** Delegates to `scripts/build.zsh` which does NOT exist. Only `scripts/build-fast.sh` exists. Build entry point chain is broken.
- **Fix:** Rename `build-fast.sh` to `build.sh`, or create `scripts/build.zsh`, or fix `build.sh` to point to the correct script.

### [P1-11] SunoOrchestrator in wrong namespace
- **File:** `src/suno/SunoOrchestrator.hpp:14`
- **Issue:** `SunoOrchestrator` is in namespace `vc` instead of `vc::suno` like all other Suno files.
- **Fix:** Move into `namespace vc::suno`.

### [P1-12] SunoOrchestrator bypasses request queue
- **File:** `src/suno/SunoOrchestrator.cpp:31,43`
- **Issue:** Bypasses `SunoClient`'s request queue (with 1s throttling) by calling `client_->networkManager()->post/get()` directly. Could cause API rate limiting.
- **Fix:** Refactor to use `SunoClient::enqueueRequest()` for all HTTP operations.

### [P1-13] Orphaned LyricAligner.hpp dead code
- **File:** `src/suno/LyricAligner.hpp`
- **Issue:** Defines `LyricAligner` (abstract base) and `PlaceholderAligner` (no-op) which are never referenced. The real `LyricsAligner` is in `src/suno/SunoLyrics.hpp`.
- **Fix:** Delete this file.

### [P1-14] Controllers in wrong namespace
- **Files:** `src/ui/controllers/AudioController.hpp`, `RecordingController.hpp`, `VisualizerController.hpp`
- **Issue:** All three are in `namespace vc` instead of `vc::ui` like `SunoPersistentAuth` and `SystemBrowserAuth`.
- **Fix:** Move into `vc::ui` namespace.

### [P1-15] Missing `<cmath>` include in LyricsRenderer.cpp
- **File:** `src/lyrics/LyricsRenderer.cpp`
- **Issue:** `KaraokeRenderer::renderInstrumental()` uses `std::sin` from `<cmath>` but `<cmath>` is not explicitly included (relies on transitive include through `<chrono>`). Fragile.
- **Fix:** Add explicit `#include <cmath>`.

### [P1-16] Dual position update path in LyricsSync
- **File:** `src/lyrics/LyricsSync.cpp:18-28`
- **Issue:** `AudioEngine::positionChanged` is connected directly to `updatePosition` via lambda (line 25-28) AND a 16ms `QTimer` also calls `updatePosition` (line 18-22). Redundant — `updatePosition` fires ~60fps from timer PLUS every signal emission. Also, `onAudioPositionChanged` slot (header line 181) exists but is empty — dead code.
- **Fix:** Remove the 16ms timer and rely on the `positionChanged` signal, OR remove the signal connection and keep the timer. Signal approach preferred. Remove empty slot.

### [P1-17] Dangling pointers from getContextLines/getUpcomingLines
- **File:** `src/lyrics/LyricsSync.cpp`
- **Issue:** Both methods return `std::vector<const LyricsLine*>` — raw pointers into `lyrics_.lines` vector. If `loadLyrics()` is called, all returned pointers dangle.
- **Fix:** Return by value or document lifetime constraint clearly.

### [P1-18] Unused includes (multiple files)
- `src/audio/AudioEngine.hpp:12` — `<QTimer>` included but never used
- `src/audio/AudioAnalyzer.cpp:6` — `<complex>` included but never used
- `src/audio/analysis/MediaMetadata.cpp:9` — `<QBuffer>` included but never used
- `src/lyrics/LyricsRenderer.cpp:8` — `<QPainterPath>` included but never used
- `src/lyrics/LyricsSync.hpp:17-18` — `<functional>` and `<deque>` included but never used
- **Fix:** Remove all unused includes.

### [P1-19] PlaylistItem::valid field never read
- **File:** `src/audio/Playlist.hpp:20`
- **Issue:** `valid` field is set but never read anywhere.
- **Fix:** Remove the field or add usage (e.g., skip invalid items in navigation).

### [P1-20] PresetBridge::cachedPresets_ never used
- **File:** `src/qml_bridge/PresetBridge.hpp`
- **Issue:** `cachedPresets_` member declared but never used. All preset accessors rebuild QVariantLists on every QML call with no caching.
- **Fix:** Implement caching with invalidation, or remove the unused member.

### [P1-21] OverlayElementConfig animation fields likely unused
- **File:** `src/core/ConfigData.hpp`
- **Issue:** `OverlayElementConfig` has `animation`/`animationSpeed` fields that may not be consumed by any renderer.
- **Fix:** Verify usage and remove if unused, or implement animation support.

### [P1-22] debug=true in default config
- **File:** `config/default.toml:7`
- **Issue:** `debug = true` enables verbose logging by default, impacting performance.
- **Fix:** Set `debug = false`. Users can enable via `--debug` flag.

### [P1-23] SettingsPanel.qml is monolithic (526 LOC)
- **File:** `src/qml/panels/SettingsPanel.qml:526`
- **Issue:** Largest QML file. Contains all settings sections in one file. Two nearly identical custom Switch implementations copy-pasted (lines 225-265). Color picker is `console.log("TODO")`. Fullscreen toggle is `console.log("TODO")`. Profile buttons are `enabled: false`.
- **Fix:** Decompose into sub-components per section. Extract Switch to reusable component. Implement color picker. Remove unimplemented TODOs.

---

## P2 — MEDIUM PRIORITY (correctness edge cases, performance, maintainability)

### [P2-1] Uses rand() instead of \<random\>
- **File:** `src/suno/SunoLyricsManager.cpp:52`
- **Issue:** Uses `rand()` (C-style) for jitter instead of `<random>`. Not thread-safe, poor randomness.
- **Fix:** Replace with `std::random_device` + `std::mt19937` + `std::uniform_int_distribution`.

### [P2-2] Uses snprintf instead of C++23 std::format
- **File:** `src/lyrics/LyricsData.cpp` (toSrt/toLrc export functions)
- **Issue:** Uses `snprintf` for time formatting instead of `std::format`.
- **Fix:** Replace with `std::format("{:02d}:{:02d}:{:02d}.{:03d}", ...)`.

### [P2-3] PresetBridge rebuilds QVariantLists on every access
- **File:** `src/qml_bridge/PresetBridge.cpp`
- **Issue:** `presets()`, `activePresets()`, `favoritePresets()` all rebuild complete QVariantLists on every QML property access. Expensive for large preset libraries.
- **Fix:** Convert to `QAbstractListModel` with proper `dataChanged` signals.

### [P2-4] SunoBridge manual QVariantMap construction
- **File:** `src/qml_bridge/SunoBridge.cpp`
- **Issue:** `onLibraryUpdated()` manually constructs QVariantMap for each clip. O(n) per page update.
- **Fix:** Use a shared conversion function or `QAbstractListModel` for clips.

### [P2-5] LyricsData::search() allocates on every call
- **File:** `src/lyrics/LyricsData.cpp`
- **Issue:** `std::transform` to lowercase on every search with O(n) allocation. No caching.
- **Fix:** Pre-compute lowercase versions at parse time, or use case-insensitive comparison.

### [P2-6] flipImageGPU() allocates textures/FBOs per frame
- **File:** `src/recorder/FrameGrabber.cpp`
- **Issue:** Creates 2 textures + FBO per frame for GPU flip, then does `glReadPixels` anyway. Full GPU roundtrip for what's essentially a memcpy. Heavy overhead at 60fps.
- **Fix:** Replace with CPU flip (the `flipImage()` function already exists).

### [P2-7] AudioAnalyzer::pcmData() returns full copy
- **File:** `src/audio/AudioAnalyzer.hpp`
- **Issue:** Returns a full copy of the PCM data vector every call. Expensive on hot path.
- **Fix:** Return `std::span` or const reference instead of copy.

### [P2-8] threadLoop() spin-waits at ~100fps when idle
- **File:** `src/recorder/VideoRecorderThread.cpp:78`
- **Issue:** 10ms `getNextFrame` timeout when no frames available = ~100fps spin, wasting CPU.
- **Fix:** Add yield or longer timeout when idle, or use condition_variable notification.

### [P2-9] parseDuration() uses std::regex
- **File:** `src/util/FileUtils.cpp`
- **Issue:** Uses `std::regex` for duration parsing. Known to be slow on libstdc++.
- **Fix:** Replace with manual string parsing or `std::from_chars`.

### [P2-10] Two visualizer implementations coexist (dead code)
- **Files:** `src/qml_bridge/VisualizerItem.hpp` + `src/qml_bridge/VisualizerQFBO.hpp`
- **Issue:** `VisualizerItem` (QQuickItem) has empty stubs for `onPcmReceived`/`feedSilentAudio`. `VisualizerQFBO` (QQuickFramebufferObject) has full implementation and was created to "solve the RHI/OpenGL conflict" (per QFBO header).
- **Fix:** Remove `VisualizerItem` entirely if QFBO is the active implementation.

### [P2-11] VideoRecorder.hpp is a pointless facade
- **File:** `src/recorder/VideoRecorder.hpp:7`
- **Issue:** 7-line facade that just includes `VideoRecorderCore.hpp`. Adds no value.
- **Fix:** Remove and update includes to use `VideoRecorderCore.hpp` directly.

### [P2-12] GLIncludes.hpp is an empty stub
- **File:** `src/util/GLIncludes.hpp:13`
- **Issue:** Empty file with only an include guard and a comment. No functional includes.
- **Fix:** Delete the file and remove from sources.

### [P2-13] Hardcoded path in default.toml
- **File:** `config/default.toml:55`
- **Issue:** `output_directory = '/home/nsomnia/Videos/ChadVis'` — not portable.
- **Fix:** Change to `'~/Videos/ChadVis'` (tilde expansion already works in `ConfigParsers.cpp:32-40`).

### [P2-14] Stub test files are dead code
- **Files:** `test_SilentAudioSource.cpp`, `test_AudioAnalyzer.cpp`, `test_ProjectMWrapper.cpp`, `test_projectm_render.cpp`
- **Issue:** 5-LINE STUBS (just TODO comments) sitting in source tree. Provide no value.
- **Fix:** Implement or delete.

### [P2-15] test_PresetScanner.cpp missing .moc and runner
- **File:** `tests/unit/visualizer/test_PresetScanner.cpp`
- **Issue:** Missing `.moc` include at bottom. No `runTestPresetScanner` function in `test_main.cpp`.
- **Fix:** Add `#include "test_PresetScanner.moc"`. Add test runner in `test_main.cpp`.

### [P2-16] Integration tests are a no-op
- **File:** `tests/integration/test_main.cpp`
- **Issue:** Creates `QCoreApplication`, returns 0. No tests registered.
- **Fix:** Add actual integration tests (smoke tests at minimum).

### [P2-17] PKGBUILD missing dependencies and wrong description
- **File:** `scripts/PKGBUILD`
- **Issue:** Missing `qt6-declarative` and `qt6-openglwidgets` in depends. `pkgdesc` says "C++20" but project requires C++23. `projectm` package name may not match AUR.
- **Fix:** Add missing deps, fix description, verify AUR package names.

### [P2-18] SunoOrchestrator JSON parsing has no null checks
- **File:** `src/suno/SunoOrchestrator.cpp:59,74`
- **Issue:** `onMessageFinished` reads `obj["response"].toString()` with no null checks. `onHistoryFinished` assumes `doc.array()` — if response is not array, UB in `toVariantList()`.
- **Fix:** Add null/type checks on JSON responses. Log unexpected format. Emit `errorOccurred` on malformed responses.

### [P2-19] PlaylistBridge takes address of reference
- **File:** `src/qml_bridge/BridgeRegistration.cpp`
- **Issue:** `PlaylistBridge::setPlaylist(&audioEngine->playlist())` takes address of a reference. Fragile if return type changes.
- **Fix:** Store as reference or pointer with documented lifetime coupling.

### [P2-20] Settings panel reuses playback icon
- **File:** `src/qml/panels/SettingsPanel.qml:423`
- **Issue:** Settings panel icon reuses `playback.svg` instead of a dedicated settings icon.
- **Fix:** Add a settings SVG icon and use it.

### [P2-21] Theme.qml missing textPrimaryVariant property
- **File:** `src/qml/panels/LyricsPanel.qml:104`
- **Issue:** References `Theme.textPrimaryVariant` which is NOT defined in `Theme.qml`. Returns undefined at runtime.
- **Fix:** Add `textPrimaryVariant` to `Theme.qml` or change reference to existing property.

### [P2-22] Playlist "Show in Folder" unreliable on Linux
- **File:** `src/qml/panels/PlaylistPanel.qml:199,247,266`
- **Issue:** Uses `Qt.openUrlExternally("file://" + path)` — unreliable on non-GNOME Linux desktops.
- **Fix:** Use `QDesktopServices::openUrl` with `QUrl::fromLocalFile()` and fallback to `xdg-open`.

### [P2-23] Color picker is unimplemented
- **File:** `src/qml/panels/KaraokeSettings.qml:47`
- **Issue:** Color picker is `console.log("Color picker TODO")`.
- **Fix:** Implement proper color picker.

### [P2-24] Fullscreen shortcut is unimplemented
- **File:** `src/qml/main.qml:544`
- **Issue:** Fullscreen shortcut is `console.log("Fullscreen toggle (TODO)")`.
- **Fix:** Implement actual fullscreen toggle.

### [P2-25] PresetPanel rebuilds model on every change
- **File:** `src/qml/panels/PresetsPanel.qml:53`
- **Issue:** `presetList.model = PresetBridge.filteredPresets()` rebuilds entire model on every `onPresetsChanged`, killing scroll position and selection.
- **Fix:** Implement as `QAbstractListModel` with proper `dataChanged` signals.

### [P2-26] Record button highlight is inverted
- **File:** `src/qml/panels/RecordingPanel.qml:28`
- **Issue:** `highlighted: !RecordingBridge.isRecording` — button highlights when NOT recording. Counterintuitive.
- **Fix:** Change to `highlighted: RecordingBridge.isRecording`.

### [P2-27] .clang-tidy variable naming rules are wrong
- **File:** `.clang-tidy:29-35`
- **Issue:** `VariableCase`, `MemberCase`, and `ParameterCase` all set to `CamelCase` (PascalCase). Means ALL variables must be `MyVariable` — highly unconventional and will flag virtually every local variable.
- **Fix:** Change `VariableCase` to `camelBack` or `lower_case`. Change `MemberCase` to `camelBack` or `xxx_` style. Change `ParameterCase` to `camelBack`.

### [P2-28] .clang-tidy missing useful checks
- **File:** `.clang-tidy`
- **Issue:** No `modernize-*` checks, no `readability-function-cognitive-complexity`, no `readability-magic-numbers`, no `performance-*` checks.
- **Fix:** Add `modernize-use-default-member-init`, `modernize-use-std-format`, `readability-function-cognitive-complexity` (threshold=25), `performance-unnecessary-copy-initialization`.

### [P2-29] .clangd config is minimal
- **File:** `.clangd`
- **Issue:** Only removes one compiler flag. No `Index`, `Diagnostics`, or `CompileDatabase` sections.
- **Fix:** Add `Index.Background: Build`. Add `Diagnostics.UnusedIncludes: Strict`.

### [P2-30] OverlayBridge saves on every change with no debounce
- **File:** `src/qml_bridge/OverlayBridge.cpp`
- **Issue:** `saveOverlays()` called on every `setOverlays/addOverlay/removeOverlay/updateOverlay` — no debouncing. 5 QML changes = 5 file writes. Also `var.toMap()` silently produces empty map for non-map types.
- **Fix:** Add debouncing (QTimer 2s like SettingsBridge). Add type checking before `toMap()`.

### [P2-31] ThemeBridge only exposes 2 writable colors
- **File:** `src/qml_bridge/ThemeBridge.hpp/cpp`
- **Issue:** Only accent and background are writable at runtime. All other 50+ colors are CONSTANT. 13 QSS theme files exist but are not switchable at runtime.
- **Fix:** Implement runtime theme switching by loading QSS files. Expose more colors as writable. Add theme selector in Settings.

### [P2-32] No internationalization support
- **Scope:** Entire codebase
- **Issue:** All user-facing strings are hardcoded with no `tr()`, no `QT_TRANSLATE_NOOP`, no `.ts` files.
- **Fix:** Add `QTranslator` support, wrap strings in `tr()`, generate `.ts` files.

---

## P3 — LOW PRIORITY (nice to have, future improvements, code quality)

### [P3-1] Manual string formatting could use std::format
- **File:** `src/util/FileUtils.cpp` (humanSize, formatDuration)
- **Issue:** Uses manual string concatenation. Could use `std::format`.
- **Fix:** Replace with `std::format` for brevity and performance.

### [P3-2] Color::fromHex uses std::stoi (allocating)
- **File:** `src/util/Color.cpp`
- **Issue:** Uses `std::stoi` on `substr` (allocating temporary strings).
- **Fix:** Replace with `std::from_chars` for zero-allocation hex parsing.

### [P3-3] TODO comments embedded in CMakeLists.txt
- **File:** `CMakeLists.txt:3,6,8,51,93`
- **Issue:** Multiple TODO comments in build system source.
- **Fix:** Create `docs/dev/BUILD.md` with build docs. Remove inline TODOs.

### [P3-4] All sources in single CMakeLists.txt
- **File:** `CMakeLists.txt` (457 LOC, all 137 source files)
- **Issue:** No `src/CMakeLists.txt` exists. Doesn't scale well.
- **Fix:** Consider splitting into per-module CMakeLists.txt files.

### [P3-5] CPM_LIBS target name guessing is fragile
- **File:** `CMakeLists.txt:340-357`
- **Issue:** Uses `CPM_spdlog`, `CPM_fmt`, `CPM_tomlplusplus` which may not match CPMAddPackage NAME parameters.
- **Fix:** Use system-target-first pattern consistently. Verify CPM target names.

### [P3-6] CPM.cmake downloaded at configure time
- **File:** `CMakeLists.txt:32-35`
- **Issue:** Downloads from GitHub if not present. Breaks air-gapped builds, supply-chain risk.
- **Fix:** Vendor CPM.cmake into repository.

### [P3-7] setupStyle()/setupQmlStyle() are no-ops
- **File:** `src/core/Application.cpp`
- **Issue:** Both are no-ops (kept for API compatibility). If QSS styles aren't being applied, this is a bug.
- **Fix:** Implement or remove.

### [P3-8] VisualizerRenderer::initialized_ never reset
- **File:** `src/visualizer/VisualizerRenderer.cpp`
- **Issue:** `initialized_` is never reset to `false` in `cleanup()`. Re-initialization won't work.
- **Fix:** Set `initialized_ = false` in `cleanup()`.

### [P3-9] PresetScanner category defaults to "Uncategorized"
- **File:** `src/visualizer/PresetScanner.cpp`
- **Issue:** Root-level presets get "Uncategorized" category.
- **Fix:** Allow category from `.milk` file header metadata.

### [P3-10] CSRF state not validated in SystemBrowserAuth
- **File:** `src/ui/SystemBrowserAuth.cpp`
- **Issue:** `state_` field is generated but never validated against callback response. CSRF protection incomplete.
- **Fix:** Validate `state_` against the state query parameter from callback URL.

### [P3-11] SQL injection risk in search_db.sh
- **File:** `scripts/search_db.sh`
- **Issue:** Uses `'$SEARCH_TERM'` string interpolation directly in SQL without parameterization.
- **Fix:** Use parameterized queries (sqlite3 `:var` binding).

### [P3-12] Suno tokens stored in plain text
- **File:** `config/default.toml`
- **Issue:** Auth token and cookie stored in plain text.
- **Fix:** Consider OS keyring (libsecret, KWallet) for sensitive tokens.

### [P3-13] PresetPersistence and RatingManager lack atomic writes
- **Files:** `src/visualizer/PresetPersistence.cpp`, `src/visualizer/RatingManager.cpp`
- **Issue:** Neither uses atomic writes. Crash mid-write corrupts files.
- **Fix:** Use atomic write pattern (`.tmp` + `fs::rename`) like `ConfigLoader.cpp`.

### [P3-14] LRC metadata tags not parsed
- **File:** `src/lyrics/LyricsData.cpp fromLrc()`
- **Issue:** LRC metadata tags (`[ar:Artist]`, `[ti:Title]`, etc.) are not parsed.
- **Fix:** Add LRC metadata tag parsing.

### [P3-15] sanitizeFilename() incomplete
- **File:** `src/suno/SunoDownloader.cpp`
- **Issue:** Only replaces `/` and `\`. Misses other illegal chars (`:`, `*`, `?`, `"`, `<`, `>`, `|`).
- **Fix:** Use comprehensive sanitization covering all platform-illegal characters.

### [P3-16] downloadAudio() always writes .mp3
- **File:** `src/suno/SunoDownloader.cpp`
- **Issue:** Always writes `.mp3` extension even if WAV format was requested.
- **Fix:** Use correct file extension based on actual audio format.

### [P3-17] onWavConversionReady() has redundant if/else
- **File:** `src/suno/SunoDownloader.cpp`
- **Issue:** Redundant if/else with identical branches.
- **Fix:** Remove the redundant else branch.

### [P3-18] loadM3U() appends instead of replacing
- **File:** `src/audio/Playlist.cpp`
- **Issue:** Appends to existing playlist rather than replacing. Doesn't parse `#EXTINF` lines.
- **Fix:** Parse `#EXTINF` lines. Consider adding "replace" vs "append" mode.

### [P3-19] CHANGELOG has multiple [Unreleased] sections
- **File:** `CHANGELOG_CURRENT.md`
- **Issue:** Multiple `[Unreleased]` sections with different dates. Missing entries for recently completed features.
- **Fix:** Consolidate into single `[Unreleased]` section. Add missing entries.

### [P3-20] Mixed formatting approaches across codebase
- **Scope:** Entire codebase
- **Issue:** Mixed use of `snprintf`, `std::format`, `fmt::format`, and manual concatenation.
- **Fix:** Standardize on `std::format` (C++23) as primary, `fmt::format` where spdlog requires it.

---

## P4 — COSMETIC / MINOR (style, comments, cleanup)

### [P4-1] ASCII art in main.cpp
- **File:** `src/main.cpp:1-11`
- **Issue:** ASCII art logo adds 10 lines of noise to entry point.
- **Fix:** Subjective — keep or trim.

### [P4-2] Humorous comments throughout codebase
- **Scope:** Multiple files
- **Issue:** "// Because typing std::chrono::milliseconds gets old fast" etc. May not be appropriate for all contributors.
- **Fix:** Evaluate codebase-wide comment policy.

### [P4-3] Inconsistent QML brace style
- **File:** `src/qml/panels/OverlayPanel.qml`
- **Issue:** Mixed brace styles (some `{` on same line, some on next).
- **Fix:** Apply consistent style via `qmlformat`.

### [P4-4] X-macros make debugging difficult
- **Files:** `src/qml_bridge/SettingMacros.hpp`, `src/core/CliArgs.inc`
- **Issue:** Stack traces show macro-expanded code.
- **Fix:** Evaluate replacing with a code generator script.

### [P4-5] Global APP macro
- **File:** `src/core/Application.hpp:160`
- **Issue:** `#define APP` in global namespace could collide.
- **Fix:** Use inline function or scoped macro.

---

## P5 — OPPORTUNISTIC IMPROVEMENTS (new capabilities, significant refactors)

### [P5-1] Add SQLite FTS5 for Suno library search
- **File:** `src/suno/SunoDatabase.cpp`
- **Issue:** Uses `SELECT * ... WHERE ... LIKE` across 5 columns. No FTS index. Slow on large libraries.
- **Fix:** Add SQLite FTS5 virtual table with sync triggers. Replace `LIKE` with `MATCH`.

### [P5-2] Duration format migration runs every startup
- **File:** `src/suno/SunoDatabase.cpp`
- **Issue:** Duration format migration (decimal seconds → mm:ss) runs at every startup instead of once during schema migration.
- **Fix:** Move into the schema migration block.

### [P5-3] Minimal test coverage
- **Scope:** Entire test suite
- **Issue:** Only 5 unit tests across 2 files actually compiled and run. Integration tests are no-op.
- **Fix:** Add tests for: `LyricsData::findLineIndex()`, `LyricsData::search()`, ConfigParsers round-trip (all sections), `AudioAnalyzer::analyze()`, Playlist navigation, LyricsSync position computation.

### [P5-4] Migrate Result\<T\> to std::expected
- **File:** `src/util/Result.hpp`
- **Issue:** Custom `Result<T>` (195 LOC) reimplements what `std::expected` (C++23) provides natively. Missing `transform`, `or_else` for error path.
- **Fix:** Migrate to `std::expected<T, Error>`. Large refactor (~50 call sites) but better standard library alignment.

### [P5-5] Custom Signal\<\> duplicates Qt signals
- **File:** `src/util/Signal.hpp`
- **Issue:** Custom `Signal<Args...>` (155 LOC) with mutex-based thread safety reimplements Qt signals. Used for non-QObject classes (valid use case) but duplicates machinery.
- **Fix:** Document why custom Signal is preferred over Qt signals. Consider submitting as standalone library.

### [P5-6] Dual-player gapless architecture is complex
- **File:** `src/audio/AudioEngine.hpp`
- **Issue:** Manual pointer swapping and signal reconnection. `swapPlayers` disconnects ALL signals via nullptr pattern — heavy-handed.
- **Fix:** Consider abstracting into a `GaplessPlayer` class.

### [P5-7] Unused SunoModel structs
- **File:** `src/suno/SunoModels.hpp`
- **Issue:** `SunoProject`, `SunoPlaylist`, `SunoFeatureGate` are defined but never referenced.
- **Fix:** Implement features using these models or remove them.

### [P5-8] Hardcoded page size in SunoLibraryManager
- **File:** `src/suno/SunoLibraryManager.cpp`
- **Issue:** Hardcoded page size of 20 (`clips.size() >= 20`). Fragile if API changes.
- **Fix:** Extract to named constant or config-driven parameter.

### [P5-9] Complete B-Side feature implementation
- **Scope:** AGENTS.md B-Side items
- **Issue:** Orchestrator wired; endpoint map centralized; feature gates unused; workspace/session persistence TODO.
- **Fix:** Complete B-Side: workspace persistence, session management, feature gate enumeration.

### [P5-10] Runtime theme switching
- **File:** `resources/` (13 QSS + 3 style QSS files)
- **Issue:** Theme files exist but no runtime switcher. Users must rebuild or edit config.
- **Fix:** Implement runtime QSS loading. Add theme selector in Settings. Support user themes in `~/.config/chadvis-projectm-qt/themes/`.

### [P5-10] Theme style sheets and theme switcher
- **File:** `resources/` (13 QSS + 3 style QSS files)
- **Issue:** Only a quick dark theme `dark-theme.qss` is implemented. All others are stubs. May be benefital to create the apporpriate directory or directories for the Qt/QMl style sheets as to keep the project organized. Model training data, web search tool calls, or github searches for repos to clone and investigate should be able to provide color mapping for all the stub files the user created of various top community `nerd` themes found in software such as VS Code.
- **Fix:** Write all stub or empty theme qss files, implement the ability to switch themes in the settings window, and ensure this setting, just like all others, is able to be stored persistantly within one of the packages directories within the users home.

### [P5-11] Profile export/import
- **File:** `src/qml/panels/SettingsPanel.qml:489-501`
- **Issue:** Profile buttons are `enabled: false` placeholders.
- **Fix:** Implement profile serialization (settings, bridge state, favorites) to portable `.toml`/`.json`.

### [P5-12] Karaoke settings not persisted
- **File:** `src/qml/panels/KaraokeSettings.qml`
- **Issue:** `showGlow`, `verticalPos` are bound in QML but never persisted to `SettingsBridge`. Changes lost on restart.
- **Fix:** Wire to `SettingsBridge` karaoke properties for persistence.

### [P5-13] SunoClient pollWavFile not cancellable
- **File:** `src/suno/SunoClient.cpp`
- **Issue:** `pollWavFile()` uses `QTimer::singleShot` recursion — not cancellable, continues polling during shutdown.
- **Fix:** Implement proper polling state machine with cancellation support.

### [P5-14] Default build type is Debug
- **File:** `CMakeLists.txt:27`
- **Issue:** Unoptimized builds by default, impacting real-time visualizer performance.
- **Fix:** Consider `Release` or `RelWithDebInfo` as default.

### [P5-15] No automated release pipeline
- **Scope:** `.github/workflows/`
- **Issue:** GitHub Actions workflow is for CI only, not releases. No automated package building.
- **Fix:** Add workflow for building and publishing releases (AppImage or Arch package).

### [P5-16] Audio playback filetypes
- **File:** Unknown (user additon)
- **Issue:** Suno.com remote only provides mp3 or wav audio filetypes. Local playback should be able to play more.
- **Fix:** There is a note from the Xiaomi model that has at least one library for wide support audio filetypes that can be included via CPM.

---

## CROSS-CUTTING CONCERNS

### [CC-1] Forward declarations in Types.hpp create coupling
`src/util/Types.hpp:106-110` forward-declares `Config`, `AudioEngine`, `Playlist`, `VisualizerWindow`, `VideoRecorder`. These are used by `BridgeRegistration.hpp` but nearly unused by `Types.hpp` itself. Any change to these headers triggers recompilation of everything including `Types.hpp`. Consider moving to a dedicated `FwdDecl.hpp`.

### [CC-2] Missing [[nodiscard]] on key functions
`Playlist::next()`, `Playlist::previous()`, `Playlist::jumpTo()` return `bool` indicating success but are not marked `[[nodiscard]]`. Callers may silently ignore failures.

### [CC-3] TODO/FIXME inventory
CMakeLists.txt has 5+ inline TODOs. 4 test files are TODO stubs. QML has 3+ console.log TODOs. Convert to GitHub Issues and remove from source (may be able to be done via either `git` or `gh` commands, else via direct github calls, falling back to informing the user or writing a file that is inside `.gitignore` for the user to handle).

### [CC-4] Magic numbers throughout codebase
`LyricsFactory::alignWordsToLines` hardcodes search window of 50 words. `AudioAnalyzer` beat threshold hardcoded at 1.5f. `VisualizerQFBO` max log count is 5. Thread loop timeout is 10ms. Should be named constants in Config. The other model codebase examination markdown documents also notes additional magic numbers. Anything that may have any chance of user configuration or ability to be adjusted and tweaked should be able to be done so via the GUI even if hidden away for lesser adjusted settings and via one of the packages user config/settings storage file(s).

### [CC-5] Unicode/Emoji dependency in parser
`LyricsData.cpp fromSunoJson()` checks for 🎵 emoji in instrumental tags. Fragile if Suno changes format. User Edit: Fairly certain suno never has any emojis in its json output. Should be able to make direct api calls via curl etc from whats in the codebase, or the user can provide the cookie details from their browser to make POST calls to the API to see exact formatting. There is likely garbage code from previous attempts at this when the user was trying to do SRT and related format lyrics saving, while this may  be reexamined in the future it adds unneeded complexity early on.

### [CC-6] FFmpeg AVFormatContextDeleter potential double-close
`FFmpegUtils.hpp` `AVFormatContextDeleter` calls `avio_closep` before `avformat_free_context`. FFmpeg docs suggest `avformat_free_context` may handle this internally. Verify to prevent double-close.

### [CC-7] Inconsistent 401 handling
`SunoClient::handleNetworkError()` clears token on 401 but doesn't trigger re-auth. `SunoLyricsManager` re-queues on 401. `SunoOrchestrator` just emits error. Inconsistent handling across the Suno module. User edit: given the files within `docs/`, `.agent`, and many found via a find commandfor for sniffed captured such as `json`, `js`, `burp` `suno <AND> api`, and simply `API`, found within the users `$HOME/{git,Documents,Downloads}` diretories will give more insight. There are also markdown doucments within the repository of some agents findings investigating these gigantic network traffic captures. 

### [CC-7a] Feature gates, sigserv feature flag beta or b-side testing, and other "hidden" or invite-only features
Investigating the found js files in the suno sitemap captured via normal browsing with a "scraper" browser extension via ripgrep searches of the suno POST endpoint API and its parameters finds many instances with keywords such as: `gate`, `VIP|vip`, `premium`, `prod`, `dev`, `unlock`, `invite`, `feature`, `beta`, `test`, `internal`, `staff`, `internal`, and the like has led to some intereting findings for example the chat feature which we have (untested) implmented as well as the as-of-yet not fully unfunctional but navigable via hte browser with a enabled boolean flip userscript shows a marketplace skeleton. A lot of these lilely onl work via diret POST endpoint API calls with no js and related webui frontend exposure which may explain why most features in the users browser extension do nothing when toggled.

### [CC-8] Legacy MVC controllers may be unused
`AudioController`, `RecordingController`, `VisualizerController` capture `this` in lambdas without null checks (dangling pointer risk on shutdown). These appear to be legacy carryover from a QWidgets architecture and may be unused in the current QML-only build. Verify and remove if dead.

---

## SUMMARY STATISTICS

| Severity | Count |
|----------|-------|
| P0 (Critical bugs) | 4 |
| P1 (High priority) | 23 |
| P2 (Medium priority) | 32 |
| P3 (Low priority) | 20 |
| P4 (Cosmetic) | 5 |
| P5 (Opportunistic) | 15 |
| Cross-cutting | 8 |
| **Total** | **~107** |

### Modules with most issues:
1. `recorder/` — 8 issues (including 1 critical)
2. `qml/` — 7 issues
3. `qml_bridge/` — 7 issues
4. `build/` — 10 issues
5. `suno/` — 6 issues
6. `lyrics/` — 5 issues
7. `audio/` — 5 issues
8. `tests/` — 6 issues
9. `visualizer/` — 4 issues
10. `core/` — 3 issues

### Recommended immediate actions (P0):
1. Fix `VideoRecorderThread.cpp` brace mismatch — audio never encoded during recording
2. Fix `writePacket()` nullptr dereference in `VideoRecorderFFmpeg`
3. Fix `LyricsRenderer` overlay bounds check
4. Decide fate of `submitAudioSamples()` no-op

### Recommended next sprint (P1):
1. Remove duplicate `AudioQueue` forward declaration
2. Remove orphaned `LyricAligner.hpp`
3. Fix namespace inconsistencies (Orchestrator, controllers)
4. Wire `test_PresetScanner` into CMakeLists
5. Fix `build.sh` → `build.zsh` delegation
6. Remove `VisualizerItem` dead code (keep QFBO)

---

*This audit was performed as a read-only operation. No source files were modified. All findings are from direct file reads with exact file paths and line numbers.*
