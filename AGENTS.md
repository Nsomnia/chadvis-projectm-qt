---
schema: 1
updated: 2026-11-19
---

# ChadVis Backlog

**Refs:** `AGENTS.md` (rules) | `scripts/task.sh` (leases/audit) | `docs/dev/TODO-archive.md` (history) | `docs/suno_api/ENDPOINT-INVENTORY.md` | `docs/dev/INVARIANTS.md`

## Agent Execution Protocol
- **Syntax:** `- [<mark>] <ID> <title> (<write-set-files>) [P] #tags (after: <IDs>)`
- **Marks:** `[ ]` open | `[~]` claimed | `[?]` unverified | `[x]` verified (`STATUS/<id>.log` `VERIFY`/`CHANGE` required) | `[!]` blocked | `[-]` dropped
- **Concurrency:** `[P]` = disjoint write-set from active `[~]` claims. Never touch intersecting files.
- **Verification Bar:**
  1. `./build.sh --fast --tests` + `ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"` (never `--test-dir build`).
  2. Recorder/session changes require `--tsan` and `--asan` (`--safe`).
  3. Headless pass ≠ proof for GL, burn-in, or audio; requires real window/hardware observation.
  4. User-visible changes → `CHANGELOG.md`.

## Settled Architectural & Product Decisions (2026-10-31)
- **Turnstile / Generation:** Never solve in-process. Hand off to browser (`suno.com/create`), import result.
- **Posture & Scope:** Unofficial/unaffiliated; no audio redistribution, money-moving, or consent POSTs. Suno client first, projectM renderer second (keep local-file path independent).
- **Build & CI:** projectM pinned to `349b7cc` (4.2.0-dev). Burn-in (`libavfilter`/`libass`) strictly optional behind `CHADVIS_HAS_AVFILTER`. Windows CI is `continue-on-error` signal only.
- **API Bases:** Primary `https://suno.com/a/api`; legacy `https://studio-api-prod.suno.com/api` (live alias + RFC 8414 OAuth issuer tree—do not merge).
- **Recon Corpus (by mtime):** `~/Documents/suno-recon-har-recordings-full-api/` → `~/Documents/suno-master-utility-browser-extension/scratchpad/` → `~/Documents/sunox-chadvis-suno-frontend-projectm-qt-research/` → `~/Documents/suno-recon-active-investigation/` → `~/Documents/suno-recon/` → `~/Documents/suno-recon-cat-stealth-modekl/`

---

## P0 — Critical Blockers, Crashes & Unverified Core Flows

- [ ] T0346 **Bring non-Studio Suno capabilities into product** (`src/suno/`, `src/qml_bridge/`) #feature #user-priority
  - Sub-tasks in `docs/backlog/T0346.md`; UI surfaces in `docs/suno_api/UI-SURFACE-INVENTORY.md`.
  - Remaining: unreferenced endpoint constants (97 dead per `check-endpoints.py`), cover art chain wiring, live route verifications.
- [ ] T0334 **Measure real 2k-clip library sync wall-clock** (`SunoLibraryManager.cpp`) #risk-high #perf
  - Page size 100 + cursor-resume on failure (`failedPageIsRetriedNotRestarted`) shipped. Needs real 2k-clip timing measurement.
- [?] T0356 **Verify Suno OAuth 2.0 loopback live flow** (`src/suno/auth/`, `docs/suno_api/MCP.md`) #feature #blocks-ui
  - `SunoOAuthLoopback` (PKCE S256, `127.0.0.1:0/callback`, `suno/mcp` key) implemented & unit-tested. Needs live browser login + MCP `initialize` + studio-api scope check.
- [?] T0199 **Verify Chromium cookie import live** (`src/suno/auth/`) #blocks-ui #risk-high #cod-high
  - C++ WAL-safe SQLite copy + decryption wired to `SunoClient::setCookie()`. Needs human UI test (MT-001-D2).
- [?] T0322 **Verify audio-reactive preset rendering in real GL** (`VisualizerRenderer.cpp`, `AudioQueue.hpp`, `VisualizerAudioTap.cpp`) #blocks-ui #risk-high
  - 40ms local-file decode fallback into `AudioQueue` unit-tested. Needs live GL window confirmation (MT-001-D4.4).

---

## P1 — Active Work, Thread Safety, UI Gaps & Core Robustness

- [~] T0358 **Reactive GUI Phase 0+1: motion tokens, reduced motion, primitives** (`src/qml/styles/Theme.qml`, `src/qml/components/App*.qml`, `AccountChip.qml`, `src/qml_bridge/SettingsBridge*`) #feature #blocks-ui
  - Plan: `docs/dev/REACTIVE-GUI-PLAN.md` §3–§4. Add `durationMicro`/`durationPop`/`motionScale`, `SettingsBridge.reduceMotion`, `Wiggle`/`GlowFrame`/`RefreshSpinner` (register in `Sources.cmake`). Qt 6.8 floor. Needs desktop build + real run.
