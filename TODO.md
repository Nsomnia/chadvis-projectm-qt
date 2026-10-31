---
schema: 1
updated: 2026-10-30
---

# ChadVis backlog

Active items only. Completed/dropped/refuted → [`docs/dev/TODO-archive.md`](docs/dev/TODO-archive.md).
Rules: [`AGENTS.md`](AGENTS.md). Leases: [`scripts/task.sh`](scripts/task.sh).
Suno endpoints: [`docs/suno_api/ENDPOINT-INVENTORY.md`](docs/suno_api/ENDPOINT-INVENTORY.md).
Recon dirs (priority by mtime): `~/Documents/sunox-chadvis-suno-frontend-projectm-qt-research/`,
`~/Documents/suno-recon/`, `~/Documents/suno-recon-active-investigation/`,
`~/Documents/suno-recon-cat-stealth-modekl/`.

## Grammar

```
- [<mark>] <ID> <title> (<files>) [P] #tags (after: <IDs>)
```

- `<ID>` — stable, never reused. Survives renames/reorder.
- `(<files>)` — write set. Intersecting file lists = conflict.
- `[P]` — parallelizable: disjoint from every live claim's files.
- `#tags` — cost-of-delay: `#risk-high #cod-high #blocks-ui #security #data-loss #perf #feature`.
- `(after: T####)` — dependency. `task.sh next` honours it.

| Mark | Meaning |
| --- | --- |
| `[ ]` | todo, unclaimed |
| `[~]` | claimed — agent on it |
| `[?]` | code written, NOT verified |
| `[x]` | done and verified (only finished state) |
| `[!]` | blocked — reason inline |
| `[-]` | dropped, decided against |

`[x]` needs `STATUS/<id>.log` with `VERIFY` or `CHANGE` line. `task.sh audit` fails if missing.
Finished work moves to [`CHANGELOG.md`](CHANGELOG.md) + [`docs/dev/TODO-archive.md`](docs/dev/TODO-archive.md).
`git log -p TODO.md` is the full ID archive.

**Ranking positional within tier.** P0–P3. No computed score.

---

## P0 — product broken, data corrupted, or secret exposed

- [ ] T0346 **Bring every non-Studio Sun capability into product** (`src/suno/`, `src/qml_bridge/`) #feature #user-priority
      11 families, build/partial/missing per grep. Owner grant (AGENTS.md "Authorized live work")
      replaces capture requirement — agent may call any authenticated route, record at `[T1]`.
      Per-family sub-tasks in [`docs/backlog/T0346.md`](docs/backlog/T0346.md).

- [ ] T0336 **Adopt moved API base `https://suno.com/a/api`** (`SunoEndpoints.hpp`, `CapturedHosts.cpp`, `SunoClient.cpp`) #risk-high #cod-high
      Old base `studio-api-prod.suno.com` still answers — live alias, not dead.
      `route-probe.mjs` got byte-identical 200s on both. Migration = hygiene, not fix.
      `page_size` on `unified/library` ignored (20/50/100 same). `/feed/v3` honours `limit` to 100.
      `GET /playlist/{id}/?page=N` verified: flat body, `playlist_clips[]`, 1-based, no `has_more`.

- [ ] T0335 **Notification client on v2, browser moved to v3** (`SunoNotificationService.cpp`, `SunoEndpoints.hpp`, `CapturedHosts.cpp`) #risk-medium #blocks-ui
      v2 still answers 200. v3 shape differs: `text[].text` concatenated, `avatars[0].image_url`.
      Badge stays v2 (no v3 badge route). List v3, counter+mutations v2. T0067 poll fires every 60s.

- [ ] T0334 **First library sync: ~3.4s/page, 6 min spinner for 2k clips** (`SunoLibraryManager.cpp`) #risk-high #perf
      Page size now 100 (was 20). 2147 clips = 22 round trips. (a) shipped. (b) open — resume not restart.
      (c) open — `{complete, pagesFetched, expectedTotal}` unconsumed, no progress number.
      User's extension corroborated: 100/page, 6752-clip library = ~68 round trips.

- [ ] T0193 **User themes need C++ `ThemeCatalog`** (`ThemeCatalog.hpp/.cpp`, `BridgeRegistration.cpp`) #cod-high
      Phase 3. Discover `<configDir>/themes/*.json`, validate (schema, id pattern, 64KiB cap, contrast).
      Never name it `Theme` — shadows QML singleton. `Theme.qml` must never parse files itself.

- [ ] T0011 **11-route mutation surface runtime-unreachable** (`SunoLibraryMutations.hpp:493`, `.cpp:394-397`) #risk-high
      **Enabled 2026-10-30.** `setEnabled(true)` at service construction. Gates in `kGatesImplementedByThisBuild`.
      Verbs reachable, `mutationsAvailable` honest. QML still doesn't connect the 4 signals — UI gap.
      `everyGatePermits()` is single predicate. `gateFor()` maps verbs to gates.
      Response shapes captured §5.13 (reaction, remix, visibility, playlist CRUD). Wire format no longer blocker.
      Next step: control per verb, not another gate.

---

## P1 — correctness, robustness, debt that will bite

- [ ] T0356 **Suno OAuth 2.0 loopback flow — in-app sign-in** (`src/suno/auth/`, `docs/suno_api/`) #feature #blocks-ui
      `POST /api/v2/external/oauth/register/` → 201 with `client_id`. Loopback on arbitrary port accepted.
      (a) sign-in UX viable now: browser → Google/FB → loopback → refresh token. No cookie harvest.
      (b) replacing `__session` unproven: token is MCP-scoped (`resource=https://api.suno.com/mcp`).
      Needs one authorized read to test if studio API accepts it. Companion: `biyan113/suno-mcp` oauth.go.

