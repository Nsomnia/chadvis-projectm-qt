# ChadVis ProjectM-QT Refactor: AGENTS.md

## Sys Instructions and requirements
- **Caveman integration:** Ensure the caveman skill is being used, else install it for token efficiency first. `npx skills add JuliusBrussee/caveman`
- **Oh-my-opencode-slim:** OMO-slim is used on the users system (not used in cloud sessions).
- **NEVER rm:** Never run `rm` commands for safety; instead append a date-time string to the file name and then move to appropriate directory within `.backup_graveyard/` for archival and data safety. It doesn't nessecarily have to be commited to git.
- **git:** Ensure git commits are made frequently enough that there is a great history of operations that an agent can quickly parse when doing a `git history --oneline`. Also do very thorough detailed commit messages where appropriate such as completing a chat session or othe major large changes so that an agent can get more details when and where needed.
- **General guidelines:** Worth a perusal when a new or complex session, but not mandatory/law: `GENERAL_LLM_STARTING_PROMPT.md`.

## Legend & Rules
- `[ ]` untouched.
- `[~]` in-progress task.
- `[x]` Finished for user review. Only the user may remove tasks, however the LLM model/agent is free to refactor, add, and reorganize all elements freely. The user may make changes at anytime with this noted.
- `[?]` NOTE: something is blocking work being done.
- `[!]` User or model attention is needed as soon as possible.

## General Guidline
- Git is a powerful history for future agent chat sessions as they *always* exceed the compression or max token usage per chat limits and thus context is lost. This project spans months and months with hundred(s) of millions of tokems invested over many models and generations.
- expanding on the previous: if you find code or project maintence that needs attention, whether immediate or far down the road, then add it to this list and you may just see another agent working on it simutaniously or have it done in the future (you may even work on it if you choose).
- Self prioritize task selection when given free reign over improving and expanding the codebase.
- Write modern code that uses appropriate programming paradigms.
- Unlmiited usage of tool calls as well as any packages on the users system such as debuggers, tui web browsers to dump sites like lynx, gh (github-cli), git, ddgr, and anything else avilable under `pacman -Qq`, If you require a specific tool then inform the user at end of output or stop poutput when needed immediately, This might include debuggers or language servers.
- A "Chad" and "Arch, BTW" vibe as if linus Torvolds and Linus Tech Tips were orchestrating this project, all while Richard Stallman gives gnu kung-fu wisdom in the backgroud meditating to song.
- Doducmentation, especially the root CHANGELOG.md and the history CHANGELOG.md(s) within docs/ should be kept up-to-date.
- Write test if truely benenfical to the project in the long run or if it allows you to verify logic is working programmatically.
- If you need to orchestrate a specific task to a specific model you may be able to find an approach. `opencode --models` will list models with any containing "free" being fair game . The configured API's nay not be operational or have quota so be warned. You can then run `opencode --model provider/name run "input_message_tokens"`.
- Use appropriate programming paradigms based on the current task. May be helpful to seed your context window with a brief overview of how you'll ensure maintance of current and writing of new portisons of the codebase. *Nesting files and directories is "free" whereas input and output tokens, as well as edit tool calls being tougher on huge code blocks, all ask cost with every token not being worked with. The user has been told 500 LOC for C++ 23 classes is a general good limit but your free to use best judgment.
- Follow industry stadards whenever both relevent and possible. Every aspect should not only be production ready but also ooze the vibe that it was done by a human to really try to keep everything both maintainable and to not become burden with slop (which is already happening in small degrees all over the project!).

## Ralph Loop/Self-Repetition/Infinite Mode
- You have complete freedom to, and are requested to, work as long as seems appropriate on this TODO list, so long as either there are tasks in this AGENTS.md TODO list, or you are aware of improvments that may be possible to the codebase. In these cases design and use a simple self-harness for repeating until your confident that your work is done and start with this harness wrapper, then end with a self promise statement of all tasks being done, compiled, tests run where appropriate, verified, logs checked, git commits made and then pushed to remote, and so-on until one step is fatally blocked or broken without being able to amend.

