---
schema: 1
updated: 2026-11-19
---

# ChadVis Active Backlog

Active items only (`[ ]`, `[~]`, `[?]`, `[!]`). Completed `[x]` and dropped `[-]` items live in [`docs/dev/TODO-archive.md`](docs/dev/TODO-archive.md).
Rules: [`AGENTS.md`](AGENTS.md) | Leases: [`scripts/task.sh`](scripts/task.sh) | Endpoints: [`docs/suno_api/ENDPOINT-INVENTORY.md`](docs/suno_api/ENDPOINT-INVENTORY.md).

## Grammar & Marks
`- [<mark>] <ID> <title> (<write-set>) [P] #tags (after: <IDs>)`
Marks: `[ ]` todo | `[~]` claimed | `[?]` unverified | `[x]` verified (`STATUS/<id>.log` required) | `[!]` blocked | `[-]` dropped.

---

## Features - New Functionality/Ideas To Implement
- [ ] TO??? (Assign next integer) **Song Relationship:** fins the history of a sonfs lineage—origioal, covera, mashups, and inspirations, optionally with tracks that share a user configurable similarity score but is not directly linked. A new page view canvas in the UI allows seeing all songs linked lime a tree/map of song relationships with the ability to click on a track node and zoom into a filteres tree/node of just that song and its ancestors/children.

---

## P0 — Product Broken, Data Corrupted, or Secret Exposed

- [ ] T0346 **Bring every non-Studio Suno capability into product** (`src/suno/`, `src/qml_bridge/`) #feature #user-priority
      Sub-tasks tracked in [`docs/backlog/T0346.md`](docs/backlog/T0346.md); UI surfaces in [`docs/suno_api/UI-SURFACE-INVENTORY.md`](docs/suno_api/UI-SURFACE-INVENTORY.md).
      Enrichment reads, generation submit+poll, trash restore, T0383 reads, and T0430–T0435 chains landed. Remaining: UI wiring + remaining capture-gated families.

- [?] T0334 **First library sync: ~3.4s/page, 6 min spinner for 2k clips** (`SunoLibraryManager.cpp`) #risk-high #perf
      Page size 100, mid-sync failure resume (`failedPageIsRetriedNotRestarted`), and `pagesFetched` progress shipped 2026-11-08. Remaining: real 2k-clip wall-clock measurement.

---

## P1 — Correctness, Robustness, Blocking Debt

- [?] T0356 **Suno OAuth 2.0 loopback flow — in-app sign-in** (`src/suno/auth/`, `docs/suno_api/MCP.md`) #feature #blocks-ui
      `SunoOAuthLoopback` (PKCE S256 → `127.0.0.1:0/callback` → `suno/mcp` credential key) implemented and unit-tested (`test_SunoOAuthLoopback`). Remaining: live browser run + verifying whether MCP token speaks `studio-api`.

- [ ] T0357 **Suno MCP Beta: chat surface in ChadVis** (`src/suno/`, `docs/suno_api/MCP.md`) #feature (after: T0356)
      `SunoMcpClient` + unit tests landed. Remaining: bridge wiring, QML chat surface, and live handshake using `suno/mcp` bearer.

- [~] T0358 **Reactive GUI Phase 0 + 1: motion tokens, reduced motion, juicy primitives** (`src/qml/styles/Theme.qml`, `src/qml/components/`, `src/qml_bridge/SettingsBridge*`) #feature #blocks-ui
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §3–§4. Add `durationMicro`/`durationPop`/`motionScale`, `SettingsBridge.reduceMotion`, `Wiggle.qml`, `GlowFrame.qml`, `RefreshSpinner.qml`. Needs desktop `./build.sh --fast --tests` + real window verification.

- [~] T0359 **Reactive GUI Phase 2: GUI for wired Suno mutations + generation + notifications** (`src/qml/components/`, `src/qml/views/`) #feature #blocks-ui (after: T0358)
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §6.1–§6.4. Part 1 (`ClipMutationsSection.qml`, `ClipActionMenu.qml`) landed. Part 2 remaining: `Toast.qml`, `GenerationStatus.qml` (browser handoff on Turnstile), `NavRail` badge, `LibraryView` bulk/trash affordances, and C++ index for `reorderPlaylistTracks`.

- [?] T0349 **Persona: reads and create wired; UI and live verification open** (`SunoPersonaService.*`, `src/qml/panels/settings/`) #feature
      Reads (`T0349`) and `createPersona` (`T0432`) landed in C++. Remaining: UI behind `Personas` gate + one live scratch `POST /persona/create/` to record response keys.