- [ ] T0348 **`SunoEndpoints.hpp` "deliberately ABSENT" list stale for 4 routes** (`SunoEndpoints.hpp`, `SunoLibraryMutations.*`) #feature #risk-medium
      §5.13 has request+response bodies: `update_reaction_type`, `playlist/create`, `playlist/trash` (204 empty),
      `playlist/v2/{id}/cover-image`. Add constants, correct absent-list, give verbs response discipline.

- [ ] T0349 **Persona reads: `[LEAD]` path family with body, zero code** (`SunoEndpoints.hpp`, `SunoPersonaService.*`, `src/qml/panels/settings/`) #feature
      User's extension grades 3 reads CONFIRMED LIVE: `GET /api/persona/get-personas/` → `{personas:[...]}`,
      `get-loved-personas/`, `get-followed-personas/`. `[LEAD]` grade but idempotent GETs — fail-closed satisfiable.
      Write half (create/edit/visibility/voice-verification) stays capture-gated. UI behind `Personas` gate.

- [ ] T0339 **Paste path bypasses `classifyStoredCredential`** (`SettingsBridge.cpp:359-391`, `ClerkAuthClient.cpp:323-370`, `SunoClient.cpp:687-717`) #risk-medium
      `setToken()` has zero production callers. `__client_uat` accepted as credential but is metadata, not credential.
      Classify at paste boundary: `BearerToken` → `setToken`, `ClerkCookieHeader` → `setCookie`, `Unsupported` → refuse.

- [ ] T0340 **Minted bearer not bound to session that minted it** (`ClerkAuthClient.cpp:671-684`, `SunoClient.cpp:957-963`) #risk-high #security
      `handleTokensBody` accepts any `{jwt}`, sets `lastObservedSessionId_` without comparing token's `sid`.
      Fix: when `sid` claim exists and differs from `ctx.sessionId`, fail `ProtocolMismatch`. When absent, keep tolerance
      but don't clear `lastActiveSessionId_`.

- [ ] T0341 **Clerk failure envelope and `Retry-After` discarded** (`ClerkAuthClient.cpp:571-590,630-668`, `SunoClient.*`, `SunoBridge.cpp:784-804`) #risk-medium #blocks-ui
      Body's `errors[0]` never read. 429's `Retry-After` ignored. `authFailed(reason)` good sentence dies at `SunoClient`.
      Map bounded code set to fixed sentences. Never surface raw `long_message` (can echo identifier).

- [ ] T0342 **`GET /v1/client` refuses envelope with no `last_active_session_id`** (`ClerkAuthClient.cpp:91-190`) #risk-medium (after: T0340)
      One session + no selector → `NoActiveSession` despite usable `jwt`. Web client: `last_active_session_id ?? sessions?.[0]?.id`.
      Adopt fallback only when exactly one session. Two+ keep refusing.

- [ ] T0049 **Qt 6.12 required property → load-time error, not used** (`src/qml/`, `Application.cpp`) #risk-high
      Mark C++-injected QML deps `required`, supply via `setInitialProperties`. Connect `objectCreationFailed` (since 6.4).

- [ ] T0170 **Overlays/karaoke cannot appear in recorded video** (`VisualizerWindow.cpp`, `src/recorder/`) #risk-high #blocks-feature
      Recorder captures native GL context; QML scene graph is sibling on top. GL compositing impossible.
      FFmpeg post-pass (T0108/T0114) is mandatory, not optional.

- [ ] T0171 **First captured frame discarded, `pixelFormat` decorative** (`src/recorder/`) #risk-high
      PBO readback gates on `pboAvailable_`, frame 0 never reaches encoder. Pixel-format half shipped.
      Frame-0 half open — needs GL. `avPixelFormatFor()` maps enum, `validate()` refuses wrong combos.

- [ ] T0173 **projectM broken-preset retry: premise needs GL evidence** (`Bridge.cpp:48`) #risk-medium
      Inspection says premise wrong. projectM has two callback slots: `requested` (overwritten) and `failed` (untouched).
      Retry lives in `failed` handler. Do not "fix" on current evidence. Needs real window + corrupt `.milk`.

- [ ] T0180 **ASan: `settingsWindowOpensOnNavigate` throws at teardown** (`test_main.cpp:70-126`, `integration/CMakeLists.txt:33`) #risk-high
      Not in `SAFE_TESTS`. Engine destroyed while async component creation pending. Mutex error separate.
      Pre-existing, not T0047 theme work. Do not "fix" by making test wait — find what's still being created.

- [?] T0199 **"Sign in with Google" opens browser, extracts nothing** (`src/suno/auth/`) #blocks-ui #risk-high #cod-high
      Clerk flow delivers code to `auth.suno.com`, never 127.0.0.1. Fix: harvest browser cookie store.
      `HttpOnly` blocks `document.cookie` but not on-disk SQLite. `bin/freshtok.py` already does Brave on macOS.
      Implement in C++: copy DB (WAL-safe), decrypt Chromium cookies, assemble header, hand to `SunoClient::setCookie()`.
      **Code complete, all gates green, one leg unverified**: human pressing the button (MT-001-D2).

- [ ] T0343 **TOS acceptance `[T1]` and `declared-unused`** (`SunoAccountManager.*`, `SunoSettings.qml`, `SunoBridge.*`) #blocks-ui #risk-medium (after: T0341)
      `GET /api/user/tos_acceptance` → `{has_accepted_tos, has_accepted_tos_timestamp}`. Re-prompt when false or
      timestamp precedes `2025-11-07T08:00:00Z`. Never POST acceptance — legal act user performs in browser.