- [~] T0359 **Reactive GUI Phase 2: UI for wired Suno mutations + generation + notifications** (`src/qml/components/ClipDetailSheet.qml`, `ClipCard.qml`, `PlaylistStrip.qml`, `NavRail.qml`, `src/qml/views/{Library,Create,Notifications}View.qml`) #feature #blocks-ui (after: T0358)
  - Part 1 (`ClipMutationsSection`, `ClipActionMenu`) landed. Remaining: `Toast.qml`, `GenerationStatus.qml`, browser Turnstile handoff UI, rail badge, playlist track reorder index in C++. No optimistic UI except playlist reorder with revert.
- [!] T0327 **Confirm benign TSan report on cross-thread closure & update `tsan.supp`** (`SunoClient.cpp:267-335`, `cmake/tsan.supp`, `docs/dev/INVARIANTS.md`) #risk-high
  - Root cause isolated: `QCallableObject` capture storage vs uninstrumented `libQt6Core` `postEvent` mutex. Run `--tsan` on `test_SunoRequestEpoch` + `test_BoundedBody`, capture symbol, add suppression + amend `INVARIANTS.md` in one commit.
- [ ] T0156 **Move encoder teardown off GUI thread** (`VideoRecorderCore.cpp:168`, `RecordingBridge.cpp:323`, `RenderExecutor.cpp:802-807`) #risk-high
  - `stop()` blocks GUI on `flush()` + `cleanup()`. Finalize on worker and post completion.
- [ ] T0157 **Fix GUI-thread reentrancy in `RenderExecutor::enqueue`** (`RenderExecutor.cpp:516-518`) #risk-high
  - `enqueue` → `pump()` → `runner_()` executes synchronously on GUI thread. Post runner to worker thread.
- [ ] T0158 **Offload `RenderQueue::settle` receipt I/O** (`RenderQueue.cpp:515`, `RenderJob.cpp:252`) #risk-medium
  - Move 3 blocking receipt-write syscalls off GUI thread.
- [ ] T0159 **Fix `JobSlot&` reference invalidation in `retireSlot`** (`RenderExecutor.cpp:844-854`) #risk-medium
  - Erase slot before invoking re-entrant `jobRunner` callback that can realloc `slots_`.
- [ ] T0418 **Bound recorder finalize with timeout + QML progress** (`VideoRecorderCore.cpp`, `RenderExecutor.*`, `RecordingPanel.qml`) #risk-high (after: T0156, T0157)
  - Prevent indefinite hang on stuck FFmpeg flush; surface finalizing progress in UI. Requires real encode test.
- [ ] T0170 **Composite overlays/karaoke via FFmpeg post-pass** (`VisualizerWindow.cpp`, `src/recorder/`) #risk-high #blocks-feature
  - Native GL recorder cannot see sibling QML scene graph; wire post-pass via T0108.
- [ ] T0173 **Verify projectM broken-preset retry on real GL** (`Bridge.cpp:48`) #risk-medium
  - Retry belongs in projectM `failed` callback (not `requested`). Test with corrupt `.milk` in real window before changing.
- [ ] T0357 **Wire Suno MCP client to bridge & chat UI** (`src/suno/SunoMcpClient.*`, `src/qml_bridge/`, `src/qml/`) #feature (after: T0356)
  - `SunoMcpClient` + 7 framing tests landed. Needs bridge wiring, chat surface, and live handshake using `suno/mcp` bearer.
- [ ] T0349 **Wire `POST /persona/create/` & verify live reads** (`SunoPersonaService.*`, `src/qml/panels/settings/`) #feature (after: T0432)
  - `GET /persona/get-personas/` & `get-persona/{id}/` implemented. Wire create flow + UI behind `Personas` gate; honor `rights_clearance_available`.
- [ ] T0365 **Build Listen-for-Credits queue view + browser submit handoff** (`src/suno/SunoPreferencesService.*`, `src/qml/panels/settings/`) #feature #user-priority
  - Reads (`summary`, `tasks`, `rewards`, `stats`, `profile`, `leaderboard`) implemented. Build queue UI, hand off captcha-gated `submit` to browser, and always call `release-buffer` on abandoned tasks.
- [?] T0351 **Wire browser Turnstile handoff for generation** (`SunoGenerationService.*`, `SunoBridge.*`, `CreateView.qml`) #feature #blocks-ui
  - `/c/check` → `/generate/v2-web/` → `/clips/get_songs_by_ids` wired & tested. Blocked by `captcha_version: 2`; surface browser handoff in `CreateView`.
- [?] T0355 **Verify song-page detail parsers against live payloads** (`SunoClipDetailService.*`, `SunoBridge.*`, `ClipDetailSheet.qml`) #feature
  - 4 GETs (`attribution`, `comments`, `remixes/count`, `get_similar`) wired & unit-tested. Confirm JSON keys on live endpoints.
- [ ] T0318 **Write `©lyr` metadata & sidecar on downloaded streams** (`SunoDownloader.cpp:832-900,1085`) #feature #risk-medium
  - Populate MP4 `©lyr` atom and sidecar from `clip.metadata.lyrics` or `aligned_lyrics_json`.