- [?] T0365 **Listen for Credits: read side wired; queue view + live GET open** (`src/suno/SunoPreferencesService.*`, `src/qml/panels/settings/`) #feature #user-priority
      `SunoPreferencesService` reads `summary`, `tasks`, `rewards`, `stats`, `profile`, `leaderboard`. Remaining: queue UI, `POST /preferences/tasks/release-buffer` on abandon, and browser handoff for captcha-gated `submit`.

- [ ] T0170 **Overlays/karaoke cannot appear in recorded video** (`VisualizerWindow.cpp`, `src/recorder/`) #risk-high #blocks-feature
      Native GL capture misses sibling QML scene graph. Requires FFmpeg post-pass (`T0108`).

- [ ] T0173 **projectM broken-preset retry: premise needs GL evidence** (`Bridge.cpp:48`) #risk-medium
      `failed` callback slot is untouched; do not alter until verified with a real GL window + corrupt `.milk` file.

- [?] T0199 **"Sign in with Google" browser cookie harvest** (`src/suno/auth/`) #blocks-ui #risk-high #cod-high
      WAL-safe SQLite copy + macOS Chromium cookie decryption complete and green. Remaining: human live button verification (`MT-001-D2`).

- [?] T0344 **Credential expiry advisory (`SunoClient` + QML property)** (`ClerkAuthClient.*`, `SunoClient.*`, `SunoSettings.qml`) #blocks-ui
      `ClerkAuthClient::reimportAdvisory()` implemented. Remaining: 1-line `SunoClient` accessor + `Q_PROPERTY` for `SunoSettings.qml` advisory banner.

---

## P2 — Robustness, Cleanup, Debt

### Threading, Recorder & Core C++
- [!] T0327 **Credential-store handoff TSan false positive** (`SunoClient.cpp:267-335`, `cmake/tsan.supp`) #risk-high
      Benign TSan report inside `libQt6Core` `QCallableObject`. Blocked on one `--tsan` lane capture over `test_SunoRequestEpoch` + `test_BoundedBody` before adding suppression and updating `INVARIANTS.md`.
- [ ] T0156 **Encoder teardown blocks GUI thread** (`VideoRecorderCore.cpp:168`, `RecordingBridge.cpp:323`, `RenderExecutor.cpp:802-807`) #risk-high
      Move `flush()` + `cleanup()` off GUI thread to worker; post completion signal.
- [ ] T0157 **GUI-thread reentrancy: render runner joins own worker mid-pump** (`RenderExecutor.cpp:516-518`) #risk-high
      Post `runner_()` to worker thread instead of running synchronously inside `enqueue` → `pump()`.
- [ ] T0158 **`RenderQueue::settle` runs receipt write on caller thread** (`RenderQueue.cpp:515`, `RenderJob.cpp:252`) #risk-medium
      Queue the 3 receipt-write syscalls off the GUI thread.
- [ ] T0159 **`retireSlot` holds `JobSlot&` across call that re-enters `jobRunner`** (`RenderExecutor.cpp:844-854`) #risk-medium
      Erase slot before reporting failure to prevent `slots_` reallocation UAF.
- [ ] T0418 **Bound recorder finalize with a timeout + QML progress** (`VideoRecorderCore.cpp`, `RenderExecutor`, `RecordingPanel.qml`) #risk-high (after: T0156, T0157)
- [ ] T0318 **Saved stream carries no lyrics, no user-settable tags** (`SunoDownloader.cpp:832-900,1085`) #feature #risk-medium
      Write `©lyr` from `clip.metadata.lyrics` or DB `aligned_lyrics_json`; populate sidecar.
- [ ] T0465 **Consolidate the 5 duplicated `readObject` helpers into `SunoJson`** (`src/suno/Suno*Service.cpp`, `SunoJson.hpp/.cpp`) #cleanup
      Extract shared body reader while preserving `SunoHooksService` array-to-`{"hooks": ...}` wrapping.
- [ ] T0391 **Shared JSON object-parse helper across remaining ~83 `fromJson` sites** (`src/suno/`) #risk-medium
      Unchecked sites fixed; remaining work is extracting the shared helper + unifying `opt*` scalar parsers.
- [ ] T0395 **Split `VideoRecorderFFmpeg` god TU (1734 lines)** (`src/recorder/VideoRecorderFFmpeg.*`) #cleanup
      Extract HW-probe and subtitle-encoder into separate TUs; target < 600 lines.