---

## P0: Critical Bugs & Safety (Data Loss / Crashes / Security)

### Memory & Thread Safety
- [ ] **PFFFT static locals thread-unsafe** — `AudioAnalyzer::performFFT()` uses static work/output arrays; concurrent calls = data race. Make thread-local or use per-instance buffers.
- [ ] **SunoClient use-after-free** — Network replies can outlive client; dangling pointer on delayed responses. Lifetime audit needed.
- [ ] **VideoRecorderFFmpeg nullptr deref** — `avcodec_alloc_context3()` return not checked; null codec ctx = crash.
- [ ] **VideoRecorderThread brace mismatch** — Audio encoding path skipped due to brace scope error; audio never encoded during recording.
- [ ] **LyricsOverlayRenderer OOB access** — Out-of-bounds array access in renderer; crash on edge-case lyric data.
- [ ] **AudioEngine scratch buffer resize in audio callback** — Allocation in RT path = undefined behavior under SCHED_FIFO. Pre-allocate or use lock-free ring.
- [ ] **Application destructor destruction order** — Members destroyed before dependent subsystems; potential use-after-destroy on shutdown.
- [ ] **Playlist::loadM3U path traversal** — No sanitization on paths from M3U files; `../../` escapes library dir. Validate/canonicalize.

### Security & Credentials
- [ ] **SunoPersistentAuth/SystemBrowserAuth credential storage audit** — Tokens stored in plain text QSettings; should use OS keychain (libsecret/KWallet).
- [ ] **CSRF state not validated in SystemBrowserAuth** — OAuth state parameter not verified; open redirect vulnerability.
- [ ] **SQL injection risk in search_db.sh** — User input concatenated into SQL; parameterize queries.

### Stubs & No-Ops (Functional Dead Code)
- [ ] **submitAudioSamples() complete no-op** — AudioBridge method body empty; visualizer receives no audio data through this path.

---

## P1: High Priority (Broken Behavior / Major Architecture Issues)

### Core Infrastructure (Milestones)
- [x] Refactor Main.qml with responsive Drawer and SplitView layout
- [x] Refactor AudioEngine for better organization and granular responsibility
- [x] Implement throttled bridge updates in VisualizerBridge/AudioBridge
- [~] Complete full migration of Sidebar panels (Library, Presets, Recording) — PlaylistBridge + RecordingBridge APIs fixed; LyricsBridge search/export still stubbed
- [x] Finalize robust persistence for all settings (SettingsBridge + TOML auto-save)
- [x] Debounced auto-save (2s QTimer) on every SettingsBridge setter
- [x] Explicit save() on app close via `onClosing` in Main.qml
- [x] UIConfig expanded with `expandedPanel`, `sidebarWidth`, `drawerOpen`
- [x] SettingsBridge Q_PROPERTYs for UI state (expandedPanel, sidebarWidth, drawerOpen)
- [x] ConfigParsers parseUI/serialize updated for new UI fields
- [x] Main.qml wired bidirectionally: accordion/drawer/sidebar ↔ SettingsBridge
- [x] default.toml updated with new [ui] keys

### Audio Engine
- [ ] **AudioQueue triple queue redundancy** — 3 separate queues for same PCM data (3x memory). Consolidate to single ring buffer with multiple consumers.
- [ ] **Naive beat detection** — Energy-ratio approach from 1990s; no spectral analysis. Implement onset detection or spectral flux.
- [ ] **No FFT windowing function** — Rectangular window = spectral leakage. Apply Hann/Hamming window before FFT.
- [ ] **No true gapless playback** — `swapPlayers()` introduces audible gap. Pre-buffer next track; crossfade or seamless splice.
- [ ] **AudioEngine no sample format validation** — No check that audio output format matches source; potential distortion/crash on mismatched formats.
- [ ] **AudioEngine analyzerWorker busy-wait** — Fixed `QThread::msleep()` spin-wait; should use wait condition or event-driven wake.