- [ ] T0353 **Diagnose intermittent UI hang** (`src/qml/`) #risk-high
  - Run under `QT_LOGGING_RULES="qt.qml.binding.removal.info=true"` to isolate non-repro hang.

---

## P2 — Refactoring, Architecture, QML Hygiene & Build/CI

### C++ Architecture & Refactoring
- [ ] T0391 **Extract shared JSON object-parse helper & unify `opt*` scalars** (`src/suno/`) #risk-medium
  - 3 unchecked sites fixed; extract helper across remaining ~83 `fromJson` sites and unify `ClipParser`/`Mango`/`ModelCatalog`/`Clerk` optional scalar readers.
- [ ] T0465 **Deduplicate `readObject` helpers into `SunoJson`** (`src/suno/Suno{CoverArt,Albums,LibrarySection,Preferences,Hooks}Service.cpp`, `SunoJson.*`) #cleanup
  - Consolidate 4 identical `readObject` copies; preserve `SunoHooksService` array-wrap `{"hooks": ...}` and tighten `SunoClient::onLibraryReply` `isObject()` guard.
- [ ] T0395 **Split `VideoRecorderFFmpeg` god TU (< 600 lines)** (`src/recorder/VideoRecorderFFmpeg.*`) #cleanup
  - Extract HW-probe and subtitle-encoder into dedicated TUs.
- [ ] T0396 **Split `SunoClient` credential worker + queue into own TU** (`src/suno/SunoClient.*`) #cleanup (after: T0401)
- [ ] T0397 **De-globalize `Config` behind injected interface** (`src/core/Config.*`, `Application.cpp`, `AudioEngine.cpp`) #cleanup
  - Confine `CONFIG` macro to `main.cpp` so tests construct isolated configs.
- [ ] T0402 **Inject remaining audio/render tuning constants from `ConfigData`** (`src/core/ConfigData.hpp`, `src/audio/`, `src/recorder/RenderExecutor.*`) #cleanup (after: T0397)
  - Part (a) `quantum_frames` TOML round-trip done. Part (b): pass `AudioQueue` capacity, `VisualizerAudioTap` timings, `AudioEngine` debounce, and `RenderExecutor::setConfig()` at construction.
- [ ] T0405 **Decompose `Application` coordinator** (`src/core/Application.*`, `src/core/CliUtils.cpp`) #cleanup (after: T0397)
  - Separate CLI parsing, config overrides, and subsystem wiring while preserving `--help`/`--dump-config`.
- [ ] T0417 **Audit QML bridges & prune dead `Q_INVOKABLE`s** (`src/qml_bridge/`, `src/qml/`) #cleanup
  - Consolidate split visualizer controls; remove uncalled invokables; document owning bridge.
- [ ] T0131 **Replace remaining singletons with DI; decouple audio from UI** (`Config.hpp`, `AudioEngine.cpp`) #blocks-feature (after: T0397)
- [ ] T0132 **Consolidate QML bridge singletons into one namespaced backend** (`src/qml_bridge/`) #cleanup (after: T0417)
- [ ] T0138 **Adopt C++20/23 idioms (`std::span`, `std::array`, concepts, `std::variant`)** (`src/`) #cleanup
- [ ] T0309 **Support Windows DPAPI & Linux libsecret in `BrowserCookieImport`** (`BrowserCookieImport.cpp:223-260`) #risk-medium
  - Or explicitly gate UI affordance on non-macOS platforms.

### QML & UI Debt
- [ ] T0452 **Keep QML free of member access on `QVariantMap` properties** (`docs/dev/`) #risk-medium
  - Qt 6.12 `qmlcachegen` segfaults (exit 139) on `mapProp.field`. Compute strings in C++; track upstream Qt fix.
- [ ] T0083 **Fix WCAG contrast failures on overlay/glass surfaces** (`Theme.qml`) #cleanup
  - `glassBackground` (85% opacity) yields ~1.3:1 over projectM canvas; `textDisabled`/`recording`/`textSecondary`/`error` fail 4.5:1.
- [ ] T0119 **Raise Theme contrast ratios & add canvas scrim** (`Theme.qml`) #feature (after: T0083)
  - Raise failing tokens to ≥ 4.5:1 and add scrim behind canvas text; keep `textPrimaryVariant` unchanged.
- [ ] T0084 **Replace ~58 raw `QtQuick.Controls` with `App*` wrappers** (`src/qml/`) #cleanup
- [ ] T0406 **Deduplicate 12× file-path/URL helpers into single QML JS module** (`src/qml/`) #cleanup
  - Cover spaces, `#`, `?`, and Windows drive letters in unit test.
- [ ] T0407 **Extract shared `StateView` (loading/empty/error/retry)** (`src/qml/components/StateView.qml`, `src/qml/views/`, `PresetsPanel.qml`) #cleanup
  - Replace 3× copy-pasted tri-state blocks; handle null visualizer window in `VideoView` and busy state in `CreateView`.