- [ ] T0344 **Envelope knows when refresh credential dies, app forgets** (`ClerkAuthClient.*`, `SunoClient.*`, `SunoSettings.qml`) #blocks-ui (after: T0342)
      `cookie_expires_at` (epoch-ms), session `abandon_at`/`expire_at` in captured envelopes. Parser reads only `id`+`jwt`.
      `__client` lives ~1 year. Carry dates out, show "re-import before <date>" advisory. Advisory only — never gate.

---

## P2 — robustness, cleanup, debt

- [?] T0355 **Song-page detail: ancestry, remix count, comments, similar** (`SunoClipDetailService.*`, `SunoBridge.*`, `ClipDetailSheet.qml`) #feature
      4 GETs wired: `/clips/{id}/attribution`, `/gen/{id}/comments`, `/clips/remixes/count?clip_id=`, `/clips/get_similar/?id=`.
      Each section settles independently. Failed read leaves sheet as-is. `load("")` on close drops detail.
      8 test cases pass. **Not verified**: live bodies — parser keys from catalogue, not capture.

- [?] T0351 **Generation submit+poll exist; captcha token missing** (`SunoGenerationService.*`, `SunoBridge.*`, `CreateView.qml`) #feature #blocks-ui
      Wired: `/c/check` preflight → `/generate/v2-web/` flat body → `/clips/get_songs_by_ids` poll (4s, 90 tries).
      9 tests pass. **Blocked**: account preflight answers `captcha_version: 2` (Cloudflare Turnstile).
      Submit without token = 422 `token_validation_failed`. Board answer: never solve Turnstile, hand off to browser.

- [ ] T0353 **App hung after clicking around; no repro** (`src/qml/`) #risk-high
      User report 2026-10-30. Not reproduced under `--debug` headless. Two suspects closed: library paging behind
      playlist view (fixed), `PlaylistStrip.qml` missing from `cmake/Sources.cmake` (fixed). Next: run with
      `QT_LOGGING_RULES="qt.qml.binding.removal.info=true"`.

- [ ] T0354 **Link time: `mold` can't help on macOS, `ld64.lld` can** (`build.sh`, `TargetSetup.cmake`) #perf
      `mold` is ELF-only. `ld64.lld` tested: 245s vs 228s — slower on this box. `CCACHE_SLOPPINESS=pch_defines,time_macros`
      set automatically. Linker swap not worth it here.

- [ ] T0327 **Credential-store handoff is captured lambda across threads** (`SunoClient.cpp:267-335`) #risk-high
      TSan sees write+read with no edge 206 times. Handoff correct today but suppression is wrong — real race
      indistinguishable. Fix: parameterless queued call with mutex-guarded slot. Target: zero reports on
      `TestSunoRequestEpoch` and `TestBoundedBody`.

- [ ] T0328 **T0154's two new posts joined same TSan class** (`AudioEngine.cpp`) #risk-medium
      `postStatusRefresh`/`postErrorSignal` are `invokeMethod(this, [this,...]{...}, QueuedConnection)`.
      Apply T0327's rule: atomic/mutex slot + parameterless queued call.

- [ ] T0329 **19 `QMetaObject::invokeMethod` sites, no rule** (`src/`) #risk-medium
      Audit each: same-thread or cross-thread. Apply no-payload rule where cross-thread. Write rule in
      `INVARIANTS.md`. Add `scripts/check-thread-handoffs.py` grep gate.

- [ ] T0330 **TSan lane counts 8, `--safe` runs 11 — different populations** (`build.sh`, `tsan.yml`) #risk-medium
      Make one list source of truth. Record per-entry baseline in checked-in file. Fail on increase per entry.

- [ ] T0331 **Sanitizer findings reported as failed ctest entry** (`build.sh`) #cleanup
      Classify exit 66 (TSan) and 67 (ASan/UBSan). Print report count next to verdict. Keep genuine failure red.

- [ ] T0318 **Saved stream carries no lyrics, no user-settable tags** (`SunoDownloader.cpp:832-900,1085`) #feature #risk-medium
      `lyrics` empty for 2147/2147 clips. 775 carry `aligned_lyrics_json`. MP4 branch writes no `©lyr`.
      Fix: write `©lyr` from `clip.metadata.lyrics` or DB aligned-lyrics. Fill sidecar. No tag override exists yet.

- [ ] T0326 **`AudioEngine::processAudioBuffer` reads `scratchBuffer_` while `applyAudioConfig` may resize** (`AudioEngine.cpp`, `.hpp`) #risk-high
      Use-after-free, not stale read. Fix: double-buffer with atomic pointer swap, or refuse conversion while reconfiguring.
      Not reproducible (darwin backend delivers no buffers, T0322).

- [ ] T0051 `QTP0004` deliberately disabled, 58 QML files away from `qt_add_qml_module` (`TargetSetup.cmake:129-134`) #risk-medium
      Enable and fix relative imports, or flatten QML tree. 6.8 can generate per-directory `qmldir`s.

- [ ] T0165 **Preset discovery misses `.milk7`/`.milkdrop`** (`PresetScanner.cpp`) #risk-medium
      projectM tests `.milk` exact, case-sensitive. `.Milk`/`.MILK`/`.milk7`/`.milkdrop` skipped. No `milkdrop.ini` in v4.

- [ ] T0058 `RatingManager` process-wide mutex-less singleton keyed on preset name (`RatingManager.cpp:9-25`) #blocks-feature
      Same preset name in two banks shares rating. Bare `std::map`, no lock. Fix: inject, key on path, add mutex.

- [ ] T0065 `SunoOrchestrator` permanently-failing stub with unreachable members (`SunoOrchestrator.cpp:14-44`) #cleanup
      Both methods only `emit errorOccurred`. Private handlers never called. `client_` written never read.

- [ ] T0066 `searchText` threaded three layers, dropped at bottom (`SunoLibraryManager.cpp:106,122`, `SunoClient.cpp:1310`) #cleanup
      `Q_UNUSED(searchText)`, `setSearchText` has no callers. Real filtering is client-side in `SunoBridge`.