- [ ] T0396 **Split `SunoClient` credential worker + queue (1660 lines)** (`src/suno/SunoClient.*`) #cleanup
- [ ] T0397 **De-globalize `Config` behind an injected interface** (`src/core/Config.*`, `Application.cpp`, `AudioEngine.cpp`) #cleanup
- [ ] T0402 **Construction-time injection for audio/render tuning constants (part b)** (`src/audio/`, `src/recorder/`) #cleanup (after: T0397)
- [ ] T0405 **Decompose `Application` coordinator (846 lines)** (`src/core/Application.*`, `CliUtils.cpp`) #cleanup (after: T0397)
- [ ] T0108 **Muxed subtitle tracks: `libavfilter` + `libass` behind `CHADVIS_HAS_AVFILTER`** (`Dependencies.cmake`, `SubtitleBurnIn.cpp`) #feature
- [ ] T0115 **Song/preset import must work with no Suno account** (`docs/PIVOT_PLAN.md`) #feature
- [ ] T0116 **Render-job model and queue exists, no producer** (`Bridge.hpp:59-68`, `SunoWorkspace.hpp`) #feature

### QML, Bridges & UI Hygiene
- [?] T0355 **Song-page detail: ancestry, remix count, comments, similar** (`SunoClipDetailService.*`, `SunoBridge.*`, `ClipDetailSheet.qml`) #feature
      4 GETs wired, 8 unit tests passing; needs live body verification.
- [?] T0351 **Generation submit+poll wired; browser Turnstile handoff polish** (`SunoGenerationService.*`, `CreateView.qml`) #feature #blocks-ui
- [~] T0451 **Drain QML incubation in `aboutToQuit`** (`src/core/Application.cpp`) #cleanup
      `drainQmlIncubation` wired; needs platform run where `incubatingObjectCount() > 0` at teardown.
- [ ] T0452 **Track Qt 6.12 `qmlcachegen` SIGSEGV on `QVariantMap` member access** (`docs/dev/`, `TODO.md`) #risk-medium
- [ ] T0353 **App hung after clicking around; no repro** (`src/qml/`) #risk-high
      Run with `QT_LOGGING_RULES="qt.qml.binding.removal.info=true"` on real window.
- [ ] T0051 **Enable `QTP0004` and `qt_add_qml_module` across 58 QML files** (`TargetSetup.cmake:129-134`) #risk-medium
- [ ] T0083 **Fix contrast failures over projectM canvas (`glassBackground` scrim)** (`Theme.qml`) #cleanup
- [ ] T0084 **Replace ~58 raw `QtQuick.Controls` with `App*` wrappers** (`src/qml/`) #cleanup
- [ ] T0406 **Deduplicate file-path helpers (`localFileUrl`/`toLocalPath`) into one module** (`src/qml/`) #cleanup
- [ ] T0407 **Extract shared `StateView` (loading/empty/error/retry)** (`src/qml/components/StateView.qml`, views/panels) #cleanup
- [ ] T0408 **Extract `SettingsPageShell`; move `AccountPage` gate-model logic to C++** (`src/qml/settings/`, `SunoBridge`) #cleanup
- [ ] T0409 **LyricsPanel: `App*` wrappers, accessible search, export errors, loading state** (`src/qml/panels/LyricsPanel.qml`, `LyricsBridge`) #cleanup
- [ ] T0410 **Persist karaoke glow knob and bind `VideoView` to it** (`KaraokeSettings.qml`, `VideoView.qml`, `SettingsBridge`) #cleanup
- [ ] T0411 **Shortcuts: ship editor or label panel read-only** (`ShortcutsSettings.qml`, `SettingsBridge`) #cleanup
- [ ] T0412 **Move color/time math from `Theme.qml` JS to a C++ `ColorUtil` singleton** (`Theme.qml`, `AppColorPicker.qml`) #cleanup
- [ ] T0413 **Single save-state model for `ClipCard`/`ClipDetailSheet`/`LibraryView`** (`src/qml/components/`, `LibraryView.qml`) #cleanup
- [ ] T0414 **OverlayPanel: themed palette + bounds-checked selection** (`OverlayPanel.qml`, `OverlayBridge`) #cleanup
- [ ] T0415 **Replace raw sizes/fonts with `Theme` tokens** (`src/qml/`) #cleanup (after: T0084)
- [ ] T0416 **PresetsPanel: preserve scroll on model change, drop per-star `MultiEffect`** (`PresetsPanel.qml`, `PresetBridge`) #perf
- [ ] T0417 **Audit QML bridge surface: consolidate visualizer control, prune dead `Q_INVOKABLE`s** (`src/qml_bridge/`) #cleanup
- [ ] T0419 **Audio settings: apply live or mark restart-required in-place** (`AudioSettings.qml`, `SettingsBridge`, `AudioEngine`) #cleanup (after: T0131)
- [ ] T0420 **Move remaining QML data-shaping (`"__favorites__"` sentinels, detail fields) to C++ bridges** (`src/qml/`, `src/qml_bridge/`) #cleanup (after: T0412)