- [ ] T0408 **Extract `SettingsPageShell` & move `AccountPage` gate rows to C++** (`src/qml/settings/`, `AccountPage.qml`, `SunoBridge`) #cleanup
- [ ] T0409 **Modernize `LyricsPanel` with `App*` wrappers, a11y labels & error banner** (`src/qml/panels/LyricsPanel.qml`, `LyricsBridge`) #cleanup
- [ ] T0410 **Persist karaoke glow setting & bind `VideoView`** (`KaraokeSettings.qml`, `VideoView.qml`, `SettingsBridge`) #cleanup
- [ ] T0411 **Implement shortcut editor or mark `ShortcutsSettings` read-only** (`ShortcutsSettings.qml`, `SettingsBridge`) #cleanup
- [ ] T0139 **Wire or prune dead shortcut bindings in `main.qml`** (`main.qml:511-602`, `ShortcutsSettings.qml`) #feature #risk-high
- [ ] T0412 **Move 28 JS color/time math functions from `Theme.qml` to C++ `ColorUtil`** (`Theme.qml`, `AppColorPicker.qml`, `ColorUtil.*`) #cleanup
  - Unit-test `#RRGGBBAA` vs `#AARRGGBB` byte order.
- [ ] T0413 **Unify save-state machine across `ClipCard`/`ClipDetailSheet`/`LibraryView`** (`src/qml/components/`, `LibraryView.qml`) #cleanup
- [ ] T0414 **Use `Theme` palette & bounds-checked selection in `OverlayPanel`** (`OverlayPanel.qml`, `OverlayBridge`) #cleanup
- [ ] T0415 **Replace raw font/size literals with `Theme` tokens** (`src/qml/`) #cleanup (after: T0084)
- [ ] T0416 **Preserve scroll & drop per-item `MultiEffect` in `PresetsPanel`** (`PresetsPanel.qml`, `PresetBridge`) #perf
- [ ] T0419 **Apply audio settings live or add inline restart-required badges** (`AudioSettings.qml`, `SettingsBridge`, `AudioEngine`) #cleanup (after: T0131)
- [ ] T0420 **Move QML data-shaping & `"__favorites__"` sentinels to C++ bridges** (`AccountPage`, `ClipDetailSheet.qml`, `PresetsPanel.qml`, `RecordingPanel.qml`) #cleanup (after: T0412)
- [ ] T0051 **Enable `QTP0004` & migrate QML tree to `qt_add_qml_module`** (`TargetSetup.cmake:129-134`, `src/qml/`) #risk-medium

### Build, Testing & CI
- [?] T0454 **Verify cold-cache PCH hit rate (< 5% uncacheable)** (`build.sh`, `src/pch.hpp`) #perf
  - Sloppiness/nohashdir flags exported (down from 20.4% to 18.5%). Verify on cold `--tests` run.
- [ ] T0455 **Reduce top-5 header fan-out (< 100 TUs in `ninja -t deps`)** (`Types.hpp`, `Result.hpp`, `SunoClient.hpp`, `SunoEndpoints.hpp`) #perf
- [ ] T0457 **Configure ccache/sccache remote storage on 2013 MBA** (`build.sh`, `.github/workflows/`) #perf
- [ ] T0458 **Enable `CMAKE_UNITY_BUILD` (batch 8) + interpreted QML in fast loop** (`TargetSetup.cmake`, `CMakeLists.txt`) #perf (after: T0454)
- [ ] T0093 **Add non-blocking `git clang-format --diff main` CI check** (`Compiler.cmake:80-83`, `.github/`) #risk-medium
- [ ] T0388 **Add `-Werror` opt-in job + clang-format/clang-tidy CI checks** (`cmake/Compiler.cmake`, `.github/workflows/guards.yml`, `.clang-format`, `.clang-tidy`) #risk-medium (after: T0093)
- [ ] T0097 **Split monolithic `unit_tests` binary or shard suites** (`test_main.cpp:38-72`) #risk-medium
  - Prevent single-suite crash/hang from aborting remaining 30 suites.
- [ ] T0100 **Add QML/bridge test coverage** (`tests/`) #risk-high #data-loss
  - C++ mutation/DB suites now covered; QML bridges and views still have zero automated tests.
- [ ] T0135 **Set up `qmltestrunner` harness linking `project_lib` + `project_libplugin`** (`tests/`, `TargetSetup.cmake`) #risk-high (after: T0051)
- [ ] T0136 **Add GPU CI lane or explicit "no GL coverage" badge** (`.github/workflows/`) #risk-high
- [ ] T0338 **Add `vcpkg.json` manifest with `builtin-baseline` for Windows CI** (`windows.yml:130-160`) #risk-medium
- [ ] T0133 **Replace deprecated `qt_finalize_executable` with `qt_finalize_target` & add `NSDocumentsFolderUsageDescription`** (`Install.cmake`, `TargetSetup.cmake`) #cleanup
- [ ] T0134 **Make `macdeployqt` optional during default macOS configure** (`TargetSetup.cmake:397-410`) #risk-medium
- [ ] T0300 **Add code coverage CI lane** (`.github/workflows/`, `CMakeLists.txt`) #risk-medium
- [ ] T0301 **Add static-analysis, SBOM & dependency-vulnerability CI lane** (`.github/workflows/`) #security
- [ ] T0305 **Consolidate 4 overlapping C++ skills in `.opencode/skills/`** (`.opencode/skills/`) #cleanup