- [ ] T0072 No `reuseItems` anywhere; nested non-interactive `GridView` in virtualized `ListView` (`src/qml/`) #performance
      `DiscoverView.qml:115-127` materialises every clip of every feed. 16 `layer.enabled` sites.

- [ ] T0076 Views with no loading/empty/error state (`VideoView.qml`, `PresetsPanel.qml`, `PlaylistPanel.qml`, `TransportBar.qml`) #blocks-ui
      Model on `LibraryView.qml:397-448`, `DiscoverView.qml:207-261`.

- [ ] T0080 Two writes to bound `value` on `AppSlider` fight each other (`KaraokeSettings.qml:52`, `RecordingPanel.qml:180,264`) #risk-high
      Use `onMoved` not `onValueChanged` per Qt docs.

- [ ] T0081 `AppSlider` keyboard- and screen-reader-inaccessible (`AppSlider.qml`) #risk-high
      No `activeFocusOnTab`, no arrow keys, no `Accessible.*`. Division by zero at `:37` if `from == to`.

- [ ] T0082 34 `Accessible.*` properties across 11 of 58 files; 47 have none (`src/qml/`) #risk-high

- [ ] T0083 Contrast failures real but not where old claim said (`Theme.qml`) #cleanup
      `textDisabled` 1.95–2.77:1, `recording` 3.07:1, `textSecondary` 3.46:1, `error` 3.60:1.
      Real defect: text over projectM canvas (`glassBackground` 85% opaque → ~1.3:1).

- [ ] T0084 ~58 raw unthemed QtQuick.Controls where `App*` wrapper exists (`src/qml/`) #cleanup

- [ ] T0085 `TextAccessible` names hardcoded English (`src/qml/`) #cleanup

- [ ] T0086 Remote art fetched with no `sourceSize`, sometimes not `asynchronous` (`ClipDetailSheet.qml:83-88`, `ClipCard.qml`, `SunoPanel.qml`) #performance

- [ ] T0087 Icons: two duplicate copies, neither used, three destinations share one glyph (`resources/icons/`) #cleanup

- [ ] T0088 macOS bundle ships no icon (`Info.plist.in`, `TargetSetup.cmake:262-290`) #cleanup

- [ ] T0089 `file(DOWNLOAD)` of CPM.cmake at configure into source tree, unverified (`Dependencies.cmake:38-42`) #risk-medium

- [ ] T0090 Nine of fifteen deps unpinned, two bypass system-first (`Dependencies.cmake`) #risk-medium
      CPM tags pinned to commit SHAs (not mutable tags). Conan rejected — CPM+vcpkg.json closes same gaps.

- [ ] T0093 No warnings-as-errors, clang-format/clang-tidy enforced nowhere (`Compiler.cmake:80-83`, `.github/`) #risk-medium
      Add non-blocking `git clang-format --diff main` report step.

- [ ] T0097 `unit_tests` one binary, one `QCoreApplication`, 31 suites sequential (`test_main.cpp:38,41-72`) #risk-medium
      Crash/hang in suite 30 loses suites 1–29.

- [ ] T0099 Four duplicate reply fakes, no fake `QNetworkAccessManager` (`tests/unit/suno/`) #risk-high
      New suite without injected factory makes real DNS lookup. Consolidate to `tests/support/` INTERFACE lib.

- [ ] T0100 `SunoLibraryMutations` 26KB write-path logic, zero tests (`src/suno/`) #risk-high #data-loss
      Also untested: `SunoDatabase`, `SunoBridge`, `SunoController`, `ClipResolver`, `PresetPersistence`,
      `RatingManager`, `CliUtils`, every QML bridge. Entire UI has zero tests.

- [ ] T0102 FFmpeg: `AVFormatContextDeleter` crash fixed; two residuals (`FFmpegUtils.hpp`) #risk-medium
      (a) `hwFramesCtx_`/`hwDeviceCtx_` survive `cleanup()`, never freed. (b) Return `AVERROR_EXIT` from
      `read_packet` before joining worker thread.

- [ ] T0156 **Encoder teardown blocks GUI thread** (`VideoRecorderCore.cpp:168`, `RecordingBridge.cpp:323`, `RenderExecutor.cpp:802-807`) #risk-high
      `stop` → `flush()` + `cleanup()` blocks on encoder drain. Fix: finalise on worker, post completion.

- [ ] T0157 **GUI-thread reentrancy: render runner joins own worker mid-pump** (`RenderExecutor.cpp:516-518`) #risk-high
      `enqueue` → `pump()` → `runner_()` runs synchronously on GUI thread. Queue makes no progress while GUI blocked.
      Fix: post runner to thread.

- [ ] T0158 **`RenderQueue::settle` runs receipt write on caller thread** (`RenderQueue.cpp:515` → `RenderJob.cpp:252`) #risk-medium
      Every render completion does 3 blocking syscalls on GUI thread. Fix: queue the receipt write.

- [ ] T0159 **`retireSlot` holds `JobSlot&` across call that re-enters `jobRunner`** (`RenderExecutor.cpp:844-854`) #risk-medium
      `slots_.push_back` may realloc, stale `slot`/`index`. Fix: erase slot before reporting failure.

- [ ] T0104 FFmpeg: `swr_alloc_set_opts` → `swr_alloc_set_opts2`; `av_init_packet` → `av_packet_alloc` (`src/recorder/`) #risk-high

- [ ] T0105 **Do not** "migrate" `av_hwdevice_ctx_create` (`VideoRecorderFFmpeg.cpp`) #risk-high
      Current, not deprecated. `av_hwdevice_ctx_alloc` is NOT a replacement — doesn't open device.

