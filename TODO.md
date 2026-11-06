---
schema: 1
updated: 2026-11-06
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
      **UI surface evidence (2026-10-31):** a recorded session's surfaces are catalogued in
      [`docs/suno_api/UI-SURFACE-INVENTORY.md`](docs/suno_api/UI-SURFACE-INVENTORY.md) —
      playlists incl. recover/delete, trash restore-vs-permanent, Voices from a recording,
      cover art, Hooks/Albums/Workspaces/History sections, multi-modal v6 create. Surfaces
      only; every route behind them still needs a capture.

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

- [?] T0356 **Suno OAuth 2.0 loopback flow — in-app sign-in** (`src/suno/auth/`, `docs/suno_api/`) #feature #blocks-ui
      `POST /api/v2/external/oauth/register/` → 201 with `client_id`. Loopback on arbitrary port accepted.
      (a) sign-in UX viable now: browser → Google/FB → loopback → refresh token. No cookie harvest.
      (b) replacing `__session` unproven: token is MCP-scoped (`resource=https://api.suno.com/mcp`).
      Companion: `biyan113/suno-mcp` oauth.go.
      **MCP launched, auth chain verified 2026-10-31** (unauthenticated `[T1]`, no credentials attached):
      `api.suno.com/mcp` challenges per RFC 9728 → RFC 8414 server metadata at the same
      `external/oauth` family (authorize/token/revoke/register/jwks, PKCE S256, refresh grant).
      The resource exists; the register probe's token has a home. Full chain:
      [`docs/suno_api/MCP.md`](docs/suno_api/MCP.md). What closes (b): one authorized exchange —
      mint a token, answer MCP `initialize` with it, and one studio-api read to see if it speaks both.
      **(a) implemented 2026-10-31 as `SunoOAuthLoopback`** (register → PKCE S256 → system browser →
      127.0.0.1:0 `/callback` → token exchange), wired to Settings → Account ("Sign in with Suno
      (official OAuth)"); token set stored under credential key `suno/mcp`, deliberately NOT
      `suno/default`. 9 unit tests (RFC 7636 vector, authorize-URL parameters, callback parser,
      query decoding, state/emptiness). **Not verified live**: a browser run and the token exchange
      itself — MT-level.

- [ ] T0357 **Suno MCP Beta: chat surface in ChadVis** (`src/suno/`, `docs/suno_api/MCP.md`) #feature
      Suno launched an official MCP server (`https://api.suno.com/mcp`, beta, early access). No API key —
      OAuth approve once; songs save to library, spend credits, works on Free.
      Capabilities announced: create from chat, upload own audio (voice memo → song), remix
      (covers/mashups/extend), Voices. Board answer to Turnstile may improve again: instead of handing
      the user to suno.com/create, walk the RFC 9728→8414→register/authorize/token chain (T0356's
      client) and speak MCP directly. Early-access gate:
      <https://sunomusic.typeform.com/to/JMPYxZqQ>. Facts + setup + probe evidence in
      [`docs/suno_api/MCP.md`](docs/suno_api/MCP.md).

- [~] T0358 **Reactive GUI Phase 0 + 1: motion tokens, reduced motion, juicy primitives** (`src/qml/styles/Theme.qml`, `src/qml/components/AppButton.qml`, `src/qml/components/AppSlider.qml`, `src/qml/components/AppSwitch.qml`, `src/qml/components/AppComboBox.qml`, `src/qml/components/AppTextField.qml`, `src/qml/components/AccountChip.qml`, `src/qml_bridge/SettingsBridge*`) #feature #blocks-ui
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §3–§4. Add
      `durationMicro`/`durationPop`/`motionScale` to `Theme.qml` (no new singleton yet),
      persisted `SettingsBridge.reduceMotion` seeded from `main.qml`, gate existing animations on
      `motionScale`, extend `AppButton`/`AppSlider` without breaking public API, add new
      `Wiggle.qml`, `GlowFrame.qml`, `RefreshSpinner.qml` (register every new file in
      `cmake/Sources.cmake`), replace the 4 copy-pasted 900 ms spinners, activate dormant
      `glowAccent`/shadow tokens. No Qt 6.12-only APIs (floor is 6.8). Verify: `lint-qml.sh`,
      `./build.sh --fast --tests`, Qt Quick Tests, QML Profiler traces. Agent on it 2026-11-01
      under Derek's standing authorisation to push to `main` and keep working; no Qt build
      exists in the agent VM, so desktop `./build.sh --fast --tests` + a real run remain the
      verification gate before any Phase 0/1 code is claimed complete.

- [ ] T0359 **Reactive GUI Phase 2: GUI for the 11 wired Suno mutations + generation + notifications** (`src/qml/components/ClipDetailSheet.qml`, `src/qml/components/ClipCard.qml`, `src/qml/components/PlaylistStrip.qml`, `src/qml/views/LibraryView.qml`, `src/qml/views/CreateView.qml`, `src/qml/views/NotificationsView.qml`, `src/qml/components/NavRail.qml`) #feature #blocks-ui (after: T0358)
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §6.1–§6.4. The 11
      verbs in `SunoMutationsSection` have zero QML callers today (T0011's UI gap): add a
      mutation section + overflow menu to `ClipDetailSheet`, a context-menu entry on `ClipCard`,
      playlist rename/description/cover/add/remove/reorder, new `Toast.qml` +
      `GenerationStatus.qml`, generation state visuals for every `generationState` (including
      captcha-required browser handoff — no fake in-app token input), notification badge on the
      rail + mark-all-read feedback. Optimistic UI forbidden for mutations except playlist
      reorder with a specified revert. Optional in this phase, only if prioritised: §1.4
      one-method surfaces (like/unlike, playlist create, playlist trash) + T0348 doc fixes.

- [ ] T0348 **`SunoEndpoints.hpp` "deliberately ABSENT" list stale for 4 routes** (`SunoLibraryMutations.*`) #feature #risk-medium
      Constants + absent-list **done 2026-11-06**: §5.13 bodies landed in `SunoEndpoints.hpp`
      (`PLAYLIST_CREATE`, `PLAYLIST_TRASH`, `PLAYLIST_V2_BY_ID`, `GEN_UPDATE_REACTION_TYPE`,
      `GEN_SET_METADATA`), and HAR-11 cleared them from the absent list. Remaining: the verbs
      themselves in `SunoLibraryMutations` with response discipline — `playlist/trash` returns
      204 (parse nothing), `update_reaction_type` returns state with `reaction_type` absent when
      cleared, `playlist/create` returns the 25-key object.

- [ ] T0349 **Persona: reads and create are `[T1]` now, zero code** (`SunoEndpoints.hpp`, `SunoPersonaService.*`, `src/qml/panels/settings/`) #feature
      HAR-11 (2026-11-06) captured all three: `GET /persona/get-personas/`,
      `GET /persona/get-persona/{id}/`, `POST /persona/create/` with its full body
      (ENDPOINT-INVENTORY §4.10, §5.14). Reads are idempotent — wire them first.
      `create/` spends credits and needs `rights_clearance_available` honoured, so it comes after
      the upload chain (T0362). Untested twins stay out: `get-loved-personas/`,
      `get-followed-personas/` are still extension-only `[LEAD]`. UI behind `Personas` gate.

- [ ] T0362 **Wire the HAR-11 surfaces: upload → stem → persona, and the free reads** (`src/suno/SunoUploadService.*`, `src/suno/SunoPersonaService.*`, `src/suno/SunoClient.cpp`, `src/qml_bridge/SunoBridge*`) #feature #user-priority
      Endpoint constants + docs landed 2026-11-06 (ENDPOINT-INVENTORY §4.10, §5.14). This is the
      wiring half, in dependency order, each step independently verifiable:
      1. `GET /custom-model/shared` → per-model `capabilities` + `allowed_condition_combinations`
         (the authoritative cover/mashup/upload_extend matrix; nothing may hard-code a mode list).
      2. Upload chain: `POST /uploads/audio/` → signed S3 POST → `initialize-clip/` →
         `upload-finish/`. Honour `rights_clearance_available`.
      3. `POST /clip/{id}/vox-stem` → poll `GET /processed_clip/{id}` → `vocal_audio_url`.
      4. `POST /persona/create/` from that stem (T0349's write half).
      5. Free reads: `GET /clip/{id}`, `GET /project/{id}/pinned-clips`, `GET /cms/cover-art-suggestions`,
         `POST /realtime/token` (Ably credential), `GET /challenge/progress`.
      6. `POST /video_gen/image/generate` + `cost/image` + `poll_batches` for generated cover art.
      Host allowlist is NOT widened by this task: `suno-data-uploads.s3.amazonaws.com` and
      `api.suno.com` need their own `CapturedHosts.cpp` change with evidence, fail-closed.

- [ ] T0363 **`POST /gen/{id}/set_audio_description` + upload TOS flags are unmodelled** (`src/suno/SunoLibraryMutations.*`) #feature #risk-medium
      HAR-11 shows the site sends `{"gemini_description_accepted":true,"audio_content_types":
      ["Song Demo","Voice","Sample"]}` and `set_metadata` carries
      `is_audio_upload_tos_accepted`. Uploaded audio is a different publication contract from a
      generated clip. Model the two flags before any upload UI ships, or an upload can be
      published under the wrong consent state.

- [ ] T0364 **`--fast --tests` fails to link: projectM source build vs repo spdlog/fmt** (`cmake/FindProjectM4.cmake`, `cmake/Dependencies.cmake`) #risk-high #cod-high
      On 2026-11-06 a cold `build-fast` configure+link died at `tests/unit/unit_tests`:
      `ld: symbol(s) not found for architecture x86_64` on
      `spdlog::logger::log_<std::basic_string<...>>` defined in `_deps/spdlog-build/libspdlogd.a`
      but referenced from `libproject_libd.a[3](FileUtils.cpp.o)`. projectM is pinned to 349b7cc
      and built from source, then both spdlog copies land in one link line. Not caused by the
      endpoint/docs work in the same session; no source change was made to CMake.
      Two environment repairs already applied here, both still needed on a fresh box:
      `sudo port install glm` (glm was missing) and deleting a corrupt
      `.cpm-cache/projectm/*` whose clone answered `fatal: bad object 349b7cc`.
      Fix the spdlog/fmt duplication (compiled-lib flags must match on both sides) so the
      documented `./build.sh --fast --tests` works from cold.

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

- [ ] T0360 **Reactive GUI Phase 3: audio-reactive chrome** (`src/qml_bridge/AudioBridge.*`, `src/qml/components/TransportBar.qml`, `src/qml/components/ClipCard.qml`, `src/qml/views/ListenView.qml`, `src/qml/components/VisualizerOverlay.qml`, `src/qml/components/KaraokeMaster.qml`, `src/qml/panels/LyricsPanel.qml`) #feature (after: T0359)
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §5. Add throttled
      (~30 Hz), C++-smoothed `audioLevel`/`beatPulse` (+ bands if the pipeline allows) and
      `energyActive` to the audio bridge — never on the realtime callback thread — then drive
      transport glow, the playing card, the Listen hero, and overlay beat modulation from them
      through springs, never raw per-frame bindings. CI green is NOT evidence for audio: a real
      run with real audio is the verification gate.

- [ ] T0361 **Reactive GUI Phase 4: view polish, particles, hero moments** (`src/qml/views/`, `src/qml/panels/`, `src/qml/settings/`) #feature (after: T0360)
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §6.5 + §4.8. Listen/
      Video/Lyrics/Presets polish, Discover tile parity with `ClipCard`, theme-change fade,
      bounded one-shot particles (one system per screen, never in a delegate, Phase 4 only),
      once-per-session hero moments. Extend `docs/dev/manual-qa.md` (MT-001) with the new motion
      checklist and run a full reduced-motion pass.

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
      **New 2026-10-31:** `platform.suno.com` — its own auth error page ("No platform account
      found for this email") seen after sign-out. Separate host, separate account concept;
      lead only, not a `CapturedHosts` entry.
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

## Human decisions — resolved 2026-10-31

Owner answered every fork. Recorded as decisions, not options; a later change is
a new decision, not a re-read of these.

- **Generation / Turnstile → hand off to the browser, import the result.** Never solve
  the challenge in-process. CreateView keeps telling the user to finish at suno.com/create.
- **Third-party posture → unofficial and unaffiliated.** No audio redistribution, no
  money-moving routes, no legal/consent writes. This is the boilerplate the README and
  release notes may state as settled.
- **Auth capture → one scoped HAR session.** Sign out, sign in, then browse with devtools
  recording (see the gitignored route checklist at the repo root). The owner does this;
  the agent sanitises and wires. Manual paste stays as the fallback, not the plan.
- **Scope → Suno client first, renderer second.** projectM music-video work gets spare
  time and only within API/credit allowance. The renderer's independence (T0115) stays
  valuable because it is the half that survives upstream.
- **projectM pin → unreleased master.** Pinned to `349b7cc` (4.2.0-dev) 2026-10-31 for
  `projectm_set_frame_time()` (offline render) and current fixes; upgradable only by a
  deliberate re-pin. Owner reports mild lag and suspects hidden rendering — the render
  timer already stops when the window is not exposed, so the remaining lag is per-frame
  cost, not a hidden loop.
- **Burn-in deps → behind a CMake option.** "No post pass" means: without `libavfilter`
  the app still launches and records, and only the burn-in pass is unavailable. That is
  the option chosen; a hard dependency is rejected.
- **Windows in CI** — unchanged policy: `continue-on-error` signal only.

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

### Confirmed live, 2026-11-06 full-API HAR (`[T1]`)
Capture: `~/Documents/suno-recon-har-recordings-full-api/suno.com_Archive.har` (signed-in,
508 `suno.com` requests). Detail: ENDPOINT-INVENTORY §4.10 + §5.14.
- `GET /clip/{id}` → one clip, 40 keys, no envelope
- `POST /uploads/audio/` `{extension,upload_type}` → `{id,url,fields,is_file_uploaded}` (signed S3 POST)
- `POST /uploads/audio/{id}/initialize-clip/` → `{clip_id, rights_clearance_available}`
- `POST /uploads/audio/{id}/upload-finish/` `{upload_type,upload_filename,agreed_to_vip_upload_terms}`
- `POST /clip/{id}/vox-stem` `{vocal_start_s,vocal_end_s}` → `{id,status,source_clip_id,…}`
- `GET /processed_clip/{id}` → adds `vocal_audio_url`; stem on `suno-data-uploads.s3.amazonaws.com`
- `POST /persona/create/` → persona from `root_clip_id` + `vox_audio_id` + range
- `GET /persona/get-persona/{id}/`, `GET /persona/get-personas/`
- `GET /custom-model/shared` → `generation_config.capabilities` (18) + `allowed_condition_combinations`
- `PATCH /playlist/v2/{id}` `{metadata:{name,is_public},bio:{description}}` → playlist
- `POST /project/{id}/clips` `{update_type:"add",metadata:{clip_ids}}` → 204
- `GET /project/{id}/pinned-clips`
- `DELETE /lyrics-projects/{id}` → 204 (only captured DELETE)
- `POST /realtime/token` `{capabilities:[{channel_pattern,subscribe}]}` → Ably token
- `POST /video_gen/image/generate` `{prompt,quantity,image_gen_category,aspect_ratio}`; `cost/image`, `poll_batches`
- `GET /cms/cover-art-suggestions` → `{items:[{slug,label,prompt}]}`
- `POST /gen/{id}/listen_milestone` `{milestone:"5s"|"30s"|"60s"}`; `increment_play_count/v2`
- `GET /challenge/progress`; `GET /report_v2/refund_prompt`
- `feed/v3` filter keys: `disliked public trashed unlocked upload fullSong fromStudioProject
  stem stemComplement persona user workspace sort` — values are strings `"True"`/`"False"`

### Confirmed live via user's extension (`[LEAD]` — not our capture)
- `GET /api/persona/get-personas/` → `{personas:[...]}` — **now `[T1]`, see HAR-11 above**
- `GET /api/persona/get-loved-personas/` — still extension-only
- `GET /api/persona/get-followed-personas/` — still extension-only

### Captured but `declared-unused` in code
- `POST /gen/{id}/downbeats_streaming/v2`
- `POST /gen/trash`
- `POST /api/generate/lyrics-mashup`, `POST /api/generate/upsample`
- `POST /api/edit/fade|crop/{clip_id}/`, `POST /api/clips/adjust-speed/`, `POST /api/clips/reverse-clip/`
- `POST /api/gen/prompt_image/` (cover art)
- `POST /api/video/generate/{clip_id}/` + `GET …/status/` poll
- `POST /api/generate/concat/v2/` (extend)

### Lead-only (sunox register §7.6, ≤2026-09-14, retrieved 2026-10-27)
- ~~Stem separation: `task:"gen_stem"`~~ **superseded** by HAR-11 `POST /clip/{id}/vox-stem`
- Extend: `task:"extend"` on `/generate/v2-web/`, `task:"upload_extend"`
- ~~Cover batch: `video_gen/image/generate` + `/video/generate`, `poll_batches`~~ **captured** (HAR-11)
- ~~Persona CRUD: full route family named~~ **captured** (reads + create); edit/visibility still open
- Generation gates: `aug_creativity` (0–4 whole), `mumble-mode`, `max-mode` session flags

### Session notes
- Account: `pro` role, `models: array[0]` in session envelope, Turnstile-gated (`captcha_version: 2`)
- `__session` cookie expires ~46 min. `__client` refresh JWT ~1 year (HttpOnly, Secure, SameSite=None)
- `freshtok.py` at `~/Documents/suno-recon-active-investigation/bin/` extracts `__session` from Brave cookies

### Recon dirs (priority by mtime)
1. `~/Documents/suno-recon-har-recordings-full-api/` — newest, signed-in full-API HAR
   (`suno.com_Archive.har`, 154 MB, 2026-11-05/06) + `network-endpoints.txt`
2. `~/Documents/sunox-chadvis-suno-frontend-projectm-qt-research/` — API intelligence, protocol audits, capability matrix
2. `~/Documents/suno-recon-active-investigation/` — `suno-session.json`, `app-endpoints.md`, `interesting-api-calls.md`, HAR captures
3. `~/Documents/suno-recon/` — JS crawler snippets, endpoint lists, API probe tools
4. `~/Documents/suno-recon-cat-stealth-modekl/` — `captured_endpoints*.json`, `network-endpoints.txt`, OAuth google flow, feature flags

### Suno layer maturity
Only playback (download tracks locally for free via trick) tested. Tagging, artwork, lyrics — unknown if done.
Major project still early. Remote info available if needed.