---

## P3 — Feature Backlog & Polish

- [ ] T0360 **Reactive GUI Phase 3: 30 Hz smoothed audio-reactive chrome** (`src/qml_bridge/AudioBridge.*`, `TransportBar.qml`, `ClipCard.qml`, `ListenView.qml`, `VisualizerOverlay.qml`, `KaraokeMaster.qml`, `LyricsPanel.qml`) #feature (after: T0359)
- [ ] T0361 **Reactive GUI Phase 4: view polish, bounded one-shot particles & hero moments** (`src/qml/{views,panels,settings}/`, `docs/dev/manual-qa.md`) #feature (after: T0360)
- [ ] T0108 **Muxed subtitle tracks (`libavfilter` + `libass` behind `CHADVIS_HAS_AVFILTER`)** (`Dependencies.cmake`, `SubtitleBurnIn.cpp`) #feature
- [ ] T0115 **Standalone local song/preset import without Suno account** (`docs/PIVOT_PLAN.md`) #feature
- [ ] T0116 **Wire producer for existing render-job model/queue** (`Bridge.hpp:59-68`, `SunoWorkspace.hpp`) #feature
- [ ] T0313 **Unified local + Suno playback queue via `QMediaPlayer`** (`AudioEngine.cpp`, `Playlist.cpp`, `PlaylistPanel.qml`) #feature (after: T0115, T0116)
- [ ] T0117 **Internationalize ~200 QML literals with `qsTr` & `qt_add_translations`** (`src/qml/`, `CMakeLists.txt`) #cod-high #feature
- [ ] T0118 **Bind dark/light theme pair to `Application.styleHints.colorScheme`** (`Theme.qml`) #feature
- [ ] T0314 **File-based end-user theme editor & WCAG-gated JSON export/import** (`AppearanceSettings.qml`, `ThemeDefinitions.qml`, `CONFIG.md`) #feature (after: T0193, T0118)
- [ ] T0120 **Waveform per-word lyric timing editor** (`src/qml/`) #feature
- [ ] T0121 **Swappable karaoke render strategies (wipe/bounce/scroll/dual-line)** (`src/qml/`) #feature
- [ ] T0122 **Versioned JSON render receipt sidecar** (`RenderJob.hpp:129`) #feature
- [ ] T0123 **6-second quarter-res proof render mode** (`EncoderSettings.cpp`) #feature
- [ ] T0125 **Poster-frame PNG export from `RenderTarget`** (`RenderTarget.cpp`, `FrameGrabber.cpp`) #feature
- [ ] T0127 **Drive frame pacing from captured hardware timestamps** (`src/recorder/`) #risk-high
- [ ] T0128 **Library sort, filter, multi-select & bulk actions** (`LibraryView.qml`) #feature
- [ ] T0320 **Add like/dislike UI affordances & wire remaining batch mutations** (`LibraryView.qml`, `ListenView.qml`, `SunoLibraryMutations.*`) #feature #blocks-ui
- [?] T0321 **Add queue & mute controls to global `TransportBar`** (`TransportBar.qml`, `main.qml`) #feature #blocks-ui
- [ ] T0129 **Surface ReplayGain metadata honestly in UI** (`LoudnessAnalysis.cpp`) #feature #retention
- [ ] T0188 **Standardize 44×44 touch targets, hover states & mobile breakpoints** (`AppButton.qml`, `src/qml/`) #feature
- [ ] T0299 **Package Linux `.deb`/`.rpm`/AppImage/Flatpak targets** (`Install.cmake:43`) #feature
- [ ] T0311 **Publish CI build artifacts on branch pushes (policy decision)** (`build.yml:42-46`, `release.yml:78-80`) #feature #blocks-release
- [ ] T0312 **Extract shared QML UI helpers into single-concern `.js` libraries** (`src/qml/js/`, `Theme.qml`, `main.qml`) #feature #cod-high
- [ ] T0316 **Time-scripted music video stagecraft scene designer** (`RenderJob.hpp`, `OverlayPanel.qml`, `LyricsExport.cpp`, `SunoDownloader.cpp`) #feature #cod-high
- [ ] T0345 **Enforce strict host/port/path OAuth redirect validator** (`OAUTH_REDIRECT_ANALYSIS.md`) [P] #cleanup
- [ ] T0385 **Reorganize `src/` into self-explanatory domain subdirectories** (`src/`) #cleanup

---

## Capture-Gated & Recon Backlog (Do Not Wire Without `[T1]` Evidence)

### Security & Governance
- [ ] T0146 **Validate new Suno CDN/subdomain leads before adding to `CapturedHosts`** (`CapturedHosts.cpp`) #security
  - Leads: `content.suno.run`, `cdn3.suno.ai`, `suno.sng.link`, CloudFront, `audiopipe-dev`, `platform.suno.com`. Keep `/b-side/*`, `/labs/*`, and money/consent routes excluded.