- [ ] T0106 Loudness: momentary/short-term unimplemented, surround weighting absent (`LoudnessAnalysis.hpp:452-465`) #risk-medium
      Integrated path verified correct (BS.1770-4). `kChannelWeight=1.0` wrong for 5.1 (should be ×1.41).
      M/S loudness doesn't exist — feature gap, not bug.

- [ ] T0107 `AV_VERSION_INT` untyped `int`, no `uint32_t` overload (`cmake/`, `src/`) #risk-medium
      Use `pkg_check_modules` at configure, extract `AV_VERSION_MAJOR`/`MINOR` at runtime. Never parse `av_version_info()`.

- [ ] T0108 Muxed subtitle tracks: `libavfilter` + `libass` behind CMake option (`Dependencies.cmake`, `SubtitleBurnIn.cpp`) #feature
      `subtitles=filename=subs.ass:fontsdir=…`. `mov_text` for MP4, `webvtt` for WebM. Local FFmpeg has libass/libfontconfig.

- [ ] T0109 Stop logging clip identifiers (`DownloadQueue.cpp:323,508-703`, `SunoDownloader.cpp:246,409,412`) #security
      Serializer structurally incapable of emitting token/cookie (preserve). Clip ID is account-linked half of scrub plan.

- [ ] T0113 `.gitignore` gaps (`build.log` present, `.DS_Store` files in tests/resources/docs) #risk-medium

- [ ] T0114 Confirm `libavfilter` NOT currently linked (`Dependencies.cmake:142`) #risk-medium
      Burn-in relies on UI gate. Adding post-pass must degrade to "no post pass" on stripped distro.

- [ ] T0115 Song/preset import must work with no Suno account (`docs/PIVOT_PLAN.md`) #feature
      Local-file path = cheapest insurance against upstream disappearing. Prerequisite for batch rendering.

- [ ] T0116 Render-job model and queue exists, no producer (`Bridge.hpp:59-68`, `SunoWorkspace.hpp`) #feature

- [ ] T0197 Nothing re-applies theme when `ui().theme` changes outside Appearance panel (`main.qml`) #risk-med
      `applyTheme()` runs once in `Component.onCompleted`. Fix: `Connections` on `themeNameChanged` unless session override active.

- [ ] T0338 **Windows vcpkg ports unpinned, CI reproducibility zero** (`windows.yml:130-160`) #risk-medium
      No `vcpkg.json`. Add manifest with `builtin-baseline` commit. Delete explicit list.

---

## P3 — polish and speculative

- [ ] T0345 **Record web client's redirect validator** (`OAUTH_REDIRECT_ANALYSIS.md`) [P] #cleanup
      Bundle's `isSafeRedirectUrl` accepts any `https:` URL. Our validator must be stricter — exact host/port/path.

- [ ] T0117 Localization: zero `qsTr` across 58 QML files, ~200 user-visible literals #cod-high #feature
      `qt_add_translations` + `engine.retranslate()`. Plurals: `qsTr(sourceText, disambiguation, n)` with `%n`.
      Add `TranslationFunctionMismatch=error`.

- [ ] T0118 Dark/light theme pair bound to `Application.styleHints.colorScheme` (`Theme.qml`) #feature
      `colorScheme()` on `QStyleHints` since Qt 6.8. `Qt.Unknown` must be handled. Light ramp unproven.

- [ ] T0119 Fix accessible-contrast failures for real (`Theme.qml`) #feature
      Raise `textDisabled`/`recording`/`textSecondary`/`error` to ≥4.5:1. Give text over canvas real scrim.
      Do NOT touch `textPrimaryVariant`.

- [ ] T0120 Waveform lyric-timing correction editor (`src/qml/`) #feature
      `LyricsData::words` carries per-word `startTime`/`endTime`. Write-once from capture, zero editors. Missing half of karaoke.

- [ ] T0121 Karaoke render strategies as swappable producers (`src/qml/`) #feature
      Wipe/bouncing-ball/scroll/dual-line. One function each — serves live overlay + FFmpeg burn-in.

- [ ] T0122 Render receipt shipped with job model (`RenderJob.hpp:129`) #feature
      Versioned sidecar: audio path+hash, scene list, karaoke source, encoder profile. First schema wins forever.

- [ ] T0123 Cheap proof render before committing to encode (`EncoderSettings.cpp`) #feature
      6-second, quarter-res, no-audio proof. Correct answer to "watch the render live".

- [ ] T0124 Wire up 8 dead encoder presets (`EncoderSettings.cpp`) #feature #cleanup
      9:16/4:5/1:1 interesting additions. Risk: projectM aspect correction never exercised at 9:16.

- [ ] T0125 Poster-frame export (`RenderTarget.cpp`, `FrameGrabber.cpp`) #feature
      `RenderTarget` reads pixels from framebuffer 0. `FrameGrabber::flipImage` is row-flip for `QImage`.

- [ ] T0126 Cut on real downbeats (`SunoEndpoints.hpp:60`) #feature #blocks-capture
      `POST /api/gen/{id}/downbeats_streaming/v2` captured, `declared-unused`. Server-computed beat source.

- [ ] T0127 Cut on real frame pacing from timestamps already in hand (`src/recorder/`) #risk-high

- [ ] T0128 Library sort, filter, multi-select, context menu (`LibraryView.qml`) #feature
      Local text search only. No sort, no multi-select, no bulk action. Remote verbs capture-gated.

- [ ] T0320 **No like/dislike anywhere, Suno surface has no UI entry point** (`LibraryView.qml`, `ListenView.qml`, `SunoLibraryMutations.*`) #feature #blocks-ui
      `is_liked` parsed/persisted/filterable, no affordance to set it. Like/unlike captured 2026-10-26.
      (a) now: thumb control bound to `is_liked`, disabled with reason while verb unavailable.
      (b) one capture: like/unlike, trash/restore, add-to-playlist, follow → flip `setEnabled` + connect signals.