### Build, CI & Test Infrastructure
- [ ] T0093 **Add non-blocking `git clang-format --diff main` report step** (`Compiler.cmake`, `.github/`) #risk-medium
- [ ] T0388 **Warnings-as-errors + clang-format/tidy CI step** (`cmake/Compiler.cmake`, `.github/workflows/guards.yml`, `.clang-format`, `.clang-tidy`) #risk-medium
- [ ] T0338 **Pin Windows vcpkg ports via `vcpkg.json` manifest** (`windows.yml:130-160`) #risk-medium
- [ ] T0097 **Isolate `unit_tests` suites so a crash/hang in suite 30 doesn't drop 1–29** (`test_main.cpp`) #risk-medium
- [ ] T0100 **Add QML/UI bridge test coverage** (`tests/`) #risk-high #data-loss
      C++ write-path (`test_SunoLibraryMutations`) and `SunoDatabase` are now covered; QML UI harness (`T0135`) remains open.
- [?] T0454 **Verify PCH + ccache hit rate (< 5% uncacheable) on cold `--tests` build** (`build.sh`, `src/pch.hpp`) #perf
- [ ] T0455 **Reduce top-5 header fan-out (< 100 TUs per header)** (`Types.hpp`, `Result.hpp`, `SunoClient.hpp`, `SunoEndpoints.hpp`) #perf
- [ ] T0457 **Configure remote ccache/sccache storage for 2013 MBA** (`build.sh`, CI) #perf
- [ ] T0458 **Enable `CMAKE_UNITY_BUILD` (batch 8) in fast loop** (`TargetSetup.cmake`, `CMakeLists.txt`) #perf (after: T0454)

---

## P2b — Suno Endpoint Coverage (urls.txt & UI-07 Follow-Ups)

- [?] T0383 **Live 200 verification for Hooks/History/Albums/Trashed reads** (`SunoLibrarySectionService`, `SunoHooksService`, `SunoAlbumsService`) #feature #blocks-ui
      Mutations and C++ read services landed; needs live 200 verification.
- [ ] T0368 **Queue: `/queue/inject`, `/queue/start` + local queue view** (`src/suno/`, `src/qml/`) #feature (after: T0321)
- [ ] T0369 **Visuals product family (`/visual/*`) — record 403 statuses + gate names; fail closed** (`docs/suno_api/OBSERVED-LEADS.md`) #feature
- [ ] T0370 **Statsig staff-flag override recon (`suno-staff-statsig-overrides`)** (`docs/dev/`, `docs/suno_api/`) #feature
- [ ] T0371 **Custom-model training family (`/custom-model/{create,edit,archive,bases,pending}`)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0372 **Clip edit family (`/clips/{adjust-speed,reverse-clip,aligned_clips}`, `/generate/{concat/v2,sum,rhymes,upsample,lyrics-mashup}`)** (`src/suno/SunoEndpoints.hpp`, `SunoOrchestrator.*`) #feature
- [ ] T0373 **Listen-for-Credits clip-review sub-family (`/preferences/clip-review/{pending,opt-out}`)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0374 **Social actions + share attribution (`/profiles/*`, `/comment/*`, `/share/*`)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0376 **Studio multitrack save/render (`/studio/{save-project,render-state*}`)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
- [ ] T0377 **Integrations: musixmatch connector (`/v2/integrations/musixmatch*`)** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature (after: T0356)
- [ ] T0378 **`/v2/integrations` capture with active MCP link** (`docs/suno_api/MCP.md`) #feature (after: T0356, T0377)
- [ ] T0379 **Auth/platform endpoint census** (`docs/suno_api/ENDPOINT-INVENTORY.md`) #cleanup
- [ ] T0380 **Persona visibility write (`PUT /persona/edit-persona/{id}/` `is_public`)** (`docs/suno_api/`, `SunoPersonaService.*`) #feature
- [ ] T0381 **`POST /project/{id}/clips` `update_type:"remove"` capture & wire** (`SunoLibraryMutations.*`, `docs/suno_api/`) #feature
- [ ] T0382 **Uploads status route (`GET /uploads/audio/{id}/`) + `processed_clip` status vocabulary** (`docs/suno_api/`) #cleanup
- [ ] T0384 **Feature-flags authed shape (`GET /api/feature-flags`)** (`docs/suno_api/`) #cleanup