### Application Architecture
- [ ] **Application god object** — 150+ line `init()`, owns everything. Split into subsystem managers (AudioSubsystem, UISubsystem, etc.).
- [ ] **Application singleton via raw pointer** — `g_app` raw pointer; should be `unique_ptr` or stack-allocated. Risk of double-delete or leak.
- [ ] **Config parser defaults don't match struct defaults** — TOML defaults diverge from C++ struct initializers; silent wrong values on partial config.
- [ ] **Config::save() errors ignored** — Save failures silently swallowed; user loses settings without warning.
- [ ] **CLI X-macro pattern fragile** — CliArgs.inc X-macros: easy to break, poor IDE support. Consider codegen or reflection-based approach.
- [ ] **TRY macro shadows std::expected** — Custom TRY conflicts with C++23 idiom; migrate to `std::expected` monadic chain.

### Build System
- [ ] **PULSEAUDIO_FOUND checked but never searched** — `find_package(PulseAudio)` never called but `PULSEAUDIO_FOUND` referenced.
- [ ] **Icons registered twice** — Duplicate registration via `.qrc` + `qt_add_qml_module`; double resource load.
- [ ] **test_PresetScanner / test_projectm_render not in CMake** — Test targets exist but not added to CMakeLists; never compiled.
- [x] **WebEngineWidgets dead dependency** — Still linked but never used; bloats build and runtime deps.
- [ ] **Remove ~20 stale cmake modules** — Only CPM.cmake + FindProjectM4.cmake used; Conan.cmake, Vcpkg.cmake, Doxygen.cmake, etc. are dead.

### Suno Integration
- [~] **B-Side feature set** — Orchestrator wired into controller/bridge; endpoint map centralized; feature gates still unused
- [ ] **Implement Generation Surface** — Full creation suite (prompt, style, seeds) with client-side overrides
- [~] **B-Side Chat/Orchestrator** — Orchestrator wired, chat flows through bridge; workspace/session persistence still TODO
- [ ] **SunoOrchestrator bypasses request queue** — Direct API calls skip rate-limiting queue; potential 429s.
- [ ] **SunoOrchestrator JSON parsing no null checks** — `.value()` calls on potentially missing keys; crash on unexpected API response.
- [ ] **SunoClient pollWavFile not cancellable** — No cancellation token; polling blocks until timeout even if user navigates away.
- [ ] **Inconsistent 401 handling** — Some controllers refresh token, others don't; user sees random auth failures.
- [ ] **Refine Suno Library search/filtering** — Local + remote; local should stay in-sync with remote if within default file structure. Downloading optional.
- [ ] **API feature parity audit** — Verify all public Suno website abilities available in package, plus b-side/testing/VIP/hidden features. Expand with local logic, advanced sorting, Suno library database.

### Namespace & Type Issues
- [ ] **Controllers in wrong namespace** — Some use `vc` instead of `vc::ui`; inconsistent with project convention.
- [ ] **Missing `<cmath>` include in LyricsRenderer** — Uses `std::sin`/`std::cos` without including `<cmath>`; breaks on some compilers.
- [ ] **Dual position update path in LyricsSync** — Timer + signal both update position; race condition on timing.

### Dangling Pointers & Dead Code
- [ ] **Dangling pointers from getContextLines/getUpcomingLines** — Return raw pointers to container elements; invalidated on any modification.
- [ ] **PlaylistItem::valid never read** — Field set but never checked; dead state.
- [ ] **PresetBridge::cachedPresets_ never used** — Populated but never read; wasted memory.
- [ ] **OverlayElementConfig animation fields unused** — Declared but never applied in rendering.
- [ ] **VisualizerItem dead code** — QML component registered but never instantiated.
- [ ] **Unused includes** — 5+ files with `#include` for types never referenced.

### Config & Defaults
- [ ] **debug=true in default.toml** — Production default config has debug logging enabled; performance impact.
- [ ] **Duration migration runs every startup** — DB migration check always re-runs; should be idempotent or track completion.