- [?] T0321 **Playback controls only in Listen view — no global transport** (`TransportBar.qml`, `main.qml`, `ListenView.qml`) #feature #blocks-ui
      `TransportBar` in one place only. Written: bar docked in shell, `AudioBridge`-bound, local+remote same.
      Listen copy deleted. Still open: queue affordance, mute-specific control.

- [ ] T0129 ReplayGain presented honestly (`LoudnessAnalysis.cpp`) #feature #retention

- [ ] T0130 `--diagnostics` report (`src/`) #feature
      Secret handling = entire risk. Allowlist not deny-list. Write to user-chosen path, never upload.

- [ ] T0131 DI over singletons; separate audio from UI (`Config.hpp`, `AudioEngine.cpp`) #blocks-feature

- [ ] T0132 Consolidate QML bridge singletons to one namespaced backend (`src/qml_bridge/`) #cleanup

- [ ] T0133 Modernise deployment path (`Install.cmake`, `TargetSetup.cmake`) #cleanup
      `qt_finalize_executable` deprecated — use `qt_finalize_target`. Add `NSDocumentsFolderUsageDescription`.

- [ ] T0134 `macdeployqt` hard requirement of any default macOS configure (`TargetSetup.cmake:397-410`) #risk-medium

- [ ] T0135 `qt_add_qml_module` + QTP0004 + QML test harness (`tests/`) #risk-high
      `qmltestrunner` still right answer. No QML test today. Test binary must link `project_lib` AND `project_libplugin`.

- [ ] T0136 GPU CI lane or honest "no GL coverage" badge (`.github/workflows/`) #risk-high
      `integration_gl_tests`, `test_OffscreenRenderSpike` skip headless. ctest counts skip as success.

- [ ] T0137 Preset categories always "Uncategorized" (`PresetScanner.cpp`) #feature

- [ ] T0138 Standard modern C++ where tree reaches for older idioms (`src/`) #cleanup
      `std::mdspan`, concepts, `std::array` for `CircularBuffer`, `std::variant`, `std::jthread`.

- [ ] T0139 Hook up off-hooks keys (`main.qml`) #feature #risk-high
      `main.qml:511-545` documents bindings, `:598-602` binds shortcuts. Family has no handler. Grep every key in
      `ShortcutsSettings.qml` against `main.qml`, wire or delete.

- [ ] T0188 Interaction language: hover-grow, 44×44 targets, responsive breakpoints (`AppButton.qml`) #feature
      Hover-grow half written. Tap targets 36px, not 44. No hover on AppComboBox/AppTextField/AppSwitch/AppSlider.
      No mobile breakpoint or bottom nav. Redesign, not patch.

- [ ] T0299 **Linux ships only TGZ/ZIP — no .deb/.rpm/AppImage/Flatpak** (`Install.cmake:43`) #feature

- [ ] T0300 **Nothing measures coverage** (`.github/workflows/`, `CMakeLists.txt`) #risk-med

- [ ] T0301 **No static-analysis, SBOM, or dependency-vulnerability lane** (`.github/workflows/`) #security

- [ ] T0305 **Four overlapping C++ skills, unclear provenance** (`.opencode/skills/`) #cleanup
      `c-cpp-profi`, `modern-cpp`, `cpp-coding-standards`, `cpp-pro` all cover modern C++. Consolidate.

- [ ] T0309 **Browser-cookie import macOS-only in code, platform-blind in UI** (`BrowserCookieImport.cpp:223-260`) #risk-med
      `deriveChromiumKey`/`decryptChromiumValue` `#ifdef Q_OS_MACOS` only. Implement DPAPI (Windows), libsecret (Linux),
      or gate affordance by platform.

- [?] T0322 **Idle preset renders but doesn't react to music** (`VisualizerRenderer.cpp:88-106`, `AudioQueue.hpp`, `VisualizerAudioTap.cpp`) #blocks-ui #risk-high
      PCM hop under suspicion. `QAudioBufferOutput` only with FFmpeg backend — macOS defaults to darwin.
      `VisualizerAudioTap` fallback: decode local file on 40ms tick, push into same `AudioQueue`. Pinned by tests.
      Still open: GL observation that canvas actually moves (MT-001-D4.4).

- [ ] T0311 **Gate produces no binary, no release — artifact only on v* tag** (`build.yml:42-46`, `release.yml:78-80`) #feature #blocks-release
      Artifact half done: `build.yml` uploads TGZ/ZIP (+`.dmg`) on every push. Publish half not wired (policy call).
      `release.yml` still publishes only on `v*`, Linux+macOS only.

- [ ] T0312 **QML JavaScript as UI logic layer** (`src/qml/js/`, `Theme.qml`, `main.qml`) #feature #cod-high
      54 QML files, zero `.js`. Shared behaviour is per-file copy. `.js` files imported as namespaces, one concern per file.
      No DOM, no `fetch`, no `require`. Network/fs/mutex stays in C++.

- [ ] T0313 **One playlist of Suno clips and local files** (`AudioEngine.cpp`, `Playlist.cpp`, `PlaylistPanel.qml`) #feature
      Entry is `remote clip | local file`. `QMediaPlayer` single playback path for both. Anchors: T0115, T0116, T0264.

- [ ] T0314 **End-user theme creation and sharing** (`AppearanceSettings.qml`, `ThemeDefinitions.qml`, `CONFIG.md`) #feature
      No per-token editor, no light theme, no export/import. File-based sharing: one JSON, no server.
      `check-theme-contrast.py` re-implements WCAG luminance — shared theme failing gated pair = refused. (after: T0193, T0241)