---

## P3 — Polish & Speculative

- [ ] T0360 **Reactive GUI Phase 3: audio-reactive chrome (~30 Hz smoothed `audioLevel`/`beatPulse`)** (`src/qml_bridge/AudioBridge.*`, `src/qml/`) #feature (after: T0359)
- [ ] T0361 **Reactive GUI Phase 4: view polish, one-shot particles, hero moments, `MT-001` checklist** (`src/qml/`, `docs/dev/manual-qa.md`) #feature (after: T0360)
- [ ] T0345 **Record web client's redirect validator (`isSafeRedirectUrl`)** (`OAUTH_REDIRECT_ANALYSIS.md`) [P] #cleanup
- [ ] T0117 **Localization: `qsTr` across 58 QML files + `qt_add_translations`** (`src/qml/`, `CMakeLists.txt`) #cod-high #feature
- [ ] T0118 **Dark/light theme pair bound to `Application.styleHints.colorScheme`** (`Theme.qml`) #feature
- [ ] T0119 **Raise `textDisabled`/`recording`/`textSecondary`/`error` contrast to >= 4.5:1** (`Theme.qml`) #feature
- [ ] T0120 **Waveform lyric-timing correction editor** (`src/qml/`) #feature
- [ ] T0121 **Karaoke render strategies as swappable producers (wipe/bounce/scroll/dual-line)** (`src/qml/`) #feature
- [ ] T0122 **Versioned render receipt sidecar shipped with job model** (`RenderJob.hpp:129`) #feature
- [ ] T0123 **6-second quarter-res proof render before full encode** (`EncoderSettings.cpp`) #feature
- [ ] T0125 **Poster-frame export from framebuffer 0** (`RenderTarget.cpp`, `FrameGrabber.cpp`) #feature
- [ ] T0126 **Cut on server downbeats (`POST /api/gen/{id}/downbeats_streaming/v2`)** (`SunoEndpoints.hpp:60`) #feature #blocks-capture
- [ ] T0127 **Cut on real frame pacing from timestamps already in hand** (`src/recorder/`) #risk-high
- [ ] T0128 **Library sort, filter, multi-select, bulk context menu** (`LibraryView.qml`) #feature
- [ ] T0320 **Like/dislike thumb control in Library/Listen views** (`LibraryView.qml`, `ListenView.qml`, `SunoLibraryMutations.*`) #feature #blocks-ui
- [?] T0321 **Global docked `TransportBar`: add queue affordance + mute control** (`TransportBar.qml`, `main.qml`) #feature #blocks-ui
- [ ] T0129 **ReplayGain presented honestly** (`LoudnessAnalysis.cpp`) #feature #retention
- [ ] T0131 **DI over singletons; separate audio from UI** (`Config.hpp`, `AudioEngine.cpp`) #blocks-feature
- [ ] T0132 **Consolidate QML bridge singletons to one namespaced backend** (`src/qml_bridge/`) #cleanup
- [ ] T0133 **Replace deprecated `qt_finalize_executable` with `qt_finalize_target`; add `NSDocumentsFolderUsageDescription`** (`Install.cmake`, `TargetSetup.cmake`) #cleanup
- [ ] T0134 **Remove `macdeployqt` hard requirement from default macOS configure** (`TargetSetup.cmake:397-410`) #risk-medium
- [ ] T0135 **`qt_add_qml_module` + `QTP0004` + `qmltestrunner` harness** (`tests/`) #risk-high
- [ ] T0136 **GPU CI lane or honest "no GL coverage" badge** (`.github/workflows/`) #risk-high
- [ ] T0138 **Modernize C++ idioms (`std::mdspan`, concepts, `std::array`, `std::variant`)** (`src/`) #cleanup
- [ ] T0139 **Wire or delete unbound shortcuts in `ShortcutsSettings.qml` vs `main.qml`** (`main.qml`) #feature #risk-high
- [ ] T0188 **Interaction language: hover-grow, 44x44 tap targets, responsive breakpoints** (`AppButton.qml`, `src/qml/`) #feature
- [ ] T0299 **Linux packaging (.deb/.rpm/AppImage/Flatpak)** (`Install.cmake:43`) #feature
- [ ] T0300 **CI code coverage measurement** (`.github/workflows/`, `CMakeLists.txt`) #risk-medium
- [ ] T0301 **Static-analysis, SBOM, and dependency-vulnerability CI lane** (`.github/workflows/`) #security
- [ ] T0305 **Consolidate 4 overlapping C++ skills in `.opencode/skills/`** (`.opencode/skills/`) #cleanup
- [ ] T0309 **Browser-cookie import DPAPI (Windows) / libsecret (Linux) or platform gate** (`BrowserCookieImport.cpp:223-260`) #risk-medium
- [?] T0322 **GL observation that canvas reacts to music (`MT-001-D4.4`)** (`VisualizerRenderer.cpp`, `VisualizerAudioTap.cpp`) #blocks-ui #risk-high
- [ ] T0311 **Publish CI build artifacts or clarify release policy** (`build.yml`, `release.yml`) #feature #blocks-release
- [ ] T0312 **Extract shared QML JS helpers into `src/qml/js/*.js` namespaces** (`src/qml/js/`, `Theme.qml`, `main.qml`) #feature #cod-high
- [ ] T0313 **Unified playlist of Suno clips and local files** (`AudioEngine.cpp`, `Playlist.cpp`, `PlaylistPanel.qml`) #feature
- [ ] T0314 **End-user theme creation, JSON export/import, and WCAG contrast gate** (`AppearanceSettings.qml`, `ThemeDefinitions.qml`) #feature (after: T0193)
- [ ] T0315 **Mine local recon corpus for generation contract (`configs.gen-endpoint`, model list)** (`ENDPOINT-INVENTORY.md`, `SunoEndpoints.hpp`) #feature #blocks-capture
- [ ] T0316 **Advanced scene designer: scripted music video stagecraft** (`RenderJob.hpp`, `OverlayPanel.qml`, `LyricsExport.cpp`) #feature #cod-high
- [ ] T0385 **Reorganize `src/` into self-explanatory directories** (`src/`) #cleanup