### QML
- [ ] **SettingsPanel.qml monolithic 526 LOC** — Single file handles all settings tabs; split into per-category components.

---

## P2: Medium Priority (Performance / Code Quality / Maintainability)

### Performance
- [ ] **rand() instead of `<random>`** — `std::rand()` used for shuffle/randomization; not thread-safe, poor distribution. Use `std::mt19937` or `std::random_device`.
- [ ] **PresetBridge rebuilds QVariantLists every access** — No caching; full rebuild on each QML read. Cache or use QAbstractListModel.
- [ ] **LyricsData::search() allocates every call** — Returns vector by value; hot path allocates. Return view or cache results.
- [ ] **flipImageGPU() allocates textures/FBOs per frame** — No reuse; GPU allocation every frame = stutter. Pool or persist resources.
- [ ] **AudioAnalyzer::pcmData() returns full copy** — 4KB+ copy per frame for spectrum data. Return span/view instead.
- [ ] **threadLoop() spin-waits ~100fps idle** — Busy-wait when no work; wastes CPU. Use condition variable or event-driven.
- [ ] **parseDuration() uses std::regex** — Regex engine heavy for simple duration parsing; hand-write parser or use `std::from_chars`.
- [ ] **AudioSpectrum passed by value in signal** — 4KB struct copied per emission; pass by const ref or use shared_ptr.
- [ ] **Playlist::addFile synchronous metadata reading** — Blocks UI thread while reading tags; move to worker thread.
- [ ] **AudioEngine::loadLastPlaylist no error recovery** — If file missing/corrupt, no fallback; silent empty playlist.
- [ ] **MediaMetadata::formatLine inefficient string replacement** — Multiple `QString::replace()` calls; build format string once.
- [ ] **AudioFrame alignas(64) wastes 52 bytes** — Over-aligned for cache; 64-byte alignment on ~12-byte frame wastes 52 bytes per frame in ring buffer.
- [ ] **No constexpr usage** — Compile-time constants computed at runtime; `constexpr` where possible for zero-cost init.
- [ ] **No [[nodiscard]] on Result<T> returns** — Discarded error results silently lost; add `[[nodiscard]]` to all Result-returning functions.

### Code Organization
- [ ] **VideoRecorder.hpp pointless facade** — Thin wrapper around VideoRecorderFFmpeg with no added abstraction; inline or remove.
- [ ] **GLIncludes.hpp empty stub** — No content; dead include. Remove or populate.
- [ ] **Hardcoded path in default.toml** — `/home/nsomnia/...` in default config; use `$HOME` or XDG paths.
- [ ] **Stub test files dead code** — Test stubs that do nothing; remove or implement.
- [ ] **PKGBUILD missing deps / wrong description** — Incomplete dependency list; description doesn't match project.
- [ ] **PlaylistBridge takes address of reference** — `&refParam` = pointer to potentially-temporary; undefined behavior.
- [ ] **Result.hpp→std::expected migration** — Custom Result<T> has map/andThen but missing orElse; migrate to `std::expected` with full monadic API.
- [ ] **FileUtils Color::fromHex()/toHex() location** — Should move to Types.hpp or dedicated Color.hpp for cohesion.
- [ ] **PresetBridge→QAbstractListModel** — `presetToVariantMap()` rebuilds every access; use proper model for large lists.
- [ ] **SunoBridge onLibraryUpdated() manual QVariantMap** — Hand-built map should use shared conversion function.
- [ ] **Duration type unclear definition** — Multiple duration representations; unify to strong typedef.
- [ ] **Application::printHelp 80+ lines hardcoded** — Hardcoded help text; generate from CLI table or metadata.
- [ ] **CHANGELOG multiple Unreleased sections** — Duplicate `[Unreleased]` headers; consolidate into single unreleased section.
- [ ] **loadM3U appends not replaces** — Should clear playlist before loading; currently accumulates.
- [ ] **SQLite FTS5 not used** — Full-text search available but not enabled; add FTS5 virtual table for lyrics/search.

