# TODO — ChadVis (Suno client first, projectM second)

> State marks: `[ ]` todo · `[~]` in progress · `[x]` done, awaiting verification ·
> `[?]` blocked on a human or a capture · `[!]` needs user attention now
> Roadmap: `docs/PIVOT_PLAN.md` · API authority: `docs/suno_api/ENDPOINT-INVENTORY.md` ·
> docs hub: `docs/README.md` · operating rules: `AGENTS.md`
> Native sign-in targets the system default browser plus an app-owned `127.0.0.1` loopback
> callback (Google/Facebook social login on suno.com); the live handshake still needs
> capture backing, while route preference and other `[VERIFY]` contracts remain unresolved.

This file is the single backlog. Rules live in `AGENTS.md`; do not add work items there.

---

## P0 — Critical: bugs, data loss, security

### Memory & thread safety
- [ ] **LyricsOverlayRenderer out-of-bounds access** — out-of-bounds array access in the renderer; crash on edge-case lyric data.
- [ ] **Preset scanning blocks the GUI thread** — no threading exists in `PresetScanner`/`PresetManager`; startup and `PresetBridge::rescan()` stall the UI with a large library.
- [ ] **Playlist not thread-safe** — accessed from UI and audio threads without synchronization.
- [ ] **Dual position update path in `LyricsSync`** — timer *and* signal both update position; timing race.
- [ ] **Playlist::addFile reads metadata synchronously** — blocks the UI thread on tag reads; move to a worker.
- [x] **PFFFT static locals thread-unsafe** — shared immutable RAII setup, 16-byte-aligned per-call FFT scratch, mutex-protected state, deterministic zero-on-setup-failure, plus normal and ThreadSanitizer tests.
- [x] **SunoClient use-after-free** — request epochs fence queued retries/waiters, tracked replies are disconnected and aborted, stale restore/upload callbacks cannot publish after credential invalidation.
- [x] **QML cached singleton lifetime** — cached bridge pointers reset when their singleton is destroyed.
- [x] **VideoRecorderFFmpeg nullptr deref** — both video and audio `avcodec_alloc_context3()` results checked.
- [x] **AudioEngine scratch buffer resize in the audio callback** — scratch preallocated during `init()`; oversized callback buffers dropped.
- [x] **Application destruction order** — QML, controller, lyrics, preset, recorder, audio, and Qt application torn down in dependency order.
- [x] **Playlist::loadM3U path traversal** — relative entries weakly canonicalized and rejected when they resolve outside the playlist directory.
- [x] **Recording video settings never persist** — `ConfigParsers::serialize` writes `[recording.video]`/`.audio` keys into `[recording]` and emits both nested tables empty, so encoder settings set in the UI are lost on restart.