- [ ] T0147 **Keep excluded stubs (`ORCHESTRATOR_CHAT`, `ORCHESTRATOR_HISTORY`, `MODAL_BASE`) fail-closed** (`src/suno/`) #security
- [ ] T0148 **Sanitize HAR captures at ingest (strip tokens/cookies/IDs)** (`docs/suno_api/raw/README.md`) #security
- [ ] T0174 **Document missing 2026-09-24 raw corpus while preserving `[T1]` grades** (`docs/suno_api/raw/`) #risk-high
- [ ] T0175 **Introduce `[REGISTERED]` grade for 401-verified routes without body captures** (`docs/suno_api/`) #risk-medium
- [ ] T0176 **Audit/scrub plaintext JWTs & cookies in `~/Documents/suno-recon-active-investigation/`** (`~`) #security
- [ ] T0177 **Re-probe 9 drifted `[T1]` route spellings before demoting** (`ENDPOINT-INVENTORY.md`) #risk-medium
- [ ] T0384 **Capture authed `/api/feature-flags` via app-originated request** (`docs/suno_api/`) #cleanup

### Endpoint Families
- [ ] T0141 **Implement `touch`-first Clerk refresh with 401 fallback to `tokens`** (`ENDPOINT-INVENTORY.md`) #blocks-capture
- [ ] T0142 **Centralize HTTP error & `Retry-After` rate-limit envelope parsing** (`src/suno/`) #risk-high
- [ ] T0143 **Capture remaining mutation response shapes (`gen/trash`, cover image IDs)** (`SunoLibraryMutations.cpp`) #blocks-capture #data-loss
- [ ] T0144 **Capture fresh notification v3 mutation payloads** (`SunoNotificationService.cpp`) #blocks-capture
- [ ] T0145 **Capture live OAuth loopback acceptance/rejection** (`OAUTH_REDIRECT_ANALYSIS.md`) #blocks-capture
- [ ] T0126 **Wire captured `POST /gen/{id}/downbeats_streaming/v2` for beat-synced cuts** (`SunoEndpoints.hpp:60`) #feature #blocks-capture
- [ ] T0315 **Mine local recon corpus for `configs.gen-endpoint` & Turnstile mint flow** (`ENDPOINT-INVENTORY.md`, `OBSERVED-LEADS.md`, `SunoEndpoints.hpp`) #feature #blocks-capture
- [ ] T0368 **Capture & wire playbar `/queue/{inject,start}` + local queue view** (`src/suno/`, `src/qml/`) #feature (after: T0321)
- [ ] T0369 **Record `/visual/*` 403 gate names; keep fail-closed** (`docs/suno_api/OBSERVED-LEADS.md`) #feature
- [ ] T0370 **Inspect local `suno-staff-statsig-overrides` to enumerate gated flags** (`docs/dev/`, `docs/suno_api/`) #feature
- [ ] T0371 **Capture scratch `/custom-model/{create,edit,archive,bases,pending}`; wire reads only** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0372 **Capture & wire clip-edit/remix routes (`adjust-speed`, `reverse-clip`, `concat/v2`, `upsample`, `lyrics-mashup`)** (`src/suno/SunoEndpoints.hpp`, `SunoOrchestrator.*`, `docs/suno_api/`) #feature
- [ ] T0373 **Capture `/preferences/clip-review/{pending,opt-out}` (leave `submit` captcha-gated)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0374 **Capture read-only social/profile endpoints (`follow`, `block`, `mutual-followers`, `share`)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0376 **Capture scratch `/studio/{save-project,render-state*}`; wire reads first** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0377 **Capture `/v2/integrations/musixmatch` connected payload** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature (after: T0356)
- [ ] T0378 **Capture `/v2/integrations` payload with attached MCP server** (`docs/suno_api/MCP.md`) #feature (after: T0356, T0377)
- [ ] T0379 **Record status-only census for `auth.suno.com` & `suno.com/api/{docs,graphql,health,trpc}`** (`docs/suno_api/ENDPOINT-INVENTORY.md`) #cleanup
- [ ] T0380 **Capture `PUT /persona/edit-persona/{id}/` visibility toggle using active persona track** (`docs/suno_api/`, `SunoPersonaService.*`) #feature
- [ ] T0381 **Capture `POST /project/{id}/clips` `update_type:"remove"` payload** (`SunoLibraryMutations.*`, `docs/suno_api/`) #feature
- [ ] T0382 **Capture `GET /uploads/audio/{id}/` & full `processed_clip.status` enum** (`docs/suno_api/`) #cleanup
- [?] T0383 **Verify live 200 responses for shipped Hooks/History/Albums/Trash reads** (`SunoLibrarySectionService.*`, `SunoHooksService.*`, `SunoAlbumsService.*`) #feature #blocks-ui

---

## Completed / Closed Index (Archived Details in `docs/dev/TODO-archive.md` & `STATUS/<id>.log`)