---

## Capture-Gated & Human Decisions

- [ ] T0141 **Clerk route selection: `tokens` vs `touch`-first with 401 fallback** (`ENDPOINT-INVENTORY.md`) #blocks-capture
- [ ] T0142 **Unified rate-limit and error envelope parser** (`src/suno/`) #risk-high
- [ ] T0143 **Capture remaining mutation response shapes (`gen/trash`, image-id space)** (`SunoLibraryMutations.cpp`) #blocks-capture #data-loss
- [ ] T0144 **Fresh notification captures** (`SunoNotificationService.cpp`) #blocks-capture
- [ ] T0145 **Loopback OAuth acceptance capture** (`OAUTH_REDIRECT_ANALYSIS.md`) #blocks-capture
- [ ] T0146 **Verify new host leads (`content.suno.run`, `cdn3.suno.ai`, `suno.sng.link`, `platform.suno.com`) before allowlisting** (`CapturedHosts.cpp`) #security
- [ ] T0147 **Keep excluded routes (`ORCHESTRATOR_*`, `MODAL_BASE`, `/b-side/*`, `/labs/*`) refused** (`src/suno/`) #security
- [ ] T0148 **Sanitize HAR captures at ingest** (`docs/suno_api/raw/README.md`) #security
- [ ] T0174 **Record missing 2026-09-24 capture corpus state on disk without demoting `[T1]`** (`docs/suno_api/raw/`) #risk-high
- [ ] T0175 **Add `[REGISTERED]` grade for 401-only endpoints distinct from `[T1]`** (`docs/suno_api/`) #risk-medium
- [ ] T0176 **Quarantine/audit plaintext credential material outside repo** (`~`) #security
- [ ] T0177 **Re-probe 9 drifted `[T1]` route spellings before demoting** (`ENDPOINT-INVENTORY.md`) #risk-medium

### Settled Owner Decisions (2026-10-31)
- **Turnstile:** Browser handoff only (`suno.com/create`); never solve in-process.
- **Posture:** Unofficial/unaffiliated; no audio redistribution, billing writes, or legal/consent POSTs.
- **Scope:** Suno client first, projectM renderer second (keep local-file renderer independent).
- **projectM pin:** Unreleased master `349b7cc` (4.2.0-dev) for `projectm_set_frame_time()`.
- **Burn-in:** Optional `CHADVIS_HAS_AVFILTER`; app launches/records without `libavfilter`.