### QML / UI
- [ ] **Settings panel reuses playback icon** — Same icon for different actions; confusing UX. Add distinct icons.
- [ ] **Theme.qml missing textPrimaryVariant** — Incomplete theme definition; text variants needed for hierarchy.
- [ ] **Playlist "Show in Folder" unreliable on Linux** — `xdg-open` path handling broken for some DEs; fallback to file manager directly.
- [ ] **Color picker unimplemented** — UI element present but no functionality.
- [ ] **Fullscreen shortcut unimplemented** — F11 key binding declared but not wired.
- [ ] **PresetPanel rebuilds model on every change** — Full model reset on single preset change; use beginInsertRows/beginRemoveRows.
- [ ] **Record button highlight inverted** — Active recording shows wrong state; logic inverted.
- [ ] **OverlayBridge saves on every change, no debounce** — JSON persistence writes on every property change; add 2s debounce like SettingsBridge.
- [ ] **ThemeBridge only exposes 2 writable colors** — Most theme colors read-only; expose setters for runtime customization.
- [ ] **No internationalization support** — All strings hardcoded in QML/C++; no i18n framework. Add Qt Linguist.

### Tooling & Static Analysis
- [ ] **.clang-tidy variable naming rules wrong** — Config doesn't match project convention; generates false positives.
- [ ] **.clang-tidy missing useful checks** — Modernize, bugprone, concurrency checks disabled; enable incrementally.
- [ ] **.clangd config minimal** — Missing compilation database hints, clang-tidy integration, header search paths.

---

## P3: Low Priority (Polish / Best Practices / Future-Proofing)

### Code Quality
- [ ] **Color::fromHex uses std::stoi (allocating)** — `std::stoi` allocates; use `std::from_chars` for zero-alloc parse.
- [ ] **TODO comments in CMakeLists** — Multiple `// TODO` in CMake; track here or resolve.
- [ ] **All sources in single CMakeLists** — Monolithic source list; split into per-module subdirectories with `add_subdirectory`.
- [ ] **CPM_LIBS target name guessing fragile** — CPM integration guesses target names; use `CPMFindPackage` with explicit targets.
- [ ] **CPM.cmake downloaded at configure time** — Network fetch during build; should be vendored or fetched via FetchContent.
- [ ] **setupStyle()/setupQmlStyle() are no-ops** — Empty functions; remove or implement.
- [ ] **VisualizerRenderer::initialized_ never reset** — Flag set once, never cleared; can't re-init renderer.
- [ ] **PresetScanner category defaults to Uncategorized** — Should infer from directory structure or metadata.
- [ ] **LRC metadata tags not parsed** — `[ar:Artist]`, `[al:Album]` etc. ignored; parse and expose.
- [ ] **sanitizeFilename() incomplete** — Doesn't handle Unicode, reserved names (CON, PRN), or path separators.
- [ ] **downloadAudio() always writes .mp3** — Extension hardcoded regardless of actual format; detect from content-type.
- [ ] **onWavConversionReady() redundant if/else** — Both branches do same thing; simplify.
- [ ] **PresetPersistence/RatingManager lack atomic writes** — Non-atomic file writes; crash = corrupt file. Use write-rename pattern.
- [ ] **Playlist no thread safety** — Accessed from UI + audio threads without synchronization; add mutex or make thread-safe.
- [ ] **MediaMetadata only supports MP3/FLAC album art** — No WAV, OGG, M4A cover art extraction; extend with taglib.
- [ ] **CircularBuffer::getSpans() defined but never used** — Dead method; remove or find use case.
- [ ] **AudioAnalyzer::pcmData() unnecessary copy loop** — Copies data that could be returned as view; eliminate copy.
- [ ] **AudioEngine.hpp includes projectM.h but doesn't use it** — Dead include; remove.
- [ ] **CliUtils::findClosestMatch truncated** — Fuzzy match logic incomplete; doesn't handle all edge cases.
- [ ] **#pragma once non-standard** — Use traditional include guards for .inc files; `#pragma once` OK for .hpp.
- [ ] **Missing include guards in .inc files** — CliArgs.inc etc. have no guards; multiple-include risk.
- [ ] **Inconsistent namespace usage** — Some files `vc::ui`, others `vc`, some top-level; standardize on `vc::ui`.
- [ ] **README humor over documentation** — More memes than useful info; add proper build/run/contribute sections.
- [ ] **docs/suno_api/README.md reverse-engineered API** — Legal gray area; add disclaimer about unofficial API usage.