- [ ] T0315 **Mine local recon corpus for generation contract** (`ENDPOINT-INVENTORY.md`, `OBSERVED-LEADS.md`, `SunoEndpoints.hpp`) #feature #blocks-capture
      `POST /api/generate/v2-web/` `[T1]`-captured but `declared-unused`. Answer: which route create page calls today
      (`configs.gen-endpoint` names it — read flag), model-list source, captcha-token mint route, completion channel.
      Recon dirs on this machine. Discipline: bundle string = lead, never evidence.

- [ ] T0316 **Advanced scene designer: scripted music video stagecraft** (`RenderJob.hpp`, `OverlayPanel.qml`, `LyricsExport.cpp`, `SunoDownloader.cpp`) #feature #cod-high
      Scene today: `{presetName, durationSeconds, crossfadeSeconds}`. Extend with typed elements + time script.
      Evaluated by C++ at render time, never QML at draw time. Lyric tag filtering. Anchors: T0242, T0120, T0121, T0108.

---

## Capture-gated — ship the gate, not the blocker

Engineering tasks with stated dependency. `[!]` = proceed, leave switch off.

- [ ] T0141 **Clerk route selection: `tokens` vs `touch`** (`ENDPOINT-INVENTORY.md`) #blocks-capture
      Both `[T1]`-observed. Ship `touch`-first with 401-retry fallback. Largest `[T1]`-without-code gap.

- [ ] T0142 **Error and rate-limit envelope handling** (`src/suno/`) #risk-high
      No reviewed request contains rate-limit envelope. One parser + one UI state for every route.

- [ ] T0143 **Library mutation success-response shapes** (`SunoLibraryMutations.cpp`) #blocks-capture #data-loss
      Broadly captured §5.13: reaction, remix toggles, visibility, playlist verbs. Traps: `trash` 422 names
      `spec.playlist_id`, `tracks/add` reports `{successes, failures}` under 200. Still uncaptured: `gen/trash`, image-id space.

- [ ] T0144 **Fresh notification captures** (`SunoNotificationService.cpp`) #blocks-capture

- [ ] T0145 **Loopback acceptance capture** (`OAUTH_REDIRECT_ANALYSIS.md`) #blocks-capture
      Only capture that can unblock P0-class gate. Rejection equally valuable — settles native sign-in permanently.

- [ ] T0146 **New host leads need capture before wiring** (`CapturedHosts.cpp`) #security
      `content.suno.run`, `cdn3.suno.ai`, `suno.sng.link`, CloudFront allowlist, `audiopipe-dev` staging.
      Excluded on purpose: money-moving, consent routes, `/b-side/*` (77 staff routes incl. impersonate), contest downloads, `/labs/*`.

- [ ] T0147 **Keep excluded on purpose** (`src/suno/`) #security
      `ORCHESTRATOR_CHAT`, `ORCHESTRATOR_HISTORY`, `MODAL_BASE` + host refusal. Don't wire, don't delete without decision.

- [ ] T0148 **Capture hygiene before any capture** (`docs/suno_api/raw/README.md`) #security
      Burp/mitmproxy + export HAR. Sanitize at ingest: keep names/paths/headers, strip values/tokens/cookies/ids.

- [ ] T0174 **2026-09-24 capture corpus gone, SHA-256 manifest unverifiable** (`docs/suno_api/raw/`) #risk-high
      Don't demote any `[T1]` — records observation, not what's on disk. Don't delete manifest. Record on-disk state.

- [ ] T0175 **`[REGISTERED]` evidence grade needed, distinct from `[T1]`** (`docs/suno_api/`) #risk-medium
      ~62 endpoints proved to exist by 401, but 401 observes no body. Split existence from contract.

- [ ] T0176 **Credential material outside repo in 3+ places** (`~`) #security
      Active-investigation dir has plaintext JWT + cookies. Read it, cite route names, never copy the file.

- [ ] T0177 **Nine `[T1]` route spellings drifted, should re-probe** (`ENDPOINT-INVENTORY.md`) #risk-medium
      Opposite of T0069. Re-probe before demoting — static negative is weak evidence.

---

## Human decisions — not tasks

Forks with real cost on one side. None resolved by agent inferring intent.

- **Generation / Turnstile.** Never solve — hand off to browser + import (recommended) · embedded-webview managed solve · drop Create surface. Option 1 costs afternoon; option 2 = ToS exposure; option 3 costs "Suno client" label.
- **Third-party client posture.** Ship as unofficial/unaffiliated, no audio redistribution, no money/legal routes (recommended) · something more assertive. Legal judgement only user can make.
- **Auth: one more capture or manual credentials forever?** Scoped capture settles Clerk loopback + sign-out + `touch` vs `tokens` (recommended) · accept manual paste as shipping 1.1.x.
- **The scope call.** Download manager + library view + account, renderer as product (recommended)? Determines which half gets next 6 months.
- **Windows in CI.** Add `windows-latest` as `continue-on-error` for signal (recommended) · omit. Windows = major userbase, credential storage plaintext = worst silence.
- **projectM pin policy.** Stay on 4.1.6, ship realtime-throttled v1 · pin unreleased master SHA for offline render now. First free; second buys offline rendering at permanent upgrade cost.
- **Burn-in dependency policy.** Add `libavfilter`+`libass` as hard dep · behind CMake option degrading to "no post pass" (recommended).

---

## Verification bar

1. `./build.sh --fast --tests` builds test targets. App usually unnecessary.
2. `ctest --test-dir build-fast/tests --output-on-failure -j"$(sysctl -n hw.ncpu)"`.
   NOT `--test-dir build` — no `CTestTestfile.cmake`, zero tests, exits 0.