### Security & credentials
- [~] **Native OAuth security** — target architecture is system browser plus an app-owned `127.0.0.1` loopback callback for Google/Facebook social login; the offline scaffold covers state/nonce/PKCE/transaction ownership. Outstanding: capture-backed proof of Clerk's loopback acceptance, plus session persistence/refresh and sign-out behavior, before the live handshake is trusted.
- [!] **History scrubbing for committed PII** — real account identifiers, a display name, and a clip UUID with copyrighted lyrics were committed and pushed. The live tree is now clean, but 13 commits still carry the material across `main`, `origin/legacy`, `origin/experiments/juce-refactor`, and tags `v1.0.0-RC1`/`v1.1.0`/`v1.1.0-BLEEDING_EDGE`. (`origin/development` and `origin/legacy` were deleted 2026-09-26 — `development`'s unique changelog content was recovered into `docs/CHANGELOG_LEGACY.md` first — which removed 3 of the 13 commits and unreferenced `a834f57`, the single worst offender.) A full review-ready plan is in [`docs/PII_SCRUB_PLAN.md`](docs/PII_SCRUB_PLAN.md); it is **proposed and not executed**. Needs an authorized `git filter-repo` pass and a force-push of every ref and tag. Treat the account identifiers as publicly disclosed and rotate the session regardless of token expiry.
- [x] **Credential storage audit** — tokens in the OS keychain via `CredentialStore` (macOS Security.framework, atomic 0600 fallback); TOML→keychain migration on first init; secrets never written to TOML or logs.
- [x] **SQL injection risk in `search_db.sh`** — queries use SQLite `.param` binding.
- [x] **Raw capture removed from the tree** — `docs/suno_api/raw/endpoints_sniffed.list` (721 KB) held DSN keys in userinfo position, a real clip UUID with copyrighted lyrics, and a user upload artifact. Archived out of the repository; the sanitized recon is the only retained raw artifact. The graveyard was purged (546 MB, 0 tracked files).

---

## P1 — High: broken behavior, major architecture

### Suno integration
- [~] **Suno core correctness** — credential storage, lossless asynchronous restore readiness, debounced Settings ownership through `SunoClient`, credential/request epochs with reply aborts, no-replay mutation policy, queued authenticated requests, feed/clip/account parsing, exact Clerk active-session selection, method-specific Studio headers, and isolated persistence seams are implemented. Route preference, sign-out, and authorized runtime behavior remain `[VERIFY]`.
- [~] **Fail closed on unverified routes and hosts** — active Orpheus/Modal, WAV conversion, legacy Clerk-host fallback, constructed media, and user-reachable lead routes are disabled; QML artwork is restricted to exact captured Suno CDN origins; the synthetic Browser-Token is removed; declaration-only fetch surfaces are retired. Never reintroduce an unverified header without a fresh capture.
- [~] **Authentication** — exact `last_active_session_id` matching, captured `touch`/`client` envelope shapes, credential-prefix normalization, and fail-closed host policy are implemented. Outstanding: route preference and preference order, sign-out evidence, and the Clerk `tokens` route — it is captured as `[T1]` but **not implemented**; the string `tokens` appears nowhere in `src/suno/`.
- [~] **Library** — cursor pagination, local filtering, DB merge, and error states are wired. Authorized-account pagination and captured-host playback still need runtime verification. Range resume is intentionally disabled.
- [~] **Account and models** — runtime model catalog and numeric account/model fields are wired. Account/billing contract fixtures and limit behavior still need capture-backed tests.
- [~] **Media and download** — playback selects only HTTPS media-array entries on the exact captured `audiopipe.suno.ai` origin, rejects forbidden sentinels, userinfo, fragments, and nondefault ports, and uses manual redirects. Unpromoted Range resume is disabled; retries restart from byte zero. Note the captured `m4a-opus`/`progressive` item is currently unplayable because the downloader accepts only `content_type == "mp3"`.
- [~] **Upload lifecycle** — initialize → direct multipart → finish is implemented for the captured `.m4a` flow. Status, clip initialization, generation linkage, limits, and full validation are not captured.
- [ ] **Generation surface** — bind typed model/catalog/limit data and the captured captcha decision to the request; add fake-request contract tests and durable queued/processing/failed UI state. Generation stays disabled until a supported CAPTCHA token flow exists.
- [ ] **Header drift capture** — reconcile route-specific Authorization/Browser-Token/Device-Id requirements from a fresh sanitized capture before changing shared header policy.
- [ ] **Video URL origin guard** — `ClipParser` stores `video_url` raw and persists it to SQLite with no origin check, unlike the image fields, and no video host is documented in the inventory. Add a captured-host allowlist or drop the field.
- [x] **Credential readiness** — the dedicated restore worker coalesces reads without dropping callers; Library and Explore wait for definitive readiness.
- [x] **Settings credential ownership** — `SunoClient` is the sole durable credential writer; edits are debounced off the GUI thread; clearing removes live and persisted credentials together.
- [x] **Remote artwork policy** — clip image fields sanitized at parse and bridge boundaries; QML receives only HTTPS on the exact captured `cdn1.suno.ai`/`cdn2.suno.ai` origins.
- [x] **Explore and notifications** — read-only Explore and notification list/badge/mark-all-read are wired from direct captures. Following-feed pagination remains `[VERIFY]`.
- [x] **Lyrics and karaoke** — captured aligned payloads flow from authoritative playback handoff into `LyricsSync` and `LyricsBridge` for Listen and Video, with late-response gating, SRT/LRC export, local search, and context/upcoming queries.
- [x] **2026-09-24 capture and docs audit** — 432 Burp items reviewed; only directly observed contracts promoted.

### Core infrastructure
- [~] **Sidebar panel migration** — `PlaylistBridge` and `RecordingBridge` APIs fixed; LyricsBridge search/export and karaoke persistence are now complete. Remaining: verify each panel against the seven-surface shell.
- [x] Refactor `main.qml` with responsive layout
- [x] Refactor `AudioEngine` for granular responsibility
- [x] Throttled bridge updates in `VisualizerBridge`/`AudioBridge`
- [x] Robust settings persistence with 2 s debounced auto-save and explicit save on close
- [x] `UIConfig` expanded with `expandedPanel`, `sidebarWidth`, `drawerOpen`; parsed, wired bidirectionally, and added to `default.toml`
- [x] Config parser defaults derive from default-constructed `ConfigData` (single source of truth)
- [x] `Config::save()` errors log actionable failures at their call sites
- [x] Removed ~20 stale cmake modules; `cmake/` now holds 7 modules in active use — `CPM.cmake`, `Compiler.cmake`, `Dependencies.cmake`, `FindProjectM4.cmake`, `Install.cmake`, `Sources.cmake`, `TargetSetup.cmake`. All are included by the top-level `CMakeLists.txt`; do not delete them.
- [x] `test_PresetScanner` wired into `unit_tests`; `test_projectm_render` stub archived

### Audio engine
- [ ] **Triple queue redundancy** — three queues hold the same PCM data (3× memory). Consolidate to one ring buffer with multiple consumers.
- [ ] **Naive beat detection** — energy-ratio approach from the 1990s; implement onset detection or spectral flux.
- [ ] **No FFT windowing function** — rectangular window causes spectral leakage; apply Hann/Hamming before the FFT.
- [ ] **No true gapless playback** — `swapPlayers()` introduces an audible gap; pre-buffer and crossfade.
- [ ] **No sample format validation** — audio output format is never checked against the source.
- [ ] **Analyzer worker busy-wait** — fixed `QThread::msleep()` spin; use a wait condition or event-driven wake.
- [ ] **No error recovery in `loadLastPlaylist`** — a missing or corrupt file yields a silent empty playlist.
- [ ] **Playback filetypes limited** — MP3/FLAC/WAV only; add OGG, M4A, OPUS via taglib or FFmpeg.
- [ ] **Album art limited to MP3/FLAC** — no WAV, OGG, or M4A cover extraction.

### Application architecture
- [ ] **Application god object** — 150+ line `init()` owns everything; split into subsystem managers.
- [ ] **`g_app` raw pointer** — should be `unique_ptr` or stack-allocated; risk of double-delete or leak.
- [ ] **CLI X-macro pattern fragile** — poor IDE support; consider codegen or reflection.
- [ ] **TRY macro shadows `std::expected`** — migrate to the C++23 monadic idiom.
- [ ] **`Result.hpp` → `std::expected`** — the custom type has `map`/`andThen`/`orElse`; migrate to the full standard monadic API and add `[[nodiscard]]` to Result-returning functions.

### Namespace, types, and dead code
- [ ] **Controllers in the wrong namespace** — some use `vc` instead of `vc::ui`.
- [ ] **Inconsistent namespace usage** — `vc::ui`, `vc`, and top-level all appear; standardize.
- [ ] **Missing `<cmath>` include in `LyricsRenderer`** — uses `std::sin`/`std::cos` without it.
- [ ] **Dangling pointers from `getContextLines`/`getUpcomingLines`** — the maps were made owned, so verify no raw-pointer variant survives; the original finding is partly resolved.
- [x] **`PlaylistItem::valid` never read** — field is gone; `struct PlaylistItem` in `src/audio/Playlist.hpp:14-22` carries no `valid` member.
- [x] **`PresetBridge::cachedPresets_` never used** — removed; `grep -rn cachedPresets_ src/` returns 0 hits.
- [ ] **`OverlayElementConfig` animation fields unused** — declared, never applied.
- [ ] **Unused includes** — five or more files include types they never reference.
- [x] **Stale-header and unused-declaration purge** — `GLIncludes.hpp` no longer exists in the tree, `setupStyle()`/`setupQmlStyle()` are gone (0 hits repo-wide), `CircularBuffer::getSpans()` is gone, and `AudioEngine.hpp` carries no `projectM.h` include.

### Build system
- [ ] **Default build type is Debug** — should default to `ReleaseWithDebInfo` for distribution.
- [ ] **No release pipeline** — no CI/CD for tagged releases; only `opencode.yml` exists. Add GitHub Actions for build, package, and publish. (The README previously carried a CI badge for a workflow that does not exist; it has been removed.)
- [ ] **CPM.cmake downloaded at configure time** — a network fetch during build; vendor it or use `FetchContent`.
- [ ] **CPM target-name guessing fragile** — use `CPMFindPackage` with explicit targets.
- [ ] **All sources in one `CMakeLists.txt`** — split into per-module `add_subdirectory` trees.
- [ ] **`PKGBUILD` missing deps / wrong description**.

---

## P2 — Medium: performance, code quality, maintainability

### Performance
- [ ] `std::rand()` instead of `<random>` — not thread-safe, poor distribution.
- [ ] `PresetBridge` rebuilds `QVariantList`s on every access — use `QAbstractListModel`.
- [ ] `PresetPanel` resets the whole model on a single change — use `beginInsertRows`/`beginRemoveRows`.
- [ ] `LyricsData::search()` allocates on every call — return a view or cache.
- [ ] `flipImageGPU()` allocates textures/FBOs per frame — pool them.
- [ ] `AudioAnalyzer::pcmData()` returns a full copy per frame — return a span.
- [ ] `threadLoop()` spin-waits at ~100 fps idle — use a condition variable.
- [ ] `parseDuration()` uses `std::regex` for trivial parsing.
- [ ] `AudioSpectrum` passed by value in a signal — 4 KB copied per emission.
- [ ] `AudioFrame alignas(64)` wastes 52 bytes per ring-buffer entry.
- [ ] `MediaMetadata::formatLine` does repeated `QString::replace()`.
- [ ] No meaningful `constexpr` usage.
- [ ] `downloadAudio()` always writes `.mp3` — detect from content-type.
- [ ] `onWavConversionReady()` redundant if/else.
- [ ] `AudioEngine::analyzerWorker` and `VisualizerRenderer::initialized_` never reset.

### Code organization
- [ ] `VideoRecorder.hpp` is a pointless facade — inline or remove.
- [ ] `PlaylistBridge` takes the address of a reference parameter — possible dangling pointer.
- [ ] `loadM3U` appends rather than replaces — should clear first.
- [ ] SQLite FTS5 not enabled — add a virtual table for lyrics and clip search.
- [ ] `PresetPersistence`/`RatingManager` lack atomic writes — a crash corrupts the file.
- [ ] `sanitizeFilename()` incomplete — no Unicode, reserved names, or path separators.
- [ ] LRC metadata tags (`[ar:]`, `[al:]`) not parsed.
- [ ] `CliUtils::findClosestMatch` truncated; `CliArg` stringly typed.
- [ ] `isatty()` not portable.
- [ ] `Color::fromHex` uses allocating `std::stoi` — use `std::from_chars`; and its home in `FileUtils` belongs in a `Color.hpp`.
- [ ] Duration has several representations — unify behind a strong typedef.
- [ ] `Application::printHelp` is 80+ hardcoded lines.
- [ ] `SunoBridge::onLibraryUpdated()` hand-builds a map that a shared converter could build.
- [ ] `#pragma once` on `.inc` files; missing include guards in `CliArgs.inc` and friends.
- [ ] `PKGBUILD` hardcoded path removed — `output_directory` now uses `~/Videos/ChadVis`.

### QML / UI
- [ ] **Fullscreen shortcut `F` is not wired** — `main.qml` reveals the Video page and logs a TODO instead of toggling fullscreen.
- [ ] **F11 declared but not wired.**
- [ ] **Color picker present but unimplemented.**
- [ ] **Settings panel reuses the playback icon** for a different action.
- [ ] **Theme.qml missing `textPrimaryVariant`.**
- [ ] **"Show in Folder" unreliable on Linux** — `xdg-open` handling breaks on some desktops.
- [ ] **No internationalization** — all strings hardcoded; no Qt Linguist integration.
- [ ] **Structured logging** — add a JSON sink for agent and log-aggregation parsing.
- [ ] ~~`ThemeBridge` writable colors~~ — moot: `ThemeBridge` is deliberately not registered as a QML singleton, because it would shadow the QML `Theme` singleton.
- [x] Record button highlight corrected against `RecordingBridge::isRecording`.
- [x] `OverlayBridge` saves with a 2 s debounce and flushes on destruction.
- [x] `SettingsPanel.qml` split from 526 LOC into 8 per-category panels.

### Tooling
- [ ] `.clang-tidy` variable-naming rules do not match project convention — false positives.
- [ ] `.clang-tidy` missing `modernize`, `bugprone`, and concurrency checks — enable incrementally.
- [ ] `.clangd` config is minimal — missing compilation-database hints and header search paths.
- [ ] `CMakeLists.txt` still carries multiple `// TODO` comments.
- [x] `docs/suno_api/README.md` carries the unofficial/support disclaimer, authority boundary, evidence labels, and secret-handling rules.

---

## P3 — Low: polish and future-proofing

- [ ] TOML-based "Chad Config" — expose every UI constant and engine parameter.
- [ ] Profile support — save and load UI themes and visualizer preset banks.
- [ ] qss themes and a switching UI section; custom user themes auto-populated; live theme reload instead of restart-required.
- [ ] "Modern Visualizer Overlay" with reactive text and graphics.
- [ ] "Karaoke Master" mode with custom aesthetic overrides.
- [ ] `std::mdspan` for FFT; concepts/constraints on unconstrained templates; `std::array` for `CircularBuffer`; configurable shuffle seed for deterministic tests; `std::variant` for lyric sources and CLI args.
- [ ] `Application::printVersion()` hardcodes `1.0.0` while `version.txt` is the source of truth.
- [ ] `PresetScanner` categories default to "Uncategorized" — infer from directory structure.
- [ ] README humor still displaces information, though Quick Start now carries the real build and test commands.
- [x] Karaoke settings persistence — `[karaoke]` is parsed and serialized.
- [x] `docs/suno_api/README.md` disclaimer and authority boundary.
- [x] Root `CHANGELOG.md` is canonical with the legacy archive in `docs/CHANGELOG_LEGACY.md`.

---

## P4 — Features and architecture

- [ ] **DI over singletons** — replace global singletons with dependency injection.
- [ ] **Separate audio processing from UI** — make the audio engine a headless library consumed via bridges.
- [ ] **`std::execution`/`std::jthread`** — C++23 parallel algorithms and join-aware threads in the audio pipeline.
- [ ] **QML bridge consolidation** — many bridge singletons into one namespaced backend interface.
- [ ] **Build system improvements** — per-module CMake subdirs, vendored CPM, optional Conan/vcpkg.
- [ ] **Audio mastering workstation pathway (JUCE under evaluation)** — P7 defers lightweight-DAW features until the export pipeline is stable. JUCE is the leading candidate for the audio engine because it supplies multi-stop `AudioFormatReader`/`Writer`, MIDI, and device-agnostic realtime I/O that would otherwise be hand-rolled. The `origin/experiments/juce-refactor` branch is retained deliberately for this reason, but none of its 2026-02 code is portable. See the considered-pathways note in `docs/PIVOT_PLAN.md`; revisit only after P5 and P6 conclude.
- [x] Suno library upgraded to `feed/v3` with infinite-scroll pagination.
- [x] Accordion height animations; expanded settings with engine and recorder controls; persistent state for all toggles and view modes.

---

## P5 — Testing and QA

- [ ] **Authorized-account smoke** — Library, Create, Listen, Explore, Notifications, Video, and Settings against a real session. The only item from the 2026-09-24 verification pass still open.
- [ ] **Capture-backed contract fixtures** — fake-request tests for account, billing, and limit behavior.
- [ ] **Health-check tests for agentic workflows** — fast signal for development cycles.
- [x] `ctest --test-dir build/tests --output-on-failure` runs all 8 registered suites. Note that `--test-dir build` discovers zero tests and still exits 0.
- [x] Offscreen integration test loads the real QML module, requires a visible/exposed `QQuickWindow`, and verifies a non-null grabbed frame.
- [x] Config parsing and audio analysis have unit coverage.

---

## Codebase audit (2026-04-28) — remaining

Full audit of 19,294 LOC across 10 modules: 24 issues found, 18 fixed across five phases, net −894 LOC (38 files changed).

- [ ] **#14** `OverlayBridge` uses separate JSON persistence instead of the config system — deliberate, since JSON suits list data. Revisit for debouncing.
- [x] **#1/#12** lyrics unification; **#2** `SunoDatabase::clipFromQuery`; **#3** `SettingsBridge` X-macro table; **#4** broken QML theme references; **#5/#13** CLI argument table; **#6/#23** lyrics dedup; **#7/#8** download helpers; **#9/#22** format helpers; **#10** `VisualizerBridge` stubs; **#11** namespace migration; **#15** `vc::lerp()`; **#16** orphaned license block; **#17** duplicate include; **#18** duplicate GL state; **#19** archived `LyricsLoader.hpp`; **#21** orphaned forward declaration; **#24** stale CMake variable; **#25** native visualizer toggle; **#26** LyricsBridge SRT/LRC export and search.

---

## Capture-gated — blocked on a human

- [~] **Native sign-in loopback** — every observed callback and final redirect in the current captures is Suno-owned HTTPS. A human capture is still owed for loopback acceptance, state/nonce/PKCE/transaction behavior, session persistence/refresh, and sign-out.
- [ ] **Clerk route selection** — `POST .../tokens` and `POST .../touch` are both directly observed; requiredness and preference order are not established.
- [ ] **Server feed search** — no reviewed request contains `searchText`; use local filtering.
- [ ] **Following-feed pagination** — the first page is observed, but no next-page token or cursor was captured. There is currently no following-feed surface in the client at all.
- [ ] **Orpheus/B-Side/VIP/hidden features** — remain disabled unless a new direct capture and a product decision promote them.
- [ ] **Fresh captures** — playlist mutation, error and rate-limit envelopes, direct generation drift, upload status and clip initialization, and other `[LEAD]` routes.
- [ ] **Orpheus model-name claims** — retired; `chirp-v4` and `chirp-auk` appear only in a deleted fork. Captured evidence shows `chirp-v3.5`.

---

## Roadmap order

1. Scrub committed PII from git history and rotate the exposed account identifiers.
2. Authorized-account smoke across all seven surfaces.
3. Library, account, and media correctness against that session.
4. Captcha-backed generation and the bounded upload lifecycle.
5. End-to-end aligned-lyrics karaoke.
6. Recording review, deterministic export, scene/keyframes, batch automation.

## Verification bar

A task is not complete from a stale binary, a declaration, a scan string, or a
historical commit. Verify the current source with a fresh configure/build, the
relevant tests, focused lint/format checks, and a real runtime path; record
observed results in `CHANGELOG.md`.