### C++23 Modernization
- [ ] **g_app raw pointer → unique_ptr** — Global app pointer should use smart pointer for ownership clarity.
- [ ] **CircularBuffer could use std::array** — Fixed-size buffer; `std::array` over raw new/delete.
- [ ] **Shuffle seed configurable** — Allow deterministic shuffle for testing/reproducibility.
- [ ] **PlaylistItem std::variant for lyric sources** — Multiple source types; variant > tagged union.
- [ ] **CliArg type safety with std::variant** — Stringly-typed args; variant for type-safe dispatch.
- [ ] **POSIX isatty not portable** — `isatty()` Unix-only; use `QFile::exists()` or cross-platform check.
- [ ] **Structured logging with JSON format** — For AI parsing and log aggregation; add structured log sink.
- [ ] **std::mdspan for FFT** — Multi-dimensional view over audio buffers; zero-cost abstraction.
- [ ] **Concepts/constraints in templates** — Template parameters unconstrained; add C++20 concepts.

---

## P4: Feature Enhancements

### Suno "Chad" Integration
- [x] Upgrade Suno API to feed/v3 for library access
- [x] Implement infinite scrolling for Suno Library (Pagination)

### UI/UX & Polish
- [x] Implement smooth height animations for AccordionPanel transitions
- [x] Expand Settings.qml with comprehensive engine/recorder controls
- [ ] Implement "Modern Visualizer Overlay" with reactive text/graphics
- [ ] Add "Karaoke Master" mode: Synced lyrics with custom aesthetic overrides
- [ ] Karaoke settings not persisted — Aesthetic overrides lost on restart; save to config.

### Strategic Goals: Maximum Customizability
- [ ] TOML-based "Chad Config": Every UI constant and engine parameter exposed
- [ ] Profile Support: Save/Load different UI themes and visualizer preset banks
- [x] Persistent state for all sidebar toggles and view modes

### Themes & Customization
- [ ] qss themes and theme switching — Default qss theme files in sub-directory; settings UI section to switch.
- [ ] Custom user themes — End user adds themes to appropriate/custom directory; auto-populated in switcher.
- [ ] Runtime theme switching — Currently requires restart; implement live theme reload.
- [ ] ThemeBridge full color exposure — Only 2 writable colors currently; expose all theme colors for runtime editing.

---

## P5: Infrastructure & Pipeline

### Build & Release
- [ ] **Default build type Debug** — Should default to ReleaseWithDebInfo for distribution; Debug only via explicit flag.
- [ ] **No release pipeline** — No CI/CD for tagged releases; add GitHub Actions for build+package+publish.
- [ ] **Audio playback filetypes limited** — Only MP3/FLAC/WAV; add OGG, M4A, OPUS via taglib/ffmpeg.

### Testing
- [ ] **Minimal test coverage** — Core audio engine, playlist, config parsing untested. Add unit tests for critical paths.
- [ ] **Any tests useful for agentic workflows** — Industry standard or health-check tests for development cycles.

### Documentation
- [ ] **CHANGELOG.md maintenance** — Keep root CHANGELOG.md and docs/ CHANGELOGs up-to-date per AGENTS.md guidelines.

---

## Codebase Audit & Refactoring (2026-04-28)

Full audit of 19,294 LOC across 10 modules. 24 issues found, 18 fixed across 5 phases.