3. Record proof as `VERIFY` line in `STATUS/<id>.log` (exact command+result), or `CHANGE` line when change is its own proof. Then flip `[?]` → `[x]`. `task.sh audit` fails if `[x]` has neither.
4. Sanitizers before merge for recorder/session work: `./build.sh --tsan --tests && ./build.sh --tsan --safe`, and `--asan` equivalents.
5. **Green run ≠ evidence for GL, burn-in, or audio.** Those skip headless. Needs real runtime observation or human on screen.
6. Record user-visible/behavioural outcomes in [`CHANGELOG.md`](CHANGELOG.md).

---

## Suno endpoint quick-reference

From [`docs/suno_api/ENDPOINT-INVENTORY.md`](docs/suno_api/ENDPOINT-INVENTORY.md),
[`docs/suno_api/OBSERVED-LEADS.md`](docs/suno_api/OBSERVED-LEADS.md),
[`docs/suno_api/THIRD-PARTY-IMPLEMENTATIONS.md`](docs/suno_api/THIRD-PARTY-IMPLEMENTATIONS.md),
recon dirs, and user's extension (`~/Documents/suno-master-utility-browser-extension/lib/api.js`).

### API bases
- `https://suno.com/a/api` — browser's current base (2026-10-24 HAR). Old base `studio-api-prod.suno.com/api` still live alias.
- `https://studio-api-prod.suno.com/api` — legacy base, still answers.

### Confirmed live (`[T1]` — our observation)
- `GET /feed/v3` — library feed, cursor-paged, `has_more` + 36-char cursor
- `GET /unified/library` — library, numeric offset, `page_size` ignored (20/50/100 same)
- `GET /unified/explore` — discover feed, `feed_id`/`feed_title`, nested feeds
- `GET /playlist/me` → `{playlists:[...]}` (12 playlists live-verified)
- `GET /playlist/{id}/?page=N` — flat body, `playlist_clips[]`, 1-based, no `has_more`
- `GET /notification/v2` → `{notified_at, notifications, next_before_datetime_utc}`
- `GET /notification/v2/badge-count` → `{badge_count}`
- `GET /notification/v3` → same shape, `text[].text` concatenated, `avatars[0].image_url`
- `GET /billing/info/` → `total_credits_left`, `monthly_usage`/`monthly_limit`, `download_usage`, `plan`
- `GET /user/tos_acceptance` → `{has_accepted_tos, has_accepted_tos_timestamp}`
- `POST /c/check` → generation preflight (`required`, `captcha_version`)
- `POST /generate/v2-web/` → generation submit (flat body, `task:"generate"`)
- `GET /clips/get_songs_by_ids?ids=…` → generation poll
- `GET /clips/{id}/attribution` → `source_clips[]`, `relationship COV|EX`
- `GET /gen/{id}/comments?order=most_liked`
- `GET /clips/remixes/count?clip_id=`
- `GET /clips/get_similar/?id=`
- `POST /gen/{id}/update_reaction_type/` → LIKE: `reaction_type:"L"`, null: no `reaction_type`, `clip` is id string
- `POST /playlist/create/` → 25-key playlist object
- `POST /playlist/trash/` → 204 empty body
- `POST /playlist/v2/{id}/cover-image` → `cover_source{id,type}`
- `POST /api/v2/external/oauth/register/` → 201 with `client_id` (loopback redirect accepted)

### Confirmed live via user's extension (`[LEAD]` — not our capture)
- `GET /api/persona/get-personas/` → `{personas:[...]}`
- `GET /api/persona/get-loved-personas/`
- `GET /api/persona/get-followed-personas/`

### Captured but `declared-unused` in code
- `POST /gen/{id}/downbeats_streaming/v2`
- `POST /gen/trash`
- `POST /api/generate/lyrics-mashup`, `POST /api/generate/upsample`
- `POST /api/edit/fade|crop/{clip_id}/`, `POST /api/clips/adjust-speed/`, `POST /api/clips/reverse-clip/`
- `POST /api/gen/prompt_image/` (cover art)
- `POST /api/video/generate/{clip_id}/` + `GET …/status/` poll
- `POST /api/generate/concat/v2/` (extend)

### Lead-only (sunox register §7.6, ≤2026-09-14, retrieved 2026-10-27)
- Stem separation: `task:"gen_stem"` on `/generate/v2-web/`, `GET /api/clip/{id}/stems/pages`
- Extend: `task:"extend"` on `/generate/v2-web/`, `task:"upload_extend"`
- Cover batch: `video_gen/image/generate` + `/video/generate`, `poll_batches`
- Persona CRUD: full route family named, voice-verification chain
- Generation gates: `aug_creativity` (0–4 whole), `mumble-mode`, `max-mode` session flags

### Session notes
- Account: `pro` role, `models: array[0]` in session envelope, Turnstile-gated (`captcha_version: 2`)
- `__session` cookie expires ~46 min. `__client` refresh JWT ~1 year (HttpOnly, Secure, SameSite=None)
- `freshtok.py` at `~/Documents/suno-recon-active-investigation/bin/` extracts `__session` from Brave cookies

### Recon dirs (priority by mtime)
1. `~/Documents/sunox-chadvis-suno-frontend-projectm-qt-research/` — API intelligence, protocol audits, capability matrix
2. `~/Documents/suno-recon-active-investigation/` — `suno-session.json`, `app-endpoints.md`, `interesting-api-calls.md`, HAR captures
3. `~/Documents/suno-recon/` — JS crawler snippets, endpoint lists, API probe tools
4. `~/Documents/suno-recon-cat-stealth-modekl/` — `captured_endpoints*.json`, `network-endpoints.txt`, OAuth google flow, feature flags

### Suno layer maturity
Only playback (download tracks locally for free via trick) tested. Tagging, artwork, lyrics — unknown if done.
Major project still early. Remote info available if needed.