- [x] T0011 11-route mutation surface enabled & trash verbs wired (`SunoLibraryMutations.*`, 2026-11-10)
- [x] T0049 Connected `QQmlApplicationEngine::objectCreationFailed` (`Application.cpp`, 2026-11-08)
- [x] T0058 Keyed `RatingManager` on path + added mutex (`RatingManager.cpp`, 2026-11-06)
- [-] T0065 Dropped cleanup; `SunoOrchestrator` is intentional fail-closed gate (2026-11-08)
- [x] T0066 Removed dead `searchText` plumbing (`SunoLibraryManager.cpp`, 2026-11-07)
- [x] T0072 Added `reuseItems: true` to 6 virtualized QML views (2026-11-08)
- [x] T0076 Added empty states to `PresetsPanel` & `PlaylistPanel` (2026-11-08)
- [-] T0080 Closed; `AppSlider` sites already use `onMoved` (2026-11-08)
- [x] T0081 Added keyboard nav, `Accessible.Slider` & div-by-zero guard to `AppSlider` (2026-11-07)
- [x] T0082 Expanded `Accessible.*` to 150 lines across 31 QML files (2026-11-08)
- [-] T0085 Dropped phantom `TextAccessible`; tracked under T0117 (2026-11-12)
- [x] T0086 Added `sourceSize` + `asynchronous` to remote art `Image`s (2026-11-07)
- [x] T0087 Pruned 7 unused SVGs from `TargetSetup.cmake` resources (2026-11-08)
- [x] T0088 Bundled `ChadVis.icns` into macOS `Info.plist` (2026-11-07)
- [x] T0089 Vendored & SHA-256-pinned `cmake/CPM.cmake` v0.40.0 out of source tree (2026-11-11)
- [x] T0090 Closed; all CPM deps already commit-SHA pinned (2026-11-11)
- [x] T0099 Consolidated duplicate reply fakes into `tests/support/FakeNetworkReply.hpp` (2026-11-08)
- [x] T0102 Freed `hwFrame_`/`hwFramesCtx_`/`hwDeviceCtx_` in `cleanup()`; refuted `read_packet` (2026-11-12)
- [x] T0104 Closed; `swr_alloc_set_opts2` & `av_packet_alloc` already migrated (2026-11-11)
- [x] T0105 Documented `av_hwdevice_ctx_create` guard comment (2026-11-11)
- [x] T0106 Implemented EBU R128 momentary/short-term peak-hold loudness (2026-11-11)
- [x] T0107 Refuted; `AV_VERSION_INT` is FFmpeg's macro used in compile-time `#if` (2026-11-12)
- [x] T0109 Scrubbed clip IDs & paths from logs (`DownloadQueue.cpp`, `SunoDownloader.cpp`, 2026-11-07)
- [-] T0113 Closed; `build.log` & `.DS_Store` already in `.gitignore` (2026-11-08)
- [-] T0114 Closed; `libavfilter` already optional behind `CHADVIS_HAS_AVFILTER` (2026-11-08)
- [x] T0124 Unified `kEncoderPresetSlugs` table & added `placebo` to `RecordingSettings.qml` (2026-11-12)
- [x] T0130 Implemented allowlist-only `--diagnostics` report (`Diagnostics.*`, 2026-11-12)
- [-] T0137 Closed; `PresetScanner` already derives category from subdirectory (2026-11-08)
- [x] T0165 Added case-insensitive `.milk`/`.milk7`/`.milkdrop` discovery (2026-11-07)
- [x] T0171 Fixed frame-0 PBO readback emission in `VisualizerRenderer` (2026-11-08)
- [x] T0180 Fixed `settingsWindowOpensOnNavigate` QML incubation drain in `test_main.cpp` (2026-11-08)
- [x] T0193 Implemented C++ `ThemeCatalog` JSON discovery & WCAG validation (2026-11-08)
- [x] T0197 Re-applied theme on `SettingsBridge.themeNameChanged` in `main.qml` (2026-11-08)
- [x] T0326 Replaced resizable `scratchBuffer_` with fixed buffer + atomic `scratchWindow_` (2026-11-11)
- [x] T0328 Converted `AudioEngine` cross-thread posts to parameterless queued slots (2026-11-10)
- [x] T0329 Audited 19 `invokeMethod` sites & added `check-thread-handoffs.py` guard (2026-11-08)
- [x] T0330 Unified `scripts/safe-tests.txt` & added `check-safe-tests.py` guard (2026-11-07)
- [x] T0331 Classified sanitizer exit codes 66/67 in `build.sh` (2026-11-07)
- [x] T0335 Migrated notification list to `/notification/v3` while keeping v2 badge/mutations (2026-11-08)
- [x] T0336 Adopted `https://suno.com/a/api` in test fixtures & kept OAuth issuer separate (2026-11-08)
- [x] T0339 Classified pasted credentials via `classifyStoredCredential` (2026-11-07)
- [x] T0340 Bound minted bearer `sid` claim to active session in `ClerkAuthClient` (2026-11-06)
- [x] T0341 Mapped Clerk `errors[0].code` & surfaced 429 `Retry-After` (2026-11-08)
- [x] T0342 Allowed single-session fallback when `last_active_session_id` absent (2026-11-06)
- [x] T0343 Implemented `GET /api/user/tos_acceptance` check & reprompt flag (2026-11-08)
- [x] T0344 Parsed `cookie_expires_at`/`abandon_at`/`expire_at` for 30-day re-import advisory (2026-11-08)
- [x] T0348 Implemented reaction/metadata/playlist mutation verbs (`SunoLibraryMutations.*`, 2026-11-06)
- [x] T0362 Closed umbrella epic after splitting into T0430–T0435 (2026-11-08)
- [x] T0363 Modelled `AudioDescriptionConsent` & `is_audio_upload_tos_accepted` (2026-11-06)
- [x] T0364 Fixed `spdlog`/`fmt` include ordering in `TargetSetup.cmake` (2026-11-06)
- [x] T0366 Wired remote search (`feed/v3` `searchText`, `suggest`, `omnisearch`) (2026-11-08)
- [x] T0367 Wired `GET /onboarding/current` read in `SunoAccountManager` (2026-11-08)
- [x] T0375 Implemented `SunoNotificationService::clearBadge()` (2026-11-08)
- [x] T0386 Scoped ASan/UBSan CI lanes to `scripts/safe-tests.txt` (2026-11-10)
- [x] T0387 Added 8 Suno service tests + `QT_QPA_PLATFORM=offscreen` to CI gates (2026-11-10)
- [x] T0389 Pinned `PresetPack.cmake` & `CPM.cmake` downloads by SHA-256 (2026-11-11)
- [x] T0390 Consolidated remaining `FakeReply` duplicates into `FakeNetworkReply.hpp` (2026-11-10)
- [x] T0392 Added `SunoDatabase` upsert, rollback & migration tests (2026-11-09)
- [-] T0393 Dropped; RFC 8414 OAuth issuer tree is separate from `API_BASE` (2026-11-10)
- [x] T0394 Added `scripts/check-endpoints.py` dead-constant baseline guard (2026-11-10)
- [x] T0398 Replaced raw `new`/`delete` in `SunoAudioUploadService` with RAII `unique_ptr` (2026-11-10)
- [x] T0399 Added `DEBUG` logging in `SunoDownloader` tag-read catch block (2026-11-09)
- [x] T0400 Moved `OffscreenRenderSpike` out of production library into test-only target (2026-11-09)
- [x] T0401 Guarded `CredentialStoreWorker` enqueue with `shuttingDown_` flag (2026-11-08)
- [x] T0403 Modernized 6 `snprintf` sites to `std::format` (2026-11-08)
- [x] T0404 Verified & documented AppleClang 16 / libc++ `JThread` fallback necessity (2026-11-10)
- [x] T0421–T0429 Completed backlog hygiene & doc indexing batch (2026-11-08)
- [x] T0430 Wired `initialize-clip` leg & rights-clearance opt-in in `SunoAudioUploadService` (2026-11-08)
- [x] T0431 Implemented `SunoStemService` vocal stem submit & poll (2026-11-08)
- [x] T0432 Implemented `SunoPersonaService::createPersona` with 10-key body (2026-11-08)
- [x] T0433 Implemented `SunoFreeReadsService` for 5 read-only endpoints (2026-11-08)
- [x] T0434 Fixed nested `cover_source` payload in playlist cover-image mutation (2026-11-08)
- [x] T0435 Implemented `SunoVoiceVerificationService` with strong `UploadId`/`VerificationId` types (2026-11-08)
- [x] T0436 Documented `AppSlider.stepSize = 1` divergence (2026-11-08)
- [x] T0437 Defaulted `AppSlider.accessibleName` to `""` (2026-11-08)
- [x] T0438 Replaced `Q_ASSERT` in tests with `qFatal` + `check-test-asserts.py` guard (2026-11-08)
- [x] T0439 Wired `PresetsPanel` search, category & blacklist filters to `PresetBridge` (2026-11-08)
- [x] T0440 Added `PresetBridge.scanning` property to gate empty state during scan (2026-11-08)
- [x] T0441 Added keyboard-only 2px focus rings to 7 custom QML controls (2026-11-08)
- [x] T0442 Added `Accessible.name` to 5 icon-only buttons in `PresetsPanel`/`PlaylistPanel` (2026-11-08)
- [x] T0443 Drained pending tail PBO frame in `VisualizerRenderer::stopRecording()` (2026-11-08)
- [x] T0444 Added `scripts/check-integration-fresh.py` staleness guard (2026-11-08)
- [x] T0450 Surfaced `SunoBridge.settleMessage` in `NotificationsView` (2026-11-08)
- [~] T0451 Added `Application::drainQmlIncubation` in `aboutToQuit` (pending `--asan` confirmation, 2026-11-09)
- [x] T0453 Split monolithic `project_lib` into 6 module `STATIC` libs + `INTERFACE` aggregator (2026-11-08)
- [x] T0456 Isolated `CHADVIS_VERSION`/feature defines into generated `BuildConfig.hpp` (2026-11-08)
- [-] T0459 Dropped `src/` hash from CI ccache key (2026-11-08)
- [x] T0460 Added `version.txt` to `CMAKE_CONFIGURE_DEPENDS` (2026-11-11)