### Completed (Phases 1-4)
- [x] **#1/#12** Lyrics unification: LyricsFactory canonical parser + AlignedLyrics conversion methods; removed dead LyricAligner.hpp
- [x] **#2** SunoDatabase: Extracted `clipFromQuery()` helper (3 identical blocks → 1)
- [x] **#3** SettingsBridge: X-macro table + SettingMacros.hpp (453→~120 LOC)
- [x] **#4** Fixed broken QML Theme refs in KaraokeMaster.qml + KaraokeSettings.qml
- [x] **#5/#13** CLI argument table: CliArgs.inc + per-type X-macros + applyOverride<T> (687→584 LOC)
- [x] **#6/#23** SunoController lyrics dedup: Uses LyricsFactory directly, removed 84-line fallback parser
- [x] **#7/#8** SunoDownloader: Extracted `getDownloadDir()` (5x) + `sanitizeFilename()` (4x)
- [x] **#9/#22** Unified formatDuration/formatBytes into FileUtils (removed 3 local duplicates)
- [x] **#10** VisualizerBridge: Wired stubs to real VisualizerWindow (added visualizerWindow Q_PROPERTY, actualFps())
- [x] **#11** Namespace migration: `chadvis` → `vc::ui` in SunoPersistentAuth/SystemBrowserAuth
- [x] **#15** Lerp consolidation: Single `vc::lerp()` in Types.hpp (removed 3 duplicates)
- [x] **#16** Removed orphaned MIT license block from SunoAuthManager.hpp
- [x] **#17** Removed duplicate `#include <QtSql/QSqlDatabase>` from SunoDatabase.hpp
- [x] **#18** Removed duplicate GL state setup in VisualizerQFBO::render()
- [x] **#19** Archived dead LyricsLoader.hpp to `.backup_graveyard/lyrics/`
- [x] **#21** Removed orphaned `OverlayEngine` forward-decl from Types.hpp + Application.hpp
- [x] **#24** Removed stale `${KISSFFT_INCLUDE_DIRS}` from CMakeLists.txt

### Remaining (Phase 5+)
- [ ] **#14** OverlayBridge uses separate JSON persistence instead of Config — documented, left alone (JSON appropriate for list data; future: add debouncing)
- [ ] **#25** VisualizerBridge::toggleActive() still no-op (no pause/resume in VisualizerWindow)
- [ ] **#26** LyricsBridge has 6 stubbed methods (exportToSrt, exportToLrc, search, getUpcomingLines, getContextLines)

### Net Impact
- **38 files changed, -894 net LOC removed** (2022 deletions, 1128 insertions)
- 7 commits: Phase 1 quick wins → Phase 2 dedup → Phase 2 lyrics/namespace/lerp → Phase 3 SettingsBridge → Phase 3 CLI table → Phase 4 VisualizerBridge+lyrics → archive

---

## Architectural Goals (Long-Term)

- [ ] **DI over singletons** — Replace global singletons with dependency injection; improve testability and decoupling.
- [ ] **Separate audio processing from UI** — Audio engine should be headless library; UI consumes via bridge interfaces.
- [ ] **Consider std::execution/jthread** — C++23 parallel algorithms and join-aware threads for audio pipeline.
- [ ] **QML BackendBridge consolidation** — Multiple bridge singletons → unified backend interface with namespaced properties.
- [ ] **Build system improvements** — Split CMakeLists into per-module subdirs; vendor CPM.cmake; add Conan/vcpkg as optional.

---

## C++23 Agent Guidelines
- **Standard**: C++23 is the required minimum.
- **I/O**: Prefer `std::println` over `std::cout` or `printf`.
- **Error Handling**: Prefer `std::expected` for error handling (utilize monadic `.and_then()`/`.or_else()`).
- **Optionals**: Use monadic operations for `std::optional`.
- **Target**: Arch Linux (latest GCC/Clang) is the primary development target.

---
*Consolidated from IMPROVEMENTS_*.md files on 2026-05-10. Sources: MINIMAX-M2.5, openrouter-owl-alpha, XIAOMI-MIMO-V2.5-PRO, XIAOMI-MIMI-V2.5-FLASH.*
*Original audit: 2026-04-28 by AGENT (audit phases 1-4)*
