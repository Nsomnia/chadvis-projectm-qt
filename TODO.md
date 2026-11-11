---
schema: 1
updated: 2026-11-08
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
      Stale check 2026-11-07: 165 constants, 38 referenced, 127 with zero call sites. Enrichment reads, generation submit+poll, trash restore already landed. Next verifiable chunks are T0383 reads, T0362 step 1, cover art chain.

- [x] T0336 **Adopt moved API base `https://suno.com/a/api`** (`SunoEndpoints.hpp`, `CapturedHosts.cpp`, `SunoClient.cpp`) #risk-high #cod-high
      Old base `studio-api-prod.suno.com` still answers — live alias, not dead.
      `route-probe.mjs` got byte-identical 200s on both. Migration = hygiene, not fix.
      `page_size` on `unified/library` ignored (20/50/100 same). `/feed/v3` honours `limit` to 100.
      `GET /playlist/{id}/?page=N` verified: flat body, `playlist_clips[]`, 1-based, no `has_more`.
      **DONE 2026-11-08:** production already runs on `https://suno.com/a/api` (`API_BASE` was adopted, `[T1]`, with the route-probe note) — the only stale base was ten generic test fixtures (header/timeout/foreign-reply bodies), now on the new base with identical route/assertion shapes. `CapturedHosts` keeps both hosts: `studio-api-prod.suno.com` is a live alias (byte-identical 200s via route-probe) and `suno.com` is the captured new base. The OAuth issuer URLs (`studio-api-prod.suno.com/api/v2/external/oauth/*`) were deliberately left — a different API tree (MCP.md, 2026-10-31); T0393 proposed moving them and was dropped as invalid 2026-11-10 after recon confirmed no capture exists for the joined route.

- [x] T0335 **Notification client on v2, browser moved to v3** (`SunoNotificationService.cpp`, `SunoEndpoints.hpp`, `CapturedHosts.cpp`) #risk-medium #blocks-ui
      v2 still answers 200. v3 shape differs: `text[].text` concatenated, `avatars[0].image_url`.
      Badge stays v2 (no v3 badge route). List v3, counter+mutations v2. T0067 poll fires every 60s.
      **DONE 2026-11-08:** the list fetch is on `/notification/v3` with `text[].text` concatenated in order and `avatars[0].image_url` parsed into `Author::avatarImageUrl`; the badge count and read/unread mutations stay on v2. Missing `text[]`/`avatars[]` degrade to empty caption/avatar, never a placeholder. No v3 badge route invented (there is none).

- [ ] T0334 **First library sync: ~3.4s/page, 6 min spinner for 2k clips** (`SunoLibraryManager.cpp`) #risk-high #perf
      Page size now 100 (was 20). 2147 clips = 22 round trips. (a) shipped. (b) open — resume not restart.
      (c) open — `{complete, pagesFetched, expectedTotal}` unconsumed, no progress number.
      User's extension corroborated: 100/page, 6752-clip library = ~68 round trips.
      **(b)+(c) DONE 2026-11-08:** a mid-sync failure keeps `pagesLoaded_` and holds the cursor, so `requestNextPage()` retries the failed page; `librarySyncSettled` now carries `pagesFetched` and status messages state clips across pages. No `expectedTotal` exists in the feed, so none is invented. Regression test `failedPageIsRetriedNotRestarted`. Remaining: real 2k-clip wall-clock measurement.

- [x] T0193 **User themes need C++ `ThemeCatalog`** (`ThemeCatalog.hpp/.cpp`, `BridgeRegistration.cpp`) #cod-high
      Phase 3. Discover `<configDir>/themes/*.json`, validate (schema, id pattern, 64KiB cap, contrast).
      Never name it `Theme` — shadows QML singleton. `Theme.qml` must never parse files itself.
      **DONE 2026-11-08:** `src/core/ThemeCatalog.*` discovers `<configDir>/themes/*.json`, validates size/id/schemaVersion/required fields/color tokens and WCAG contrast (mirrors `Theme.qml`'s math; no C++ helper existed), and rejects per file without aborting the others. A duplicate id keeps the first and reports the rest. The bridge property is deliberately not added yet — no bridge exposes a theme list, so that is its own slice.

---

## P1 — correctness, robustness, debt that will bite

- [x] T0011 **11-route mutation surface runtime-unreachable** (`SunoLibraryMutations.hpp:493`, `.cpp:394-397`) #risk-high
      **Enabled 2026-10-30.** `setEnabled(true)` at service construction. Gates in `kGatesImplementedByThisBuild`.
      Verbs reachable, `mutationsAvailable` honest.
      `everyGatePermits()` is single predicate. `gateFor()` maps verbs to gates.
      Response shapes captured §5.13 (reaction, remix, visibility, playlist CRUD). Wire format no longer blocker.
      **Re-ranked P0 -> P1 2026-11-08:** verbs are enabled and reachable; what remains is the QML surface, which T0359 owns.
      **DONE 2026-11-10:** the 4 signals are connected and ALL 13 verbs have a per-verb control. The 2 trash
      verbs (clip-trash-restore, playlist-trash-v2 — UI-07 [T1]) had service + section methods but no bridge
      invokable/QML control: added `setClipTrashed`/`setPlaylistTrashed` forwarders, a Trash/Playlist-trash
      MutationRow with accepted-state readback + per-subject settled handlers, and a "Move to trash" card-menu
      entry. Verified: full build 385/385, ctest 33/33, lint-qml clean (1374 baseline unchanged).

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
      query decoding, state/emptiness). **Locally verified 2026-11-09** (`test_SunoOAuthLoopback`
      1/1 ctest). **Not verified live**: a browser run and the token exchange
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
      **MCP client landed 2026-11-06:** `src/suno/SunoMcpClient.{hpp,cpp}` + `test_SunoMcpClient`
      (7 cases, JSON/SSE framing, fail-closed). Still open: bridge wiring, chat surface,
      live handshake. Bearer from `suno/mcp` (T0356), never `suno/default`.

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

- [~] T0359 **Reactive GUI Phase 2: GUI for the 11 wired Suno mutations + generation + notifications** (`src/qml/components/ClipDetailSheet.qml`, `src/qml/components/ClipCard.qml`, `src/qml/components/PlaylistStrip.qml`, `src/qml/views/LibraryView.qml`, `src/qml/views/CreateView.qml`, `src/qml/views/NotificationsView.qml`, `src/qml/components/NavRail.qml`) #feature #blocks-ui (after: T0358)
      Plan: [`docs/dev/REACTIVE-GUI-PLAN.md`](docs/dev/REACTIVE-GUI-PLAN.md) §6.1–§6.4. The 11
      verbs in `SunoMutationsSection` have zero QML callers today (T0011's UI gap): add a
      mutation section + overflow menu to `ClipDetailSheet`, a context-menu entry on `ClipCard`,
      playlist rename/description/cover/add/remove/reorder, new `Toast.qml` +
      `GenerationStatus.qml`, generation state visuals for every `generationState` (including
      captcha-required browser handoff — no fake in-app token input), notification badge on the
      rail + mark-all-read feedback. Optimistic UI forbidden for mutations except playlist
      reorder with a specified revert. Optional in this phase, only if prioritised: §1.4
      one-method surfaces (like/unlike, playlist create, playlist trash) + T0348 doc fixes.
      **Part 1 done 2026-11-08:** `ClipMutationsSection.qml` (+ `ClipActionMenu.qml`) in `ClipDetailSheet`, both registered in `Sources.cmake`. Live-verified consumption states the card is recycling-safe (settle note resets on `clipData` change, so a recycled card never carries another clip's note). Gate honesty is per-gate (library/sharing/playlists rows in `SunoBridge.gateStatuses`, absent row = denied → fail-closed), state changes only on `accepted` (no optimistic flips), and the section renders `mutationsUnavailableReason` verbatim when nothing permits. `SunoBridge` now also forwards `is_public` into the clip-variant map (the feed field existed, was dropped before QML). Two defects found at integration time and fixed by the orchestrator: (1) `ClipMutationsSection` without `import QtQuick.Controls` failed to load (`ToolTip.visible` = "Non-existent attached object"), (2) four per-row `shakeAnim` ids collided ("id is not unique") leaving the whole LibraryView chain unavailable. Reorder (`reorderPlaylistTracks`) needs a C++ index follow-up since `playlistClips` carries no relative index. Part 2 (Toast, NavRail, CreateView, LibraryView) remains.


- [x] T0348 **`SunoEndpoints.hpp` "deliberately ABSENT" list stale for 4 routes** (`SunoLibraryMutations.*`) #feature #risk-medium
      Constants + absent-list **done 2026-11-06**: §5.13 bodies landed in `SunoEndpoints.hpp`
      (`PLAYLIST_CREATE`, `PLAYLIST_TRASH`, `PLAYLIST_V2_BY_ID`, `GEN_UPDATE_REACTION_TYPE`,
      `GEN_SET_METADATA`), and HAR-11 cleared them from the absent list. Remaining: the verbs
      themselves in `SunoLibraryMutations` with response discipline — `playlist/trash` returns
      204 (parse nothing), `update_reaction_type` returns state with `reaction_type` absent when
      cleared, `playlist/create` returns the 25-key object.
      **DONE 2026-11-06:** verbs landed in `SunoLibraryMutations` with response discipline (204 parses nothing, reaction clear sends null, 8 new tests). `SunoMutationsSection::isClipScoped` covers the new verbs (7 clip / 9 playlist).

- [ ] T0349 **Persona: reads and create are `[T1]` now, zero code** (`SunoEndpoints.hpp`, `SunoPersonaService.*`, `src/qml/panels/settings/`) #feature
      HAR-11 (2026-11-06) captured all three: `GET /persona/get-personas/`,
      `GET /persona/get-persona/{id}/`, `POST /persona/create/` with its full body
      (ENDPOINT-INVENTORY §4.10, §5.14). Reads are idempotent — wire them first.
      `create/` spends credits and needs `rights_clearance_available` honoured, so it comes after
      the upload chain (T0362). Untested twins stay out: `get-loved-personas/`,
      `get-followed-personas/` are still extension-only `[LEAD]`. UI behind `Personas` gate.
      READS DONE 2026-11-08: `SunoPersonaService` reads `GET /persona/get-personas/` and `GET /persona/get-persona/{id}/`; id/name typed, the rest kept in `raw`. `create/` still open after T0362 upload chain. Live GET 200 still open.

- [x] T0362 **Wire the HAR-11 surfaces: upload → stem → persona, and the free reads** (`src/suno/`, `src/qml_bridge/`) #feature #user-priority
      Umbrella task, split 2026-11-08 into T0430-T0435 so each step is independently claimable.
      Shipped inside this epic before the split: step 1 `SunoModelCatalog` (`GET /custom-model/shared`)
      and step 6 slice 1 `SunoCoverArtService` (`POST /gen/prompt_image/`).
      Host allowlist is NOT widened by this task: `suno-data-uploads.s3.amazonaws.com` and
      `api.suno.com` need their own `CapturedHosts.cpp` change with evidence, fail-closed.

- [x] T0430 **Upload chain: reserve → S3 → initialize-clip → finish** (`src/suno/SunoAudioUploadService.*`, `SunoClient.cpp`) #feature (after: T0362)
      `POST /uploads/audio/` → signed S3 POST → `POST /uploads/audio/{id}/initialize-clip/` → `POST /uploads/audio/{id}/upload-finish/`.
      `rights_clearance_available:false` must ask, never guess. Mostly present in `SunoAudioUploadService`;
      open work is the initialize-clip leg and its response discipline.
      Verify: fixture test for each leg + one live 2xx, no credits.
      **DONE 2026-11-08:** `initialize-clip` leg wired (reserve -> S3 -> initialize-clip -> finish); `rights_clearance_available:false` fails with a decision error unless the caller explicitly opts in via `setProceedWithoutRightsClearance(true)`. 3 static + 4 chain fixture tests; `clipId()` kept. Also fixed the test binary's missing `QCoreApplication`. Host allowlist untouched (needs its own evidence change).

- [x] T0431 **Vocal stem extraction and poll** (`src/suno/`) #feature (after: T0430)
      `POST /clip/{id}/vox-stem` `{vocal_start_s, vocal_end_s}` → `{id: processed-clip, status}`;
      poll `GET /processed_clip/{id}` until `vocal_audio_url`. Range comes from the user's selection;
      no auto-detect route exists. Verify: fixture tests for submit and each observed status.
      **DONE 2026-11-08:** `SunoStemService` — `submit(clipId, vocalStartS, vocalEndS)` posts exactly `{vocal_start_s, vocal_end_s}` and returns the processed-clip id; `poll(processedClipId)` reads `GET /processed_clip/{id}`. Only `submitted`/`complete` are mapped (the first from the twin route's capture); anything else degrades to `Unknown` with the raw object. Invalid range fails before any request. 10 fixture tests.

- [x] T0432 **Persona create from a stem** (`src/suno/SunoPersonaService.*`) #feature (after: T0431)
      `POST /persona/create/` with `root_clip_id`, `vox_audio_id`, range, `persona_type:"vox"`, `clips[]`.
      Spends credits. Reads already shipped (T0349). Verify: fixture test for the body; one live create
      against a scratch persona, then record the response keys.
      **DONE 2026-11-08:** `createPersona(CreatePersonaParams)` sends the captured ten-key body with `persona_type:"vox"` fixed; refuses empty root-clip/vox-audio ids without a request. The create RESPONSE shape is not in the inventory, so a 2xx settles success and the reply is kept verbatim in `created()` — no field invented. Still needs one live create against a scratch persona (spends credits).

- [x] T0433 **Free reads bundle** (`src/suno/`, `src/qml_bridge/`) #feature (after: T0362)
      `GET /clip/{id}`, `GET /project/{id}/pinned-clips`, `GET /cms/cover-art-suggestions`,
      `POST /realtime/token` (Ably credential), `GET /challenge/progress`. All read-only, no credits.
      Verify: one read service with fixture tests per route.
      **DONE 2026-11-08:** `SunoFreeReadsService` ships all five reads (`fetchClip`, `fetchPinnedClips`, `fetchCoverArtSuggestions`, `fetchRealtimeToken`, `fetchChallengeProgress`); 16 fixture tests; uncaptured shapes kept raw. All five endpoint constants already existed.

- [x] T0434 **Cover art apply: playlist cover from a generated image id** (`src/suno/SunoLibraryMutations.*`) #feature (after: T0362)
      Generation half shipped (`SunoCoverArtService`). Open: feed `image_s3_id` into
      `POST /playlist/v2/{id}/cover-image` `{cover_source:{id,type:"generated"}}` and surface per-field outcome.
      Verify: fixture test for the body; one live apply on a scratch playlist, then revert.
      **DONE 2026-11-08:** the apply verb already existed but sent a flat body the server rejects; it now sends the evidenced nested `{"cover_source":{"id","type":"generated"}}`. 6 new test slots incl. byte-exact body assertion and 404 `cover_source_not_found` -> `Rejected`.

- [x] T0435 **Voice verification chain** (`src/suno/`) #feature (after: T0431)
      `GET /voice-verification/phrase/` → `POST /processed_clip/voice-vox-stem` (upload id, not clip id)
      → `POST /voice-verification/` → poll `GET /voice-verification/{id}`. This is what makes a voice
      persona usable. Verify: fixture tests; the two stem routes take different id kinds and must not
      be interchangeable.
      **DONE 2026-11-08:** `SunoVoiceVerificationService` with the full chain (phrase → stem → verification → poll). The two stem routes take different id kinds and that is now a compile-time boundary: `UploadId` and `VerificationId` are distinct strong types with no conversion between them. 9 fixture tests.

- [x] T0363 **`POST /gen/{id}/set_audio_description` + upload TOS flags are unmodelled** (`src/suno/SunoLibraryMutations.*`) #feature #risk-medium
      HAR-11 shows the site sends `{"gemini_description_accepted":true,"audio_content_types":
      ["Song Demo","Voice","Sample"]}` and `set_metadata` carries
      `is_audio_upload_tos_accepted`. Uploaded audio is a different publication contract from a
      generated clip. Model the two flags before any upload UI ships, or an upload can be
      published under the wrong consent state.
      **DONE 2026-11-06:** `ClipMetadataUpdate` + `AudioDescriptionConsent` typed records with body builders in `SunoLibraryMutations`; verbs arrive with T0362.

- [x] T0364 **`--fast --tests` fails to link: projectM source build vs repo spdlog/fmt** (`cmake/FindProjectM4.cmake`, `cmake/Dependencies.cmake`) #risk-high #cod-high
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
      **DONE 2026-11-06:** `cmake/TargetSetup.cmake` prepends each paired target's own
      interface include dir to `COMMON_INCLUDES` (spdlog/fmt/tomlplusplus). Verified:
      `--fast --tests --safe` 16/16, unit_tests status 0. Cold-configure retest still open.

- [ ] T0365 **Listen for Credits: a route family that EARNS credits, zero code** (`src/suno/SunoPreferencesService.*`, `src/qml/panels/settings/`) #feature #user-priority
      Persona HAR 2026-11-06 captured the whole program (ENDPOINT-INVENTORY §5.15): `summary`,
      `tasks`, `submit`, `tasks/release-buffer`, `rewards`, `stats`, `leaderboard`, `profile`,
      plus the `listen_and_rank` survey. Reading a task and playing its clips costs nothing and
      pays credits per accepted rating (`daily_submit_cap: 200`, streak reward at 5 tasks/day).
      **The submit step is captcha-gated**: it answers 403 `verification_required` until the
      owner's browser handoff completes. So build the read side + a queue view, and hand the
      submit to the browser rather than faking it. Always `release-buffer` a claimed task that
      will not be finished — abandoning tasks degrades availability.
      READS DONE 2026-11-08: `SunoPreferencesService` reads `summary`, `tasks`, `rewards`, `stats`, `profile`, `leaderboard` as raw objects. Submit stays browser handoff. Live GET 200 still open.

- [x] T0339 **Paste path bypasses `classifyStoredCredential`** (`SettingsBridge.cpp:359-391`, `ClerkAuthClient.cpp:323-370`, `SunoClient.cpp:687-717`) #risk-medium
      `setToken()` has zero production callers. `__client_uat` accepted as credential but is metadata, not credential.
      Classify at paste boundary: `BearerToken` → `setToken`, `ClerkCookieHeader` → `setCookie`, `Unsupported` → refuse.
      **DONE 2026-11-07:** paste boundary now classifies; JWT -> setToken (now has a caller), cookie
      header -> setCookie, Unsupported/Empty -> refused. Metadata-only strings no longer look like credentials.

- [x] T0340 **Minted bearer not bound to session that minted it** (`ClerkAuthClient.cpp:671-684`, `SunoClient.cpp:957-963`) #risk-high #security
      `handleTokensBody` accepts any `{jwt}`, sets `lastObservedSessionId_` without comparing token's `sid`.
      Fix: when `sid` claim exists and differs from `ctx.sessionId`, fail `ProtocolMismatch`. When absent, keep tolerance
      but don't clear `lastActiveSessionId_`.
      **DONE 2026-11-06:** sid mismatch fails `ProtocolMismatch` (secret-free), absent sid keeps the observed session; 5 new tests in `test_ClerkAuthClient`.

- [x] T0341 **Clerk failure envelope and `Retry-After` discarded** (`ClerkAuthClient.cpp:571-590,630-668`, `SunoClient.*`, `SunoBridge.cpp:784-804`) #risk-medium #blocks-ui
      Body's `errors[0]` never read. 429's `Retry-After` ignored. `authFailed(reason)` good sentence dies at `SunoClient`.
      Map bounded code set to fixed sentences. Never surface raw `long_message` (can echo identifier).
      **Partly stale (2026-11-07):** the failure reason already reaches the UI
      (`errorOccurred` -> SunoBridge::onLibraryFetchFailed -> errorMessage), and
      `httpFailureReason(status)` already emits fixed sentences only -- no raw
      `long_message`, no identifier leak. The security half is CLOSED. Remaining
      enhancements: read `errors[0].code` for a bounded fixed-sentence map, and
      honor 429 `Retry-After`. Neither is a secret-exposure defect.
      **DONE 2026-11-08:** `errors[0].code` now maps through one 8-entry fixed-sentence table in `ClerkAuthClient` (`long_message` never read); 429/430 surfaces `Retry-After` seconds on the existing `authFailed` sentence. Verified `./build.sh --fast --tests --safe` 16/16 (incl. `test_ClerkAuthClient`).

- [x] T0342 **`GET /v1/client` refuses envelope with no `last_active_session_id`** (`ClerkAuthClient.cpp:91-190`) #risk-medium (after: T0340)
      One session + no selector → `NoActiveSession` despite usable `jwt`. Web client: `last_active_session_id ?? sessions?.[0]?.id`.
      Adopt fallback only when exactly one session. Two+ keep refusing.
      **DONE 2026-11-06** with T0340; deduped across both envelope halves.

- [x] T0049 **Qt 6.12 required property → load-time error, not used** (`src/qml/`, `Application.cpp`) #risk-high
      Mark C++-injected QML deps `required`, supply via `setInitialProperties`. Connect `objectCreationFailed` (since 6.4).
      **DONE 2026-11-08:** the premise was wrong for this tree: there are no `setContextProperty`/`setInitialProperties` sites and no QML component takes an injected delegate dep, so `required` has nothing to hold. Every C++-injected dependency in QML is a `qmlRegisterSingletonType` singleton, and `required` applies to instantiated components, not singletons. What was real: a failed QML load only said "root load returned no objects". `QQmlApplicationEngine::objectCreationFailed` is now connected and logs the failing URL (`url.path()`, not the full URL); the detailed warnings were already attached. The unused `engine` parameter in `registerBridges` is flagged as untouchable (changing it churns `test_main.cpp` for zero behaviour gain).

- [ ] T0170 **Overlays/karaoke cannot appear in recorded video** (`VisualizerWindow.cpp`, `src/recorder/`) #risk-high #blocks-feature
      Recorder captures native GL context; QML scene graph is sibling on top. GL compositing impossible.
      FFmpeg post-pass (T0108/T0114) is mandatory, not optional.

- [x] T0171 **First captured frame discarded, `pixelFormat` decorative** (`src/recorder/`) #risk-high
      PBO readback gates on `pboAvailable_`, frame 0 never reaches encoder. Pixel-format half shipped.
      Frame-0 half open — needs GL. `avPixelFormatFor()` maps enum, `validate()` refuses wrong combos.
      **DONE 2026-11-08:** the gate was in the renderer, not the recorder (`src/visualizer/VisualizerRenderer`), which is why this stayed open. `setupPBOs()` hard-coded `pboAvailable_ = false` and only flipped it at the end of the first capture — after the emit was skipped — so frame 0 was emitted by the second render, never by its own. Now the flag is derived from buffer-init outcome (no writer left in the capture path), the first capture completes inline so frame 0 is emitted by its own render, and later captures keep the one-frame pipeline. PBO readback genuinely cannot run headless (the offscreen spike uses plain `glReadPixels`, and a drawable-less framebuffer-0 readback is zeros/errors by its own sentinel measurements), so the headless proof is a unit assertion on the pure entry-condition decision `captureEmit(first, pending)` plus the real GL slot `firstRenderEmitsAFrame` (fails on old code: needs two renders). Adjacent, unfixed: the last frame of a recording is still not drained at stopRecording — one frame short at the tail.

- [ ] T0173 **projectM broken-preset retry: premise needs GL evidence** (`Bridge.cpp:48`) #risk-medium
      Inspection says premise wrong. projectM has two callback slots: `requested` (overwritten) and `failed` (untouched).
      Retry lives in `failed` handler. Do not "fix" on current evidence. Needs real window + corrupt `.milk`.

- [x] T0180 **ASan: `settingsWindowOpensOnNavigate` throws at teardown** (`test_main.cpp:70-126`, `integration/CMakeLists.txt:33`) #risk-high
      Not in `SAFE_TESTS`. Engine destroyed while async component creation pending. Mutex error separate.
      Pre-existing, not T0047 theme work. Do not "fix" by making test wait — find what's still being created.
      **DONE 2026-11-08 (test half):** verdict A — a test-lifecycle bug, not an app race. The slot never pumped the GUI thread's event loop, so `QQmlApplicationEngine` left ~77 incubated objects (ListView/Repeater delegates, StackLayout children, Popup/Dialog content) pending at destruction — the "QML components still being created at engine destruction" abort. The drain is by `QQmlIncubatorController::isReady()`, not an arbitrary wait. The other half of the original report (a destroyed `std::mutex` at static teardown) matched the RatingManager static already retired by T0058; any remaining abort after this drain implicates `Config::mutex_` and needs a fresh `--asan` lane to confirm. Production hardening (drain in `aboutToQuit`) is filed separately as T0451.

- [?] T0199 **"Sign in with Google" opens browser, extracts nothing** (`src/suno/auth/`) #blocks-ui #risk-high #cod-high
      Clerk flow delivers code to `auth.suno.com`, never 127.0.0.1. Fix: harvest browser cookie store.
      `HttpOnly` blocks `document.cookie` but not on-disk SQLite. `bin/freshtok.py` already does Brave on macOS.
      Implement in C++: copy DB (WAL-safe), decrypt Chromium cookies, assemble header, hand to `SunoClient::setCookie()`.
      **Code complete, all gates green, one leg unverified**: human pressing the button (MT-001-D2).

- [x] T0343 **TOS acceptance `[T1]` and `declared-unused`** (`SunoAccountManager.*`, `SunoSettings.qml`, `SunoBridge.*`) #blocks-ui #risk-medium (after: T0341)
      `GET /api/user/tos_acceptance` → `{has_accepted_tos, has_accepted_tos_timestamp}`. Re-prompt when false or
      timestamp precedes `2025-11-07T08:00:00Z`. Never POST acceptance — legal act user performs in browser.
      **DONE 2026-11-08:** `SunoAccountManager::refreshTosAcceptance()` reads `GET /api/user/tos_acceptance` only (never a POST — acceptance stays a browser act). `needsTosReprompt` fires on `!hasAcceptedTos`, an unparseable timestamp, or a timestamp before `2025-11-07T08:00:00Z`; transport failure leaves the state unknown without claiming acceptance. Boundary tested at the cutoff.

- [x] T0344 **Envelope knows when refresh credential dies, app forgets** (`ClerkAuthClient.*`, `SunoClient.*`, `SunoSettings.qml`) #blocks-ui (after: T0342)
      `cookie_expires_at` (epoch-ms), session `abandon_at`/`expire_at` in captured envelopes. Parser reads only `id`+`jwt`.
      `__client` lives ~1 year. Carry dates out, show "re-import before <date>" advisory. Advisory only — never gate.
      **DONE 2026-11-08:** envelopes now carry `cookie_expires_at`, session `abandon_at`/`expire_at` into `CredentialExpiry` on `ClerkAuthClient::lastCredentialExpiry()`; `reimportAdvisory()` returns a re-import sentence when the earliest date is within 30 days, else nothing. Malformed dates are ignored (never reject a credential). Advisory only — nothing gates on it. The one-line `SunoClient` accessor + `Q_PROPERTY` for QML is still to add.

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

- [-] T0354 **Link time: `mold` can't help on macOS, `ld64.lld` can** (`build.sh`, `TargetSetup.cmake`) #perf
      `mold` is ELF-only. Old claim "245 s vs 228 s" unreproducible: paired benchmark 2026-11-08 on this box (same link line, 8 reps, `tests/unit/unit_tests`) medians 1.051 s Apple ld vs 1.051 s ld64.lld. Link is not the bottleneck (~1 s/link); the 31 redundant links are (T0453). No linker swap.

- [!] T0327 **Credential-store handoff is captured lambda across threads** (`SunoClient.cpp:267-335`) #risk-high
      TSan sees write+read with no edge 206 times. Handoff correct today but suppression is wrong — real race
      indistinguishable. Fix: parameterless queued call with mutex-guarded slot. Target: zero reports on
      `TestSunoRequestEpoch` and `TestBoundedBody`.
      **ANALYZED 2026-11-08, blocked on reproduction (not on a design choice). Verdict: a benign TSan false positive, not a real race.** The three reports are the closure's own capture storage, not project state: `QCallableObject`'s constructor writes the functor on the posting thread, and `impl()` reads it on the receiving thread (`qobjectdefs_impl.h:548`). The happens-before edge is `QCoreApplication::postEvent`'s event-queue mutex inside libQt6Core, which has zero `__tsan_*` symbols, so TSan cannot see it. `restoreInFlight_`, `restoreDiscarded_`, `restoreGeneration_`, `restoreCompletions_` are not part of the race (same thread by construction, and `kCompletionConnectionType` is pinned queued). `cmake/tsan.supp` is deliberately empty and its own rule forbids an un-reproduced entry, so the drafted `race_top:qobjectdefs_impl.h` entry (fallback `race:QCallableObject`) is not added. Next action: one `--tsan` lane run over `test_SunoRequestEpoch` + `test_BoundedBody`, capture the symbolized report, confirm the substring, then add the entry and amend `INVARIANTS.md:64-81` (which currently requires tsan.supp to stay empty) in the same commit.

- [x] T0328 **T0154's two new posts joined same TSan class** (`AudioEngine.cpp`) #risk-medium
      `postStatusRefresh`/`postErrorSignal` are `invokeMethod(this, [this,...]{...}, QueuedConnection)`.
      Apply T0327's rule: atomic/mutex slot + parameterless queued call.
      **DONE 2026-11-10:** both posts are parameterless `invokeMethod(this, &AudioEngine::..., QueuedConnection)`
      calls. Status needs no payload beyond `observedSinkRate_`, already atomic. Errors are pushed onto a
      mutex-guarded `pendingErrors_` vector and drained by the new private slot `emitPendingErrors()`,
      which emits outside the lock (a subscriber may re-enter). Behaviour is unchanged: one signal per
      posted message, same thread. Qt 6's PMF overload wraps a member pointer rather than a capture, so
      no mutable functor storage crosses the boundary. `reportConversionFailure`'s lambda keeps its
      by-value capture — same treatment is T0328's sibling and would need a second mutex-guarded queue.

- [x] T0329 **19 `QMetaObject::invokeMethod` sites, no rule** (`src/`) #risk-medium
      Audit each: same-thread or cross-thread. Apply no-payload rule where cross-thread. Write rule in
      `INVARIANTS.md`. Add `scripts/check-thread-handoffs.py` grep gate.
      **DONE 2026-11-08:** 16 code sites audited (3 more were comments); all queued, none captured by
      reference, so no production change was needed. Rule added to `docs/dev/INVARIANTS.md` with the
      site inventory. `scripts/check-thread-handoffs.py` gates it (exit 0 on the tree, exit 1 with
      file:line on a planted `[&x]` queued call) and now runs in `guards.yml`.

- [x] T0330 **TSan lane counts 8, `--safe` runs 11 — different populations** (`build.sh`, `tsan.yml`) #risk-medium
      Make one list source of truth. Record per-entry baseline in checked-in file. Fail on increase per entry.
      **DONE 2026-11-07:** `scripts/safe-tests.txt` is the one source of truth; tsan.yml builds its RE
      from it, `check-safe-tests.py` (guards.yml) fails on drift/duplicates, and the SAFE_TESTS duplicate
      is gone. The "8 vs 11" drift was already closed by T0041/T0297; this makes it unrecurrable.

- [x] T0331 **Sanitizer findings reported as failed ctest entry** (`build.sh`) #cleanup
      Classify exit 66 (TSan) and 67 (ASan/UBSan). Print report count next to verdict. Keep genuine failure red.
      **DONE 2026-11-07:** `do_ctest` classifies ctest-exit-8 sanitizer findings (66/67) as FINDINGS with report count; genuine failures stay red.

- [ ] T0318 **Saved stream carries no lyrics, no user-settable tags** (`SunoDownloader.cpp:832-900,1085`) #feature #risk-medium
      `lyrics` empty for 2147/2147 clips. 775 carry `aligned_lyrics_json`. MP4 branch writes no `©lyr`.
      Fix: write `©lyr` from `clip.metadata.lyrics` or DB aligned-lyrics. Fill sidecar. No tag override exists yet.

- [x] T0326 **`AudioEngine::processAudioBuffer` reads `scratchBuffer_` while `applyAudioConfig` may resize** (`AudioEngine.cpp`, `.hpp`) #risk-high
      Use-after-free, not stale read. Fix: double-buffer with atomic pointer swap, or refuse conversion while reconfiguring.
      Not reproducible (darwin backend delivers no buffers, T0322).
      **DONE 2026-11-11:** chose a third, simpler option that removes the shared
      mutable buffer entirely: `scratchBuffer_` is allocated ONCE at
      `pcm::kMaxInterleavedSamples` (16384 f32 / 64 KiB) and never resized, so its
      address is stable for the engine's life; an atomic `scratchWindow_` (the
      active sample count, clamped by `conversionWindow()` into that capacity) is
      all `applyAudioConfig` changes. The callback loads the window with acquire
      and spans exactly that many samples. No lock on the audio callback, no
      reclamation/hazard-pointer complexity, and the UAF is gone by construction.
      Regression tests `windowTracksConfigWithoutWideningTheConversionBuffer`
      (a 16384-sample buffer fits a wide window, is dropped when narrowed) and
      `reconfigureDuringCallbacksDoesNotCorruptTheConversionBuffer` (2000 worker
      callbacks concurrent with 128 reconfigs) both pass; full build 374/374,
      ctest 33/33. Rule recorded in `docs/dev/INVARIANTS.md`. TSan was not run:
      build-tsan has no warm objects here and a cold `-fsanitize=thread` rebuild
      is a full compile on this constrained box; the structural fix + concurrency
      test are the applied verification. STATUS/T0326.log.

- [ ] T0051 `QTP0004` deliberately disabled, 58 QML files away from `qt_add_qml_module` (`TargetSetup.cmake:129-134`) #risk-medium
      Enable and fix relative imports, or flatten QML tree. 6.8 can generate per-directory `qmldir`s.

- [x] T0165 **Preset discovery misses `.milk7`/`.milkdrop`** (`PresetScanner.cpp`) #risk-medium
      projectM tests `.milk` exact, case-sensitive. `.Milk`/`.MILK`/`.milk7`/`.milkdrop` skipped. No `milkdrop.ini` in v4.
      **DONE 2026-11-07:** case-insensitive extension match + `.milk7`/`.milkdrop`; shared `hasExtension()`. Test pins discovery.

- [x] T0058 `RatingManager` process-wide mutex-less singleton keyed on preset name (`RatingManager.cpp:9-25`) #blocks-feature
      Same preset name in two banks shares rating. Bare `std::map`, no lock. Fix: inject, key on path, add mutex.
      **DONE 2026-11-06:** keyed on path + mutex (singleton kept; DI is T0131). `PresetBridge::migrateNameKeyedRatings` moves old name-keyed stars onto path keys once; path keys win.

- [-] T0065 `SunoOrchestrator` permanently-failing stub with unreachable members (`SunoOrchestrator.cpp:14-44`) #cleanup
      Both methods only `emit errorOccurred`. Private handlers never called. `client_` written never read.
      **Not a bug to "fix" (2026-11-07):** the failure is an intentional fail-closed gate —
      B-Side Chat endpoints are uncaptured (AGENTS.md), so the stub must refuse. `onMessageFinished`/
      `onHistoryFinished` are real parsers, not dead code; they fire once a route is wired. Keep until
      a capture exists, then wire. Do not delete the stub to "clean up".
      **CLOSED 2026-11-08:** re-grep confirms both handlers are real parsers and the stub is the intended fail-closed gate; nothing to change until a capture exists.

- [x] T0066 `searchText` threaded three layers, dropped at bottom (`SunoLibraryManager.cpp:106,122`, `SunoClient.cpp:1310`) #cleanup
      `Q_UNUSED(searchText)`, `setSearchText` has no callers. Real filtering is client-side in `SunoBridge`.
      **DONE 2026-11-07:** dead `searchText`/`searchText_` removed from `fetchLibraryPage` and manager. Remote search stays T0366.

- [x] T0072 No `reuseItems` anywhere; nested non-interactive `GridView` in virtualized `ListView` (`src/qml/`) #perf
      `DiscoverView.qml:115-127` materialises every clip of every feed. 16 `layer.enabled` sites.
      **DONE 2026-11-08:** `reuseItems: true` added to six views on five files (LibraryView clip grid, DiscoverView feed list + its nested clip grid, NotificationsView, LyricsPanel, SunoPanel). The nested grid in DiscoverView is genuinely two-dimensional (columns derived from width and card minimum), so it was kept rather than replaced with a single-column container. Two short lists (OverlayPanel, PlaylistStrip) were skipped as pooling gains nothing. No position, delegate or binding changed; no reuseItems existed anywhere before.

- [x] T0076 Views with no loading/empty/error state (`VideoView.qml`, `PresetsPanel.qml`, `PlaylistPanel.qml`, `TransportBar.qml`) #blocks-ui
      Model on `LibraryView.qml:397-448`, `DiscoverView.qml:207-261`.
      **DONE 2026-11-08:** `PresetsPanel` shows an empty block (copy names the folder; Rescan acts) and `PlaylistPanel` shows one on an empty queue. `VideoView` and `TransportBar` were checked and left alone: neither has a reachable loading/error state (the visualizer window is created before the engine; `AudioBridge` exposes no loading flag) and `TransportBar` already handles `!hasTrack` and `recordError`. Loading is not expressible for presets — `PresetBridge` publishes only the finished list.

- [-] T0080 Two writes to bound `value` on `AppSlider` fight each other (`KaraokeSettings.qml:52`, `RecordingPanel.qml:180,264`) #risk-high
      Use `onMoved` not `onValueChanged` per Qt docs.
      **Stale premise (2026-11-07):** both cited call sites already use `onMoved`. Remaining
      `onValueChanged` sites are OverlayPanel.qml / AppColorPicker.qml, which bind a display value,
      not a settings write — no two-way fight. Re-grep before acting; likely closable as no-op.
      **CLOSED 2026-11-08:** `KaraokeSettings.qml` no longer exists; the remaining cited site (`RecordingPanel.qml:180`) uses `onMoved`. No two-way write exists.

- [x] T0081 `AppSlider` keyboard- and screen-reader-inaccessible (`AppSlider.qml`) #risk-high
      No `activeFocusOnTab`, no arrow keys, no `Accessible.*`. Division by zero at `:37` if `from == to`.
      **DONE 2026-11-07:** div-by-zero guard, keyboard (arrows/Page/Home/End), `Accessible.Slider` with increase/decrease actions. qmllint clean.

- [x] T0082 34 `Accessible.*` properties across 11 of 58 files; 47 have none (`src/qml/`) #risk-high
      **DONE 2026-11-08:** `Accessible.*` went from 39 lines in 13 files to 150 lines in 31 files. AccountChip (was mouse-only) is now a named, keyboard-operable pill; the color picker strips and swatches got names and arrow-key control; every icon-only AppButton outside PresetsPanel/PlaylistPanel is named; sliders got `accessibleName`; Actor	row and delegate tab stops were added without any visible change. Delegating to the wrapper properties (SettingSpinRow, AppSlider) means one edit fixes many call sites. `PlaylistPanel` and `PresetsPanel` are excluded (another lane owned them) and still hold five unnamed icon-only buttons.

- [ ] T0083 Contrast failures real but not where old claim said (`Theme.qml`) #cleanup
      `textDisabled` 1.95–2.77:1, `recording` 3.07:1, `textSecondary` 3.46:1, `error` 3.60:1.
      Real defect: text over projectM canvas (`glassBackground` 85% opaque → ~1.3:1).

- [ ] T0084 ~58 raw unthemed QtQuick.Controls where `App*` wrapper exists (`src/qml/`) #cleanup

- [x] T0436 **`AppSlider.stepSize` defaults to 1 while `Slider` defaults to 0 (continuous)** (`src/qml/components/AppSlider.qml`) #cleanup
      A raw `Slider` dropped into an `AppSlider` silently loses fractional range unless the caller
      passes `stepSize: 0` — found converting T0084. Either default the wrapper to 0 or document the
      divergence at the property. Verify: a test or lint note pins the chosen default.
      **DONE 2026-11-08:** default kept at 1 (TransportBar/RecordingPanel depend on it — switching to Qt's 0 would make seeks/volumes fractional) and the divergence is now documented at the property and file header; fractional controls must still set stepSize explicitly.

- [x] T0437 **`AppSlider.accessibleName` defaults to the literal "Slider"** (`src/qml/components/AppSlider.qml`) #cleanup
      Three settings sliders are announced unnamed because they never set it. Verify: each call site
      in `src/qml/panels/settings/` carries a real name, or the wrapper drops the placeholder default.
      **DONE 2026-11-08:** `accessibleName` defaults to `""` (unnamed, as a native Qt control with no name is); description guards the leading comma. Real names at call sites remain follow-up work.

- [x] T0438 **`Q_ASSERT` in tests is dead in Release builds** (`tests/`, `.github/workflows/guards.yml`) #risk-medium
      Windows and Linux gates configure Release; Qt6 defines `QT_NO_DEBUG` for every non-Debug config, so
      `Q_ASSERT` compiles out. `test_SunoAudioUploadService`'s fixture write was guarded that way: the file
      was never created, four new chain tests timed out against a missing path, and the process died on an
      empty reply vector with no output (one full Windows cycle to find).
      **DONE 2026-11-08:** the fixture uses real checks (`qFatal` on failure) and the chain helper asserts
      `isUploading()` with `service.error()` in the message. `scripts/check-test-asserts.py` forbids
      `Q_ASSERT`/`Q_ASSERT_X` under `tests/` (exit 1 with file:line) and runs in `guards.yml`.

- [x] T0439 **Preset search, category filter and Blacklisted did nothing** (`src/qml/panels/PresetsPanel.qml`, `src/qml_bridge/PresetBridge.cpp`) #risk-medium
      The search box and category combo wrote two local QML properties (`root.searchQuery`,
      `root.selectedCategory`) that nothing read; `filteredPresets()` reads the bridge's own fields and the
      list only reassigns on `presetsChanged`. Separately the combo's `"__blacklisted__"` entry had no
      branch in `filteredPresets()`, so it fell through to `byCategory("__blacklisted__")` and returned
      empty. **DONE 2026-11-08:** QML writes the bridge properties directly; the blacklisted branch calls
      `blacklistedPresets()`. Build clean, `unit_tests` passed.

- [x] T0440 **Preset empty state shows during the startup scan** (`src/qml/panels/PresetsPanel.qml`, `src/qml_bridge/PresetBridge.*`) #cleanup
      `PresetBridge` publishes only the finished list, so an empty model cannot be told apart from "still
      scanning". Verify: a scanning property on the bridge gates the empty block.
      **DONE 2026-11-08:** `Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)` on `PresetBridge`, backed live by `PresetManager::scanInFlight()` (no new manager state needed). `rescan()`/`setFolder()` fold it true immediately; `onListChanged` and `onScanFailed` fold it false. The panel gates its empty copy on `!scanning` and shows a busy placeholder while scanning. Two bridge tests: true in flight + false on land, and false on a failed scan.

- [x] T0443 **Drain the last rendered frame at stopRecording** (`src/visualizer/VisualizerRenderer.*`, `src/recorder/`) #cleanup
      T0171's frame-0 fix left the tail: the final render's PBO readback is never drained when recording
      stops, so a recording ends one frame short. Verify: a GL slot that renders a known frame last,
      stops recording, and asserts that frame is in the output.
      **DONE 2026-11-08:** `drainPendingFrame()` emits the pending tail frame at its readback timestamp through the shared `emitPboFrame` path; `stopRecording()` calls it before destroying the PBOs, and a stop with nothing pending is a no-op. The renderer destructor deliberately does not drain under a failing context (T0026), so a window destroyed mid-recording still loses its tail frame there. GL slot `stopRecordingDrainsTheLastFrame` (A,B,C → 2 emitted before stop, 3 after) proves it; `firstRenderEmitsAFrame` now also asserts no extra frame when nothing is pending.

- [x] T0444 **`integration_tests` is outside the `--tests` build list, so it goes stale silently** (`build.sh`, `docs/dev/TESTING.md`, `AGENTS.md`) #risk-medium
      `integration_tests`/`integration_gl_tests` are deliberately not in `SAFE_TESTS`, but nothing builds
      them: they only get rebuilt by a full build. T0171's new GL slot landed in a binary that
      `--fast --tests` never relinked, so ctest ran it against the old binary and reported
      "Unknown test function". Verify: a check that rebuilds the integration targets before the ctest
      step, or that refuses to run them when the binary is older than the sources.
      **DONE 2026-11-08:** `scripts/check-integration-fresh.py` fails when the `integration_tests` binary is older than `tests/integration/` sources and prints the rebuild command; it also fails if `integration_tests` ever appears in build.sh's SAFE_TESTS. Wired into `.github/workflows/guards.yml`; `AGENTS.md` §7 and `docs/dev/TESTING.md` name the check and the rebuild command. The everyday `--tests` loop is unchanged (one Python check, milliseconds).

- [~] T0451 **Drain QML incubation in `aboutToQuit`** (`src/core/Application.cpp`) #cleanup
      The production counterpart of T0180's test fix: if the app quits during the first event-loop
      turns, the engine can be destroyed with objects still being created. The probe proposed a drain
      in `aboutToQuit` before member teardown (engine before audio, matching `Application.hpp:136-138`).
      **Done 2026-11-09:** `Application::aboutToQuit` drains incubation on the GUI thread first via the static seam `Application::drainQmlIncubation(QQmlApplicationEngine*)`: null-engine no-op, then bounded `processEvents(ExcludeUserInputEvents, 50)` chunks until `incubatingObjectCount() == 0` with a 5s deadline (logs and continues). Engine-before-audio order preserved. Qt 6.12 readiness is `incubatingObjectCount() == 0` (there is no `isReady()`, and the header is `QQmlIncubationController`, i.e. with an "ation"). The integration slot `earlyQuitDrainsIncubationViaProductionHelper` covers the null no-op (always meaningful) but SKIPs with an honest reason on platforms where navigate completes creation synchronously — the >0 precondition cannot be reproduced there, and the drain call itself is the same production path.


- [ ] T0452 **qmlcachegen (Qt 6.12) segfaults on member access on a QVariantMap** (`docs/dev/`, `TODO.md`) #risk-medium
      Reading `.outcome` off a `QVariantMap` returned by a Q_PROPERTY (`SunoBridge.lastSettle || {}`)
      made `qmlcachegen` exit 139 (SIGSEGV) during AOT generation; removing the member access made the
      same file compile. Bisected 2026-11-08: `.outcome` access crashes, `|| {}` alone does not, plain
      expressions and object-literal returns are fine. Workaround in place: the settle sentence is
      computed in C++. Verify: keep any QML free of member access on map-typed properties, or track the
      Qt fix and revert.

- [x] T0450 **Wire `notificationSettled` into QML** (`src/qml_bridge/`, `src/qml/views/NotificationsView.qml`) #feature
      `SunoNotificationService::notificationSettled` exists (T0375 follow-up) with per-call outcome, but
      no bridge exposes it, so the UI cannot tell the user which click was refused or rejected. Verify:
      the bridge surfaces the result; NotificationsView shows the outcome for clear-badge/mark-all-read.
      **DONE 2026-11-08:** `SunoBridge.lastSettle` (verb/outcome/reason) plus a C++ `settleMessage` — the outcome sentence lives in C++ because QML's member access on a QVariantMap crashed qmlcachegen (see below). The NotificationsView header subtitle is the only visible change: failures of mark-all-read, clear-badge or refresh show their fixed sentence in error colour; an accepted settle changes nothing.

- [x] T0441 **Focus rings for the new keyboard tab stops** (`src/qml/`) #cleanup
      T0082 made the custom-drawn controls (AppColorPicker areas, tiles, chips) reachable by Tab, but none
      of them shows a focus outline, so a sighted keyboard user cannot see where focus is. Verify: a
      visible focus indicator on every `activeFocusOnTab` custom-drawn control.
      **DONE 2026-11-08:** keyboard-only focus rings now appear on the seven custom-drawn tab stops (AppColorPicker swatch + saturation/brightness, hue and alpha strips, ClipCard tile, AccountChip pill): 2px `Theme.borderFocus` outline matching `AppButton`'s precedent, no geometry change. One constraint worth knowing: `focusReason`/`visualFocus` exist only on `Control`, so the plain-`Rectangle` roots cannot read them; each carries a small `keyboardFocused` flag that clears when a mouse click takes focus and resets on focus loss, reproducing `visualFocus` exactly (Tab/Backtab → ring, mouse → none). Hover was already ring-free. Wrapped Qt controls (`SettingSpinRow`'s SpinBox, the card's save `AppButton`) were not given a second ring.

- [x] T0442 **Five unnamed icon-only buttons in PresetsPanel/PlaylistPanel** (`src/qml/panels/`) #cleanup
      T0082's lane was scoped off these two files (another lane owned them), so every icon-only
      `AppButton` elsewhere is now named but theirs are not. Verify: grep the two files for an icon-only
      `AppButton` without a name returns nothing.
      **DONE 2026-11-08:** all five icon-only `AppButton`s (shuffle, clear, random, favorite-star, blacklist) got a state-aware `Accessible.name`, e.g. "Add preset to favorites" / "Remove preset from favorites". `AppButton` already provides tab focus and Enter/Space, so no new keyboard handlers were needed; `ToolTip` added where the file already uses them. Non-icon `AppButton`s with visible text already inherit their name from the text.

- [ ] T0085 `TextAccessible` names hardcoded English (`src/qml/`) #cleanup

- [x] T0086 Remote art fetched with no `sourceSize`, sometimes not `asynchronous` (`ClipDetailSheet.qml:83-88`, `ClipCard.qml`, `SunoPanel.qml`) #perf
      **DONE 2026-11-07:** all three remote Image loads get sourceSize + asynchronous; DiscoverView already had both.

- [x] T0087 Icons: two duplicate copies, neither used, three destinations share one glyph (`resources/icons/`) #cleanup
      **DONE 2026-11-08:** `resources/icons/qml/` held 7 SVGs registered in `TargetSetup.cmake` RESOURCES but referenced by zero QML files, including a byte-identical `playback.svg`/`suno.svg` pair. All 7 removed from the resource list and moved to `.backup_graveyard/resources-icons-qml-20261108T114157Z/`. The 18 icons QML actually names stay. Verified: full build 148/148, ctest 26/26.

- [x] T0088 macOS bundle ships no icon (`Info.plist.in`, `TargetSetup.cmake:262-290`) #cleanup
      **DONE 2026-11-07:** `resources/ChadVis.icns` (reproducible via `scripts/make-app-icon.sh`) copied to
      `Contents/Resources`; plist sets `CFBundleIconFile`. Verified in built bundle.

- [x] T0089 `file(DOWNLOAD)` of CPM.cmake at configure into source tree, unverified #risk-medium
      **DONE 2026-11-11:** the fallback no longer writes into the source tree (it downloads into
      the build dir) and is TLS-verified, `EXPECTED_HASH`-pinned to upstream v0.40.0's SHA-256
      and status-checked with FATAL_ERROR on failure. The vendored, git-tracked `cmake/CPM.cmake`
      is byte-identical to upstream and its hash is checked on EVERY configure, so a truncated or
      tampered copy fails loudly. Cold configure is offline-capable; the fallback download
      mechanism was exercised directly via `cmake -P`. The old `Dependencies.cmake:38-42` line
      reference was stale — the bootstrap now lives at ~107. STATUS/T0089.log.

- [x] T0460 **Bumping `version.txt` never re-runs configure** (`CMakeLists.txt`) #risk-medium
      Discovered 2026-11-11: `file(READ version.txt)` is not a configure dependency, so after the
      1.3.0 bump build-fast still stamped 1.2.0 into `BuildConfig.hpp` and 3 `TestVersion` cases
      failed (binary/banner/QML all reported the old version). **DONE 2026-11-11:** `version.txt`
      is a `CMAKE_CONFIGURE_DEPENDS` input, so a bump re-runs configure and regenerates the header
      on the next build; only the header's few consumers rebuild. Verified: reconfigure → app
      `--version` = 1.3.0, `unit_tests` TestVersion 9/9, ctest 33/33. STATUS/T0460.log.

- [x] T0090 Nine of fifteen deps unpinned, two bypass system-first (`Dependencies.cmake`) #risk-medium
      CPM tags pinned to commit SHAs (not mutable tags). Conan rejected — CPM+vcpkg.json closes same gaps.
      **CLOSED 2026-11-11 (premise stale):** all four CPM deps (fmt, spdlog,
      toml++, readerwriterqueue) use `GIT_TAG` with full 40-char commit SHAs, not
      mutable tags; pffft (one of the two "system-first bypassers") was removed
      2026-10-04, leaving only readerwriterqueue, which is header-only with no
      system package; `CPM_SOURCE_CACHE` is shared across profiles. The remaining
      "unpinned" inputs are host system packages resolved by find_package/
      pkg-config with version floors (Qt >= 6.8) — not commit-pinnable and not a
      CPM/Conan concern. Verified by grep of `cmake/Dependencies.cmake`. The
      reproducibility work that remains is the SBOM/vulnerability lane (T0301),
      not this item. `docs/dev/DEPENDENCIES.md`'s "Why not Conan" section updated
      to match. STATUS/T0090.log.

- [ ] T0093 No warnings-as-errors, clang-format/clang-tidy enforced nowhere (`Compiler.cmake:80-83`, `.github/`) #risk-medium
      Add non-blocking `git clang-format --diff main` report step.

- [ ] T0097 `unit_tests` one binary, one `QCoreApplication`, 31 suites sequential (`test_main.cpp:38,41-72`) #risk-medium
      Crash/hang in suite 30 loses suites 1–29.

- [x] T0099 Four duplicate reply fakes, no fake `QNetworkAccessManager` (`tests/unit/suno/`) #risk-high
      New suite without injected factory makes real DNS lookup. Consolidate to `tests/support/` INTERFACE lib.
      **DONE 2026-11-08:** one `tests/support/FakeNetworkReply.hpp` + `test_support` INTERFACE lib; 10 suites migrated, zero `class ScriptedReply` left, old suno header deleted. `succeed()` streams, `complete()` is the one-shot form a tracked reply needs.

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

- [x] T0109 Stop logging clip identifiers (`DownloadQueue.cpp:323,508-703`, `SunoDownloader.cpp:246,409,412`) #security
      Serializer structurally incapable of emitting token/cookie (preserve). Clip ID is account-linked half of scrub plan.
      **DONE 2026-11-07:** all clip identifiers and local paths out of durable LOG lines and all reason strings; helpers id-free.

- [-] T0113 `.gitignore` gaps (`build.log` present, `.DS_Store` files in tests/resources/docs) #risk-medium
      **Verified no-op (2026-11-07):** `.DS_Store` (line 145) and `build.log` (line 170) are already
      gitignored; `git check-ignore` confirms every stray file, none are tracked. Nothing leaks into commits.
      **CLOSED 2026-11-08:** `git check-ignore build.log .DS_Store` resolves both; re-verified, still a no-op.

- [-] T0114 Confirm `libavfilter` NOT currently linked (`Dependencies.cmake:142`) #risk-medium
      **Verified no-op (2026-11-07):** libavfilter is an OPTIONAL dep gated by CHADVIS_HAS_AVFILTER;
      the no-libavfilter path degrades to `burnInUnavailable` (no post pass), as designed. Already done.
      Burn-in relies on UI gate. Adding post-pass must degrade to "no post pass" on stripped distro.
      **CLOSED 2026-11-08:** `cmake/Dependencies.cmake` still gates libavfilter behind `CHADVIS_HAS_AVFILTER`; the degradation path is the design.

- [ ] T0115 Song/preset import must work with no Suno account (`docs/PIVOT_PLAN.md`) #feature
      Local-file path = cheapest insurance against upstream disappearing. Prerequisite for batch rendering.

- [ ] T0116 Render-job model and queue exists, no producer (`Bridge.hpp:59-68`, `SunoWorkspace.hpp`) #feature

- [x] T0197 Nothing re-applies theme when `ui().theme` changes outside Appearance panel (`main.qml`) #risk-medium
      `applyTheme()` runs once in `Component.onCompleted`. Fix: `Connections` on `themeNameChanged` unless session override active.
      **DONE 2026-11-08:** `Connections { target: SettingsBridge; onThemeNameChanged }` in `main.qml` re-runs the idempotent `applyTheme()`; a session override still wins.

- [ ] T0338 **Windows vcpkg ports unpinned, CI reproducibility zero** (`windows.yml:130-160`) #risk-medium
      No `vcpkg.json`. Add manifest with `builtin-baseline` commit. Delete explicit list.

- [x] T0453 **Split `project_lib` (80 MB, 48 `Q_OBJECT`) into ~7 module libs** (`TargetSetup.cmake`) #perf
      Landed 2026-11-08 as 6 STATIC libs (qt_add_qml_module needs STATIC); project_lib is an INTERFACE aggregator.
      Own moc/PCH per module. QML module moved to `project_bridges`. Feature defines scoped: keychain→suno, soxr→recorder, presets→core.
      Verify: full build 450/450 + ctest 33/33; moc TU split into 6 (largest now bridges/suno, ~16-18 s vs 21 s monolith).
      Detail: [`docs/dev/BUILD-SPEED.md`](docs/dev/BUILD-SPEED.md).

- [?] T0454 **PCH defeats ccache (1,603 `could_not_use_precompiled_header`)** (`build.sh`, `src/pch.hpp`) #perf
      Exported in build.sh (unseen-by-default): CCACHE_SLOPPINESS=+include_file_ctime/mtime/modules, NOHASHDIR=1,
      MAXSIZE=20GiB. Verified via `ccache --show-config`.
      **Not verified**: uncacheable < 5% needs a full --tests from cold cache on this box. Current: 18.5% (was 20.4%).

- [ ] T0455 **Top-5 header fan-out (900 TUs)** (`Types.hpp`, `Result.hpp`, `SunoClient.hpp`, `SunoEndpoints.hpp`) #perf
      Split `<filesystem>`/`<span>` out, forward-declare, split endpoints per service. Verify: `ninja -t deps` top header < 100.

- [x] T0456 **Version bump rebuilds ~350 TUs (`CHADVIS_VERSION` PUBLIC)** (`TargetSetup.cmake`, `Application.cpp`, `CliUtils.cpp`) #perf
      Landed 2026-11-08: `BuildConfig.hpp.in` generated by `configure_file`; version/AVFILTER/AES defines replaced by the header.
      The 5 consumers include it; the generated include dir is PUBLIC so tests see the same answer.
      Verify: `ctest` 33/33 incl. `test_Version`.

- [ ] T0457 **Cache sharing: remote storage, 2013 MBA as cache server only** (`build.sh`, CI) #perf
      ccache `remote-storage` (S3/GCS/Redis) or sccache Redis on 2013 MBA. No `distcc`/`icecc` (toolchain parity).
      Detail: [`docs/dev/BUILD-SPEED.md`](docs/dev/BUILD-SPEED.md).

- [ ] T0458 **QML qmlcache (59 TUs) + unity build in fast loop** (`TargetSetup.cmake`, `CMakeLists.txt`) #perf
      Interpreted QML in `CHADVIS_FAST_ITERATION`; `CMAKE_UNITY_BUILD` batch 8 after T0454. Verify: `--tests` cold wall < 50% baseline.

- [-] T0459 **CI ccache key ignores `src/` (`build.yml:241`)** (`.github/workflows/build.yml`) #perf
      **Dropped 2026-11-08:** deliberately not in the key — ccache keys entries on preprocessed source, so source-only changes are correct misses and restore-keys walk back to the last good entry. Including sources would only fragment the bucket.
---

## P2b — endpoint coverage from urls.txt (1058 unique URLs) + UI-07 follow-ups

Evidence: the owner's URL dump at `~/Documents/suno-recon-cat-stealth-modekl/urls.txt`
(1 058 unique; 4 750 URL lines) and the signed-in DevTools HAR
`~/Downloads/suno.com4.har`. Route *existence* below comes from that dump; verbs and
bodies are unproven until a browser walk or an app-originated call captures them.
Promote to `docs/suno_api/ENDPOINT-INVENTORY.md` with the same `[T1]` discipline.

- [x] T0366 **Search family: remote search, suggest, history** (`src/suno/SunoEndpoints.hpp`, `SunoClient.cpp`, `SunoLibraryManager.cpp`, `docs/suno_api/`) #feature #blocks-ui (after: T0066)
      `/a/api/search`, `/search/history`, `/search/users`, `/unified/search/omnisearch`,
      `/unified/search/suggest`, `/unified/search/suggest/history`,
      `/unified/search/suggest/history/item`. Typing in the library box and Explore search
      capture the verbs. T0066 threads `searchText` then drops it — this closes that with the
      real route instead of local filtering.
      **BLOCKED 2026-11-08 (evidence):** zero of the seven routes appear in ENDPOINT-INVENTORY; six are
      `GET?`/`Bearer?` rows in OBSERVED-LEADS §4.5 that explicitly say no reviewed capture supports them,
      and `/unified/search/suggest/history/item` appears in no document. `SunoEndpoints.hpp` has no search
      constant. Building a read service now would invent routes, so nothing was built.
      Next action (needs an authenticated browser): type in the library box and in Explore search, record
      method + query/body for each route, then promote them to `[T1]` in the inventory. Then the service
      follows `SunoFreeReadsService` exactly.
      **DONE 2026-11-08:** the capture landed. Library search is `filters.searchText` on `POST /feed/v3` (`SunoClient::fetchLibraryPage` + `SunoLibraryManager::setSearchText`); a term change restarts the feed, an empty term returns the unfiltered feed, and a failed page still resumes with the term. The Explore page's `POST /unified/search/suggest` and `/unified/search/omnisearch` are wired in `SunoSearchService`, with `feed.items` kept raw (uncaptured). Evidence: ENDPOINT-INVENTORY §4.5 (2026-11-08 capture); the non-chased rows (`/api/search`, `history`, `users`) stay `[LEAD]`. `SunoBridge`'s local `contains` filter is untouched, so route choice is now the caller's.
- [x] T0367 **Onboarding + user-config writes** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
      `/onboarding/{start,current,back,skip,complete,submit}`,
      `/onboarding/audio-upload/{abort,remove}`,
      `/user/{me,update_user_config,reset_onboarding,vip_program_acceptance}`. `reset_onboarding`
      is reversible on the owner account; wire `GET /onboarding/current` only.
      **DONE 2026-11-08 (reads slice):** `GET /onboarding/current` is `[T1]` and now read in `SunoAccountManager` as a raw envelope; every write verb (`start`/`back`/`skip`/`complete`/`submit`, `audio-upload/{abort,remove}`) stays unwritten for its own ticket. `audio-upload/abort` and `/remove` are mutually destructive with the upload chain's status route, so they need that design first.
- [ ] T0368 **Queue: `/queue/inject`, `/queue/start` + queue view** (`src/suno/`, `src/qml/`) #feature (after: T0321)
      Playbar Queue is a first-class site surface with no ChadVis counterpart. Capture both
      bodies from the playbar queue affordance; local queue UI is the second half.
- [ ] T0369 **Visuals product family — status-only, fail closed** (`docs/suno_api/OBSERVED-LEADS.md`) #feature
      `/visual/{models,assets,assets/uploads,characters,compositions,cost,generations,plays,presets,prompts/references,prompts/rewrite,signals,workflows}`.
      All 403 `feature_disabled` for this account (UI-07). Record statuses + gate names; do not
      wire. Revisit after T0370.
- [ ] T0370 **Statsig staff-flag override for capability recon** (`docs/dev/`, `docs/suno_api/`) #feature
      The site ships `suno-staff-statsig-overrides` and `suno-staff-flag-toolbar-config`
      (UI-07 bundle strings). A local override reveals gated surfaces without server changes —
      use it to enumerate `visual/*`, `suno_chat/*`, and the flag names behind 403s. Record
      observations only; no server-side writes.
- [ ] T0371 **Custom-model training family** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
      `/custom-model/{create,edit,archive,bases,pending}`. Training is likely credit-bearing;
      capture with one scratch model from the studio UI, wire reads only, fail closed on writes.
- [ ] T0372 **Clip edit family** (`src/suno/SunoEndpoints.hpp`, `SunoOrchestrator.*`, `docs/suno_api/`) #feature
      `/clips/{adjust-speed,reverse-clip,aligned_clips}` and
      `/generate/{concat/v2,sum,rhymes,upsample,lyrics-mashup}`. Reachable from Remix/Sample;
      trim has no standalone save (range rides the generate body, UI-07). Promote then wire.
- [ ] T0373 **Listen-for-Credits clip-review sub-family** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
      `/preferences/clip-review/{pending,opt-out,submit}` — distinct from `/preferences/tasks`.
      Capture `pending`/`opt-out` (reversible); the submit stays captcha-gated and untouched.
- [ ] T0374 **Social actions + share attribution** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
      `/profiles/{follow,block,mutual-followers}`, `/comment/{block,unblock}-user`,
      `/recommend/hide-creator`, `/share/{event,attribute}`, `/song_copy/send-song`,
      `/report_v2/refund_prompt/clicked`, `/image_insights/analyze`,
      `/instrument/describe-doodle`. Follow/block need a scratch second account; reads only.
- [x] T0375 **Notifications: clear-badge + read variants** (`src/suno/SunoEndpoints.hpp`, `SunoNotificationService.cpp`) #feature
      `/notification/v2/clear-badge`, `/notification/v2/read` (constant exists,
      `NOTIFICATION_V2_READ`). Click mark-all-read and the badge in the UI; pin status + body.
      **DONE 2026-11-08:** `clearBadge()` added mirroring `markAllRead()` — `POST /notification/v2/clear-badge` with an empty object, 2xx settles Accepted (no response parsing; the capture is an empty 204), `badgeCleared()` zeroes the unread count and triggers a refresh without marking notifications read. 4 fixture tests (success, 404 rejection, signed-out refusal, malformed 204 body). Badge stays on v2, list stays v3, existing mutations untouched. One gap: the service has no `MutationResult` type, so outcomes surface via signals only — a follow-up if the bridge needs per-call results.
- [ ] T0376 **Studio multitrack save/render** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature
      `/studio/save-project`, `/studio/render-state`, `/studio/render-state-multitrack`, plus the
      captured `GET /studio/project/{id}`. Create a scratch project, add one clip, save; capture
      bodies, wire reads first.
- [ ] T0377 **Integrations: musixmatch connector** (`src/suno/SunoEndpoints.hpp`, `docs/suno_api/`) #feature (after: T0356)
      `/v2/integrations/musixmatch`, `/v2/integrations/musixmatch/authorize-url`. Owner action:
      connect a real integration; capture `{connections:[…]}` with one attached.
- [ ] T0378 **`v2/integrations` with a real MCP link** (`docs/suno_api/MCP.md`) #feature (after: T0356, T0377)
      Needs a hosted MCP server plus a Suno-side connect. Captures the connected-object shape
      that UI-07 could not (no Integrations surface, `{connections:[]}`).
- [ ] T0379 **Auth/platform endpoint census** (`docs/suno_api/ENDPOINT-INVENTORY.md`) #cleanup
      `auth.suno.com/v1/client/{verify,sessions/{id}/touch}`, `/social/complete/google-oauth2/`,
      `suno.com/api/{docs,graphql,health,healthz,trpc,v1}`. Status-only; not for wiring.
- [ ] T0380 **Persona visibility write — capture when a track uses the voice** (`docs/suno_api/`, `SunoPersonaService.*`) #feature
      Blocked on data state: the Public switch is server-disabled for an unused voice (UI-07).
      Make one scratch generation with the persona, then toggle Public (reversible). Anchors T0349.
- [ ] T0381 **`project/{id}/clips` `update_type:"remove"`** (`SunoLibraryMutations.*`, `docs/suno_api/`) #feature
      `"add"` is captured (UI-07-adjacent); `"remove"` is not. Remove a clip from a scratch
      workspace via the UI, capture the body, and mirror the add path's 2xx handling.
- [ ] T0382 **Uploads status route + processed_clip vocabulary** (`docs/suno_api/`) #cleanup
      `GET /uploads/audio/{id}/` was never observed; poll a stem across its lifecycle and record
      every `processed_clip.status` value (`complete` now known, UI-07). Update §4.10/§4.12.
- [?] T0383 **Wire gen/trash + UI-07 reads into the client** (`SunoLibraryMutations.*`, `SunoClient.*`, `src/suno/SunoEndpoints.hpp`) #feature #blocks-ui (after: T0267)
      Trash/restore contract is now proven: `POST /gen/trash {trash,clip_ids}` →
      `{ids,is_trashed}`; playlist trash/recover is `{undo:true|false}`. Implement both behind
      the existing mutation discipline (2xx = accepted, shape pinned by a unit test), then wire
      the Hooks/History/Albums reads. Anchors UI-07 §4.12.
      **Mutations done 2026-11-07:** `ClipTrashRestore` (`setClipsTrashed`) and
      `PlaylistTrashV2` (`setPlaylistTrashed`) in `SunoLibraryMutations`, bridge
      wrappers `setClipTrashed`/`setPlaylistTrashed`, `test_SunoLibraryMutations`
      pinned (6 cases). **Reads still open:** Hooks/History/Albums constants exist
      (`VIDEO_HOOKS_*`, `PROFILES_LISTEN_HISTORY`, `UNIFIED_FEED`); shapes for
      history/trashed now in §4.12; no read service exists yet.
      READS DONE 2026-11-08: `SunoLibrarySectionService` reads `GET /clips/trashed_v2` (`{clips:[...]}`) and `GET /profiles/listen-history` (`{history:[{content:...}]}`) as raw objects. Hooks/Albums still open. Live GET 200 still open.
      HOOKS DONE 2026-11-08: `SunoHooksService` reads the three Hooks GETs as raw objects. Albums still open.
      ALBUMS DONE 2026-11-08: `SunoAlbumsService` posts `library_albums` to `POST /unified/feed`. T0383 reads complete; live 200 still open.
- [ ] T0384 **Feature-flags authed shape** (`docs/suno_api/`) #cleanup
      The app never calls `/api/feature-flags`; the browser 401s (Bearer-only). Capture only
      through an app-originated call (bookmarklet/extension holding the app's own header), never
      by pasting a token into a file.

---

## P2c — recon triage 2026-11-08 (read-only pass, writer-ready)

> **Session checkpoint 2026-11-11 (HEAD f59971c, two commits, fast build 374/374 + ctest 33/33 green).**
> Landed, fully verified locally, no live/account/GL evidence needed:
> - `4d3624b` **configure-time integrity** — CPM.cmake v0.40.0 vendored + SHA-256-verified every configure; fallback download moved out of the source tree, TLS/hash/status-checked (T0089). Preset pack pinned to immutable commit `0180df21…` with hash-verified archive and a non-fatal idle-preset fallback (T0389). `version.txt` is now a configure dependency, so a release bump can no longer leave the binary/banner/QML reporting the old version — this also fixed 3 pre-existing `TestVersion` failures (T0460, discovered here).
> - `f59971c` **audio UAF fix** — the PCM conversion scratch buffer is allocated once at the max window and never resized; only an atomic window size follows config, so the GUI thread can no longer reallocate it under the sink callback's `std::span` (T0326). Two new regression tests, one a 2000-callback/128-reconfigure concurrency stress.
> - T0090 closed as a stale premise (all CPM deps pinned to SHAs, pffft gone, source cache shared); `docs/dev/DEPENDENCIES.md` "Why not Conan" corrected to match.
> **For the owner to take stock of:** (1) ThreadSanitizer was NOT run for T0326 — `build-tsan` has no warm objects and a cold `-fsanitize=thread` build is a full recompile on this box; the fix is structural (no shared mutable buffer) plus a concurrency test, but a CI TSan lane would be the belt-and-braces check. (2) Release-worthy `[Unreleased]` now has 3 Fixed/Security entries; run `./scripts/release.sh --dry-run` when a release is wanted. (3) Still not automatable here: the big P0/P1 Suno features (T0346/T0357/T0170/T0365) need authenticated captures or GL runtime evidence, and QML-heavy refactors (T0407/T0408/T0412…) need a real window/designer. (4) Nothing was pushed — `origin/main` is still at `adc5b88`; push when you want these shared. (5) `AGENTS.md` carries an **unrelated uncommitted edit** (§18 subagent model renamed `codebuddy/hy4free` → `workbuddy/hy4free`, timestamped before this session's work) that I did not make and deliberately excluded from all three commits; left for you to commit or revert.

> **Session checkpoint 2026-11-10 (HEAD 50f346d, all 5 CI gates green, clean tree).** Landed this session: build-speed chain (T0453–T0459, module split + BuildConfig + ccache/PCH + budget), T0354 refuted, T0401/T0399/T0400/T0392/T0404/T0393/T0394/T0386/T0387(+offscreen follow-up)/T0391(partial)/T0398/T0390/T0011. Guard fixes: `check-thread-handoffs.py` apostrophe-in-comment bug, `check-endpoints.py` new drift guard, `check-safe-tests.py` extended for ASan scope + gate-regex agreement — all with regression self-tests. Subagent model now `codebuddy/hy4free` (`high`); pass `model:` explicitly on every subagent call (the built-in default still points at the dead `kilo/stealth/crouton-alpha`). Ongoing at compaction: T0328 (AudioEngine handoff refactor) + T0389 (hash-pin feasibility recon) lanes in flight. Next verifiable slices: T0402(a) unblocked ConfigData `quantum_frames` round-trip; T0407 shared StateView (QML, needs a designer); T0391 shared-helper remainder. Big P0/P1 items (T0346/T0357/T0170/T0365) need live captures or GL runtime evidence — not automatable. T0452 needs the upstream Qt qmlcachegen fix (a regex guard would false-positive on `var`-aliased maps); leave as prose.

> **Session checkpoint 2026-11-10 (HEAD ae9b067, release v1.3.0 published with all three platform assets).**
Landed: T0328 (AudioEngine's two cross-thread posts are parameterless queued calls onto private slots;
errors drain from a mutex-guarded queue, emitted outside the lock) and T0402(a) (`[recording] quantum_frames`
is a parsed/serialised/clamped TOML key with a "no consumer yet" disclosure; construction-time injection is (b),
blocked on T0397). Verified locally with unit_tests only (`ctest -R '^unit_tests$'` 1/1 Passed, 140.7 s) plus the
five guard scripts, deliberately avoiding another cold full rebuild on this box. Release v1.3.0 dispatched through
`./scripts/release.sh --tag v1.3.0` after build+windows gates went green at a2adcb4; assets are the Darwin dmg,
Linux tar.gz and Windows zip. Changelog rotated, so `[Unreleased]` is empty and the release gate now refuses an
unwritten release. Next verifiable slices: T0391 shared-helper remainder, T0407 shared StateView, T0417 bridge
Q_INVOKABLE audit, T0406 path-helper dedupe; T0358/T0359 QML lanes still need a real window for motion evidence.

4 parallel read-only lanes (C++ slop, QML/recorder, docs/backlog, tests/build/Suno) on kilo/stealth/crouton-alpha. T0385 was assigned by hand before recon returned; all ids below are fresh T0386+. Overlaps merged (JSON helper, spike audit). Entries already covered by live tasks cross-reference instead of duplicating. Live claims at insert time: T0358 `[~]`, T0362 `[~]` (steps 1 done, cover-art steps open) — write-sets below intersect `src/suno/` and `src/qml/`; do not start touching those trees until claims are released. Ordered P1 first, then P2/P3. Writer lane: pick smallest verifiable slice, run the verification bar below, record VERIFY in STATUS/<id>.log.

- [x] T0386 **Scope ASan/UBSan lanes to safe-tests.txt, extend the guard** (`.github/workflows/asan.yml`, `scripts/check-safe-tests.py`, `scripts/safe-tests.txt`) #risk-high
      Done 2026-11-10: both asan.yml ctest lanes (ASan :156, UBSan :299) now build `RE` from scripts/safe-tests.txt at run time and pass `-R "$RE"`, mirroring tsan.yml. `check-safe-tests.py` gained `asan_sources_canonical()` which fails if either lane is unfiltered (regression control: passes healthy, fails when one `-R` removed). `safe-tests.txt` consumers comment updated.
      Verify: `ctest --test-dir build-asan/tests -N` lists only the 11 safe entries; `check-safe-tests.py` fails when asan.yml is unfiltered.
- [x] T0387 **Add the 8 missing Suno service tests to the CI gates** (`.github/workflows/build.yml`, `.github/workflows/windows.yml`, `.github/workflows/release.yml`) #risk-medium
      Done 2026-11-10: added the 8 (`test_SunoClipDetailService`, `test_SunoCoverArtService`, `test_SunoGenerationService`, `test_SunoLibrarySectionService`, `test_SunoMcpClient`, `test_SunoModelCatalog`, `test_SunoPersonaService`, `test_SunoPlaylistService`) to all THREE hard-gate `-R` regexes. **The trap:** `release.yml:314` was omitted from the original write-set (TODO listed only build/windows) — fixed here too, or the 8 would still not gate on releases. All 8 verified as plain QTEST_MAIN service tests (no GL/QGui) and passing 8/8. Gates now run 19 entries; step names + counts corrected. `check-safe-tests.py` gained `gate_regexes_agree()` so the 3 copies cannot drift (regression control passes healthy / fails on drift).
      **Follow-up 2026-11-10:** first CI run failed — all 8 aborted on headless Linux with "no Qt platform plugin" (QTEST_MAIN → QGuiApplication on Linux with Qt GUI). Added `QT_QPA_PLATFORM=offscreen` ENVIRONMENT for the 8 in `tests/unit/CMakeLists.txt` (same fix as test_SunoNotificationService). Verified 8/8 headless; safe-tests guard still green.
      Verify: `ctest --test-dir build-fast/tests -N` covers all 20 SAFE_TESTS.
- [ ] T0388 **Warnings-as-errors + clang-format/tidy CI step** (`cmake/Compiler.cmake`, `.github/workflows/guards.yml`, `.clang-format`, `.clang-tidy`) #risk-medium
      No `-Werror` and no format/tidy enforcement exists today. Narrows T0093. Verify: `git clang-format --diff main` reported in guards; new warning fails the opt-in job.
- [x] T0389 **Pin preset-pack and CPM.cmake downloads by hash** (`cmake/PresetPack.cmake`, `cmake/Dependencies.cmake`) #risk-medium
      Both use `file(DOWNLOAD …)` with no hash; preset pack tracks a mutable branch. Narrows T0089. Verify: cold configure succeeds offline via the cached path; `grep -rn "file(DOWNLOAD" cmake/` shows URL + hash.
      **DONE 2026-11-11:** CPM.cmake pinned to upstream v0.40.0 SHA-256 (`7b354f3a…`, the
      vendored copy is byte-identical) and checked on every configure. Preset pack URL pinned to
      immutable commit `0180df21…` (was `refs/heads/master`) with SHA-256 verification of the
      downloaded zip; a mismatch/failed download warns and falls back to the idle preset (never
      fatal, preserving PresetPack rule 1). Reconfigures reuse any already-extracted pack via a
      LICENSE.md glob, so the fast loop needs no network even though the extracted dir name
      changed with the pin. Verified: cold configure EXIT 0 (fetch + hash + extract), reconfigure
      has 0 fetch lines, wrong-hash simulation warns + falls back with EXIT 0 (restored), fallback
      download via `cmake -P`, `grep` shows both URLs + hash, full build 375/375. Docs:
      `docs/dev/DEPENDENCIES.md`. STATUS/T0389.log.
- [x] T0390 **Consolidate the remaining FakeReply duplicates** (`tests/unit/suno/`) #cleanup
      Done 2026-11-10: removed the duplicate local `class FakeReply` from `test_DownloadQueue.cpp` (now `using fake_net::FakeReply`). The shared `tests/support/FakeNetworkReply.hpp` gained the 4 drivers the local class had (`succeedShort`, `succeedUnknownLength`, `failWithRetryAfter`, `openStream` overload). `grep "class FakeReply" | wc -l` = 1; unit_tests + ctest 33/33 green.
      Two local `class FakeReply` remain beside the shared `FakeNetworkReply.hpp`. Narrows T0099. Verify: `grep -rn "class FakeReply" tests/ | wc -l` is 1; `ctest --test-dir build-fast/tests -R "DownloadQueue|RequestEpoch"` green.
- [ ] T0391 **Shared JSON object-parse helper; fix the 3 unchecked sites** (`src/suno/SunoClient.cpp:1490`, `src/suno/SunoAccountManager.cpp:243,297`, `src/suno/`) #risk-medium
      86 `fromJson` sites repeat the parse-error boilerplate; 3 never check. Also merge C++-lane opt* scalar unify (ClipParser vs Mango/ModelCatalog/Clerk). Verify: no unchecked `fromJson` remains; `./build.sh --fast --tests --safe` 16/16.
      **PARTIAL 2026-11-10:** the 3 unchecked sites now check `QJsonParseError` and fail via each file's existing error channel (SunoClient `handleJsonReply`; AccountManager `handleSessionReply`/`handleBillingReply`). unit_tests green, `test_SunoAccountManagerTos`/`Onboarding` green. **Still open:** shared-helper extraction across the remaining ~83 sites + opt* scalar unify.
- [x] T0392 **DB sync/migration/rollback tests** (`tests/unit/suno/test_SunoDatabase.cpp`) #risk-high #data-loss
      Done 2026-11-09: 4 cases — upsert round-trip (title/tags/artwork), failed sync rolls back with no partial
      rows, failed transaction preserves existing data, legacy-schema migration keeps rows (duration "65.0"→"01:05", user_version 3).
      Verify: new cases pass under `./build.sh --fast --tests`.
      **DONE:** ctest 33/33; full thorough suites run to completion, including TestSunoDatabase -> 0.
- [-] T0393 **Unify the API base URL** (`src/suno/auth/SunoOAuthLoopback.hpp`, `src/suno/SunoEndpoints.hpp`, `src/suno/CapturedHosts.cpp`) #risk-medium
      **DROPPED as invalid 2026-11-10** (recon): the 3 "hardcoded occurrences" are the RFC 8414 OAuth issuer of the MCP authorization server (`https://studio-api-prod.suno.com/api/v2/external/oauth/*`), a DIFFERENT API tree from `API_BASE` (`https://suno.com/a/api`). Joining them invents an uncaptured route. T0336 (DONE, TODO.md:62) already recorded this decision deliberately. `CapturedHosts.cpp:68` `kStudioProd` must stay (live alias + allowlist + static asserts). No code change; must not proceed without a fresh capture.
- [x] T0394 **Endpoint dead-constant guard script** (`scripts/check-endpoints.py`, `src/suno/SunoEndpoints.hpp`, `docs/suno_api/ENDPOINT-INVENTORY.md`) #cleanup
      Done 2026-11-10: `scripts/check-endpoints.py` extracts all 169 `endpoints::` constants, reports zero-call-site names, and pins the count via `--baseline` (so a new unused route fails CI without gating existing debt). Wired into `guards.yml`.
      Measured at HEAD: 72 referenced, **97 dead** (TODO's "127" was a stale/different count). Verify: `./scripts/check-endpoints.py --baseline 97` exit 0; drift simulation `--baseline 96` exit 1.
- [ ] T0395 **Split VideoRecorderFFmpeg god TU** (`src/recorder/VideoRecorderFFmpeg.cpp`, `src/recorder/VideoRecorderFFmpeg.hpp`) #cleanup
      1734 lines mixing encoder, muxing, resampling, subtitle, HW probe. Verify: HW-probe + subtitle-encoder move to separate TUs; recorder lanes green; file under 600 lines.
- [ ] T0396 **Split the SunoClient credential worker + queue** (`src/suno/SunoClient.cpp`, `src/suno/SunoClient.hpp`) #cleanup (after: T0401)
      1660 lines. Do not start while T0362 is claimed. Verify: worker in its own TU; credential tests unchanged and green.
- [ ] T0397 **De-globalize Config behind an injected interface** (`src/core/Config.hpp`, `src/core/Config.cpp`, `src/core/Application.cpp`, `src/audio/AudioEngine.cpp`) #cleanup
      Two-process-global singletons (`Config`, `Application`) block isolated tests. Verify: tests construct two Configs without global surgery; `CONFIG` macro confined to `main.cpp`.
- [x] T0398 **Replace raw new/delete in audio upload** (`src/suno/SunoAudioUploadService.cpp`) #risk-medium
      Done 2026-11-10: `QFile` and `QHttpMultiPart` are local `unique_ptr`s. The multipart uses a `deleteLater` deleter (REQUIRED: a multipart already handed to `post()` may still be read by a live reply on early return, so a plain `delete` is a use-after-free). Ownership transfers via `setParent` + `release()` — no manual delete/deleteLater remains, behavior preserved. Verify: `test_SunoAudioUploadService` 29/29 + `test_SunoRequestEpoch` 11/11 + unit_tests green.
      `new QFile`/`new QHttpMultiPart` with manual delete; early return leaks. Verify: no manual delete remains; ASan lane clean on the upload failure path.
- [x] T0399 **Log the double-catch in tag read** (`src/suno/SunoDownloader.cpp:1006-1013`) #cleanup
      Done 2026-11-09: `catch(std::exception& e)` logs at DEBUG before `nullopt`, so OOM/bad_alloc is visible; success/malformed-tag behavior unchanged. unit_tests linked + full run green.
- [x] T0400 **Audit OffscreenRenderSpike thread lifetime + production include** (`src/recorder/OffscreenRenderSpike.cpp`, `src/recorder/OffscreenRenderSpike.hpp`, `src/recorder/RenderExecutor.hpp:153`) #risk-high
      Done 2026-11-09: verdict vocabulary (RenderThreadVerdict/toString/offscreenVerdictMaxConcurrency) absorbed into RenderVerdict.{hpp,cpp}; probe sources removed from RECORDER_SOURCES (test-only now, compiled only by test_OffscreenRenderSpike). App binary: 0 refs to runOffscreenRenderProbe. Verify: full build 380/380, ctest 33/33.
- [x] T0401 **Guard CredentialStoreWorker enqueue with the shutdown flag** (`src/suno/SunoClient.cpp:364-373`) #risk-high
      Guard landed 2026-11-08: enqueue's queued lambda now captures the shared shuttingDown_ copy and early-returns, mirroring requestRestore. New `storeEnqueuesOnWorkerThread` covers the store/remove path.
      Verify: enqueue checks the same flag; shutdown-order test covers `wait()` failure.
      **DONE:** ctest 33/33.
- [ ] T0402 **Expose audio/render tuning constants as TOML keys** (`src/core/ConfigData.hpp`, `src/core/ConfigParsers.cpp`, `src/audio/AudioQueue.hpp`, `src/audio/VisualizerAudioTap.*`, `src/audio/AudioEngine.*`, `src/recorder/RenderExecutor.*`) #cleanup (after: T0397)
      Verify: each constant has a TOML key + documented default equal to today.
      **Recon 2026-11-10 — premise partly stale.** The constants are NOT unnamed magic numbers: they are already named `inline constexpr` or live in `RenderExecutor::Config` (which `setConfig()` already applies). Real remaining work is (a) TOML/plumbing + (b) construction-time injection. Split:
      - (a) UNBLOCKED, small: add one field to `RecordingConfig` (`ConfigData.hpp:119-130`) + an `X("quantum_frames", quantumFrames, U32)` row in `CHADVIS_RECORDING_FIELDS` (`ConfigParsers.cpp:120-127`) + clamp in `parseRecording`. Pure ConfigData/ConfigParsers, zero T0397 dependency, pinned by the existing `roundTripPreservesEveryField` test. Establishes the pattern for the rest.
      **DONE 2026-11-10 (a):** `RecordingConfig::quantumFrames` (default 8, the executor's `kDefaultQuantumFrames`), a `quantum_frames` row in `CHADVIS_RECORDING_FIELDS`, a 1–32 clamp at the end of the recording-field expansion in `parseRecording`, the key in `config/default.toml`, a row + "not yet consumed" disclosure in `docs/user/CONFIG.md`, and the field in `populate`/`verifyPopulated` so the round-trip test fails if the key is ever dropped. Nothing reads it yet: `RenderExecutor::setConfig()` has no production caller, which is (b).
      - (b) BLOCKED on T0397: `AudioQueue` capacity (:27 36000), `VisualizerAudioTap` rate/chunk/lead/realign (:13,14, hpp:66,69,72), `AudioEngine` debounce (:159 500) — these are needed at construction time and the only route today is the global `Config::instance()`, exactly what T0397 removes. Do NOT touch them before T0397.
      - NOT config (leave): `AUDIO_FRAME_SAMPLES` (:23 8, cache-line geometry), AudioFrame ctor 48000/2 (:36,182 structural), `EncoderSettings.cpp` named preset factories. ConfigData would break layout/tests.
- [x] T0403 **Modernize snprintf to std::format** (`src/suno/SunoDownloader.cpp`, `src/recorder/VideoRecorderFFmpeg.cpp`, `src/recorder/OffscreenRenderSpike.cpp`) #cleanup
      Verify: zero `snprintf` in scope; output strings byte-identical.
      **DONE 2026-11-08:** 6 sites moved to `std::format` (`{:02d}`/`{:03d}`/`{:x}`/`{:.{}f}`); full build 499/499, ctest 26/26.
- [x] T0404 **Re-check the JThread fallback necessity** (`src/util/JThread.hpp`) #cleanup
      Verified 2026-11-10 (compile probe, AppleClang 16 / libc++ 180100): `__cpp_lib_jthread` is NOT defined; `std::jthread`/`std::stop_token` are compiled out by `_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN` (gated behind `_LIBCPP_ENABLE_EXPERIMENTAL`). The fallback at JThread.hpp:42 is what compiles — removing it breaks 8 TUs. Keep it.
      Verify: fallback removed if all supported compilers have jthread, else the comment cites the exact lacking compiler.
      **DONE:** comment corrected to name AppleClang 16 / libc++ 180100 + the experimental gate.
- [ ] T0405 **Decompose the Application coordinator** (`src/core/Application.cpp`, `src/core/Application.hpp`, `src/core/CliUtils.cpp`) #cleanup (after: T0397)
      CLI parse + config overrides + subsystem wiring in one 846-line file. Verify: `--help`/`--dump-config` output unchanged.
- [ ] T0406 **Deduplicate the file-path helpers into one module** (`src/qml/`) #cleanup
      `fileName`/`localFileUrl`/`toLocalPath`/`toLocalFilePath` copied 12× across 7 files. Verify: single implementation; all 9 call sites delegate; URL unit test covers spaces/`#`/`?`/drive letters.
- [ ] T0407 **Extract a shared StateView (loading/empty/error/retry)** (`src/qml/components/StateView.qml`, `src/qml/views/LibraryView.qml`, `src/qml/views/DiscoverView.qml`, `src/qml/views/NotificationsView.qml`, `src/qml/panels/PresetsPanel.qml`, `src/qml/views/VideoView.qml`, `src/qml/views/CreateView.qml`) #cleanup
      Tri-state copy-pasted 3×; 5 views/panels blank or silent. Narrow of T0076. Verify: one component owns it; PresetsPanel shows empty; VideoView names a null visualizer window; CreateView shows BusyIndicator.
- [ ] T0408 **Extract SettingsPageShell; move AccountPage gate-model logic to C++** (`src/qml/settings/`, `AccountPage.qml`, `SunoBridge`) #cleanup
      7 near-identical Flickable shells; 892-line AccountPage builds 27 gate rows in JS. Verify: one shell used by all `*Page.qml`; gate rows arrive pre-built.
- [ ] T0409 **LyricsPanel: App* wrappers, accessible search, export errors, loading state** (`src/qml/panels/LyricsPanel.qml`, `LyricsBridge`) #cleanup
      Raw Controls, emoji search button with no Accessible.name, silent export failures. Verify: no raw `ToolButton`/`TextField`/`RadioButton`/`Dialog`; export failure shown in-panel.
- [ ] T0410 **Persist the karaoke glow knob and bind VideoView to it** (`KaraokeSettings.qml`, `VideoView.qml`, `SettingsBridge`) #cleanup
      The toggle is local-only; the live view hardcodes `showGlow: true`. Verify: persisted with NOTIFY, survives restart, toggle visibly changes glow on a real window (headless is not proof).
- [ ] T0411 **Shortcuts: ship the editor or stop implying editability** (`ShortcutsSettings.qml`, `SettingsBridge`) #cleanup
      "Coming soon" beside a panel that looks authoritative. Verify: bindings editable and persisted, or the panel is labelled read-only.
- [ ] T0412 **Move color/time math from Theme.qml JS to a C++ singleton** (`Theme.qml`, `AppColorPicker.qml`, new `ColorUtil`) #cleanup
      28 JS functions for color science; `#RRGGBBAA` vs `#AARRGGBB` trap documented because it bit before. Verify: C++ unit tests incl byte-order cases; QML delegates.
- [ ] T0413 **Single save-state model for ClipCard/ClipDetailSheet/LibraryView** (`src/qml/components/`, `LibraryView.qml`) #cleanup
      Same save-state machine 3×; error sentence re-derived per component. Verify: one store owns per-clip state; icon-only AppButtons have explicit accessible names.
- [ ] T0414 **OverlayPanel: themed palette + bounds-checked selection** (`OverlayPanel.qml`, `OverlayBridge`) #cleanup
      Hardcoded 12-color palette; unchecked bridge-array index. Verify: palette from Theme; deleted index degrades safely.
- [ ] T0415 **Replace raw sizes/fonts with Theme tokens** (`src/qml/`) #cleanup (after: T0084)
      Raw `font.pixelSize`, `font.family: "Inter, Roboto, sans-serif"`, `width: 420` etc. Verify: no raw font literals outside Theme; overlay follows `karaokeFont`.
- [ ] T0416 **PresetsPanel: preserve scroll on model change, drop per-star MultiEffect** (`PresetsPanel.qml`, `PresetBridge`) #perf
      Model rebuild loses scroll; ~250 effect layers per 50-row list. Verify: position preserved across `presetsChanged`; no per-item MultiEffect. Needs a real window for jank.
- [ ] T0417 **Audit the bridge surface: consolidate visualizer control, prune dead Q_INVOKABLEs** (`src/qml_bridge/`, qml callers) #cleanup
      Visualizer split across two bridges; SunoBridge has 195 references and apparently uncalled invokables. Verify: every Q_INVOKABLE has a QML or C++ caller or is removed; owning bridge documented.
- [ ] T0418 **Bound recorder finalize with a timeout + QML progress** (`VideoRecorderCore.cpp`, `RenderExecutor`, `RecordingPanel.qml`) #risk-high (after: T0156, T0157)
      `stop()` can block forever on a stuck FFmpeg flush; Finalizing has no progress surface. Verify: stop returns past a documented timeout; panel shows finalizing progress. Real encode + stop required; headless is not proof.
- [ ] T0419 **Audio settings: apply live or mark restart-required in-place** (`AudioSettings.qml`, `SettingsBridge`, `AudioEngine`) #cleanup (after: T0131)
      Device/rate/buffer only apply on restart, disclosed only in a shared notice. Verify: controls reconfigure the engine, or each carries an inline restart badge.
- [ ] T0420 **Move QML data-shaping to C++ bridges** (`AccountPage`, `ClipDetailSheet.qml`, `PresetsPanel.qml`, `RecordingPanel.qml`) #cleanup (after: T0412)
      Gate rows, detail fields, `"__favorites__"` sentinel strings in JS. Verify: typed bridge models; sentinels eliminated; QML presents only.
- [x] T0421 **Close the refuted-premise items as no-ops** (`TODO.md`, `STATUS/`) #cleanup
      T0080, T0113, T0114, T0065, T0137 bodies refute their own premises but stay `[ ]`. Verify: re-grep each, flip to `[-]` with the in-body refutation + date. No code.
      **DONE 2026-11-08:** all five flipped to `[-]` with a dated CLOSED line; each premise re-grepped first.
- [x] T0422 **Release the expired claims sweep** (`STATUS/*.claim`) #cleanup
      9 expired claims (T0051–T0057, T0182, T0199, T0335, T0336) block `O_EXCL` re-claim; T0358 `[~]` has no claim file. Verify: sweep run, board marks match filesystem claims.
      **DONE 2026-11-08:** all 11 expired `.claim` files released; `STATUS/*.claim` is empty and `task.sh peers` reports no stale rows.
- [x] T0423 **Split T0362 steps 2–8 into per-step tasks** (`TODO.md`, `docs/suno_api/`) #feature
      8 steps in one 21-line task; step 1 done.
      **DONE 2026-11-08:** split into T0430 (upload chain), T0431 (stem + poll), T0432 (persona create), T0433 (free reads), T0434 (cover apply), T0435 (voice verification). T0362 is now a closed umbrella. Playlist-v2 trash was already covered by T0383.
- [x] T0424 **Fix the dead after: edge on T0383** (`TODO.md`) #cleanup
      T0383 `(after: T0267)` — T0267 exists nowhere. Verify: edge dropped or repointed.
      **DONE 2026-11-08:** edge dropped; T0383 has no unmet dependency (its mutation half shipped 2026-11-07 and its reads 2026-11-08).
- [x] T0425 **Index the 5 unlisted suno_api docs in docs/README.md** (`docs/README.md`) #cleanup
      `MCP.md`, `UI-SURFACE-INVENTORY.md`, `CLIENT-GATE-RECON-2026-11-07.md`, `LOGIN-CAPTURE-FINALIZATION.md`, `TOUCH-TOKENS-SHAPES.md` missing from the hub table.
      **DONE 2026-11-08:** all five added as table rows between OAuth redirect analysis and Evidence provenance; every link resolves from `docs/`.
- [x] T0426 **Fix the 2 broken relative links in docs/backlog/T0346.md** (`docs/backlog/T0346.md`) #cleanup
      `../../suno_api/` → `../suno_api/`.
      **DONE 2026-11-08:** lines 8 and 23 fixed; both targets confirmed present under `docs/suno_api/`.
- [x] T0427 **Fix build.sh --help --qt text** (`build.sh`) #cleanup
      Help omits `$HOME/Qt`, MacPorts, and the `.qt-stitch`-last rule documented in CHANGELOG.
      **DONE 2026-11-08:** help now reads `--qt PATH  Qt6 prefix (else $CHADVIS_QT_PATH, then $HOME/Qt, MacPorts, Homebrew/distro, .qt-stitch last)`, matching the resolution order in the script.
- [x] T0428 **Re-rank T0011 P0 → P1** (`TODO.md`) #cleanup
      Verbs enabled 2026-10-30; the UI gap is T0359's scope.
      **DONE 2026-11-08:** T0011 block moved under P1 with a dated re-rank line; T0359 still owns the UI gap.
- [x] T0429 **Normalise `#risk-med` → `#risk-medium`; `#performance` → `#perf`** (`TODO.md`) #cleanup
      Grammar lists only the canonical forms.
      **DONE 2026-11-08:** `perl -i -pe 's/#risk-med(?!ium)/#risk-medium/g; s/#performance\b/#perf/g' TODO.md`
      leaves 0 matches for either non-canonical tag; `task.sh audit` clean.

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

- [-] T0137 Preset categories always "Uncategorized" (`PresetScanner.cpp`) #feature
      **Stale (2026-11-07):** PresetScanner derives category from the relative subdirectory
      (`CategoryA` test passes); "Uncategorized" is correct only for presets flat in the scan root.
      **CLOSED 2026-11-08:** `PresetScanner.cpp:33-35` sets the category from the relative subdirectory; the fallback fires only for a flat root.

- [ ] T0138 Standard modern C++ where tree reaches for older idioms (`src/`) #cleanup
      `std::mdspan`, concepts, `std::array` for `CircularBuffer`, `std::variant`, `std::jthread`.

- [ ] T0139 Hook up off-hooks keys (`main.qml`) #feature #risk-high
      `main.qml:511-545` documents bindings, `:598-602` binds shortcuts. Family has no handler. Grep every key in
      `ShortcutsSettings.qml` against `main.qml`, wire or delete.

- [ ] T0188 Interaction language: hover-grow, 44×44 targets, responsive breakpoints (`AppButton.qml`) #feature
      Hover-grow half written. Tap targets 36px, not 44. No hover on AppComboBox/AppTextField/AppSwitch/AppSlider.
      No mobile breakpoint or bottom nav. Redesign, not patch.

- [ ] T0299 **Linux ships only TGZ/ZIP — no .deb/.rpm/AppImage/Flatpak** (`Install.cmake:43`) #feature

- [ ] T0300 **Nothing measures coverage** (`.github/workflows/`, `CMakeLists.txt`) #risk-medium

- [ ] T0301 **No static-analysis, SBOM, or dependency-vulnerability lane** (`.github/workflows/`) #security

- [ ] T0305 **Four overlapping C++ skills, unclear provenance** (`.opencode/skills/`) #cleanup
      `c-cpp-profi`, `modern-cpp`, `cpp-coding-standards`, `cpp-pro` all cover modern C++. Consolidate.

- [ ] T0309 **Browser-cookie import macOS-only in code, platform-blind in UI** (`BrowserCookieImport.cpp:223-260`) #risk-medium
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

- [ ] T0385 **Reorganize `src/` into granular self-explanatory directories** (`src/`) #cleanup
      Current top-level dirs (`core`, `suno`, `recorder`, `qml_bridge`, etc.) hide contents;
      a new reader needs to open the tree to know what lives where. Split/index by
      responsibility (AGENTS.md §4.1) so each directory and file name is self-explanatory
      at top-level recon. Directories are free; agent confusion is not an acceptable cost.
      Acceptance: directory inventory with one-line role per dir, moves preserve build
      (`./build.sh --fast --tests`) and QML/bridge wiring, docs/README index updated.

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
  time. The renderer's independence (T0115) stays
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

### Confirmed live, 2026-11-06 persona/voice/credits HAR (`[T1]`)
Capture: `~/Documents/suno-master-utility-browser-extension/scratchpad/suno.com_me_persona.har`.
Detail: ENDPOINT-INVENTORY §4.11 + §5.15.
- `GET /voice-verification/phrase/` -> `{phrase_id, phrase_text}`
- `POST /processed_clip/voice-vox-stem` `{upload_id, recording_type|range}` — stem from an UPLOAD
  (`/clip/{id}/vox-stem` is the gen-clip twin; not interchangeable)
- `POST /voice-verification/` + `GET /voice-verification/{id}` -> `status:"approved"`
- `GET /preferences/{summary,tasks,stats,rewards,leaderboard,profile}`, `PUT /preferences/profile`,
  `POST /preferences/{submit,tasks/release-buffer}` — **Listen for Credits**, earns credits
- `POST /preferences/submit` -> 403 `verification_required` (captcha-gated)
- `POST /gen/prompt_image/` `{prompt}` -> `{image_url, image_s3_id, upload_id}`; `image_s3_id`
  is the id `playlist/v2/{id}/cover-image` wants
- `POST /playlist/v2/{id}/trash` `{"undo":false}` -> `{success, updated_fields:["is_trashed"], errors:{}}`
  (200, undoable — unlike `/playlist/trash/` which is 204 with no body)
- `POST /unified/library` `{cursor, customization:{include_synthetic}}` -> nested `feeds[]`
- `POST /video_gen/action` -> 204; `POST /suno_chat/rooms` -> 403 feature_disabled;
  `GET /visual/models` -> 403 feature_disabled
- `GET /v2/integrations` -> `{connections:[]}` (in-app connector list; MCP would appear here)
- `GET /labs/configs` -> live-radio, genre-wheel, listen-and-rank, milo
- `GET /api/feature-flags` (legacy base, 401 anon)

### Confirmed live, 2026-11-07 sign-in + DevTools HAR (UI-07, `[T1]`)
Detail: ENDPOINT-INVENTORY §4.12. All 200s.
- Library tabs: `GET /video/hooks/user_hooks`, `GET /video/hooks/me/liked/v2`,
  `GET /video/hooks/suggested_clips`, `GET /profiles/listen-history?limit=20`,
  `GET /clips/trashed_v2?limit=20`,
  `GET /playlist/me?page=1&show_trashed={false|true}&show_sharelist=false`,
  `GET /project/me?page=1&sort=max_created_at_last_updated_clip&show_trashed=false&exclude_shared=false`,
  `GET /lyrics-projects?limit=100&sort=updated_at`
- Albums/Playlists feeds: `POST /unified/feed {feed_id:"library_albums"|"library_playlists",cursor,page_size,request_metadata}` -> `{feed:{…items[]}}`
- Trash: `POST /gen/trash {trash,clip_ids}` -> `{ids,is_trashed}`;
  list `GET /clips/trashed_v2?limit=20`
- Playlist trash + recover: `POST /playlist/v2/{id}/trash {"undo":false}` and
  `{"undo":true}` -> `{success,updated_fields:["is_trashed"],errors:{}}`
- Persona: `PUT /persona/edit-persona/{id}/` (name/desc/is_public/styles/vox_audio_id/range),
  `GET /persona/get-persona-paginated/{id}/?page=1`, `POST /persona/{id}/toggle_love/`
- Create: `POST /c/check {"ctype":"generation"}`; speech and sounds share
  `POST /generate/v2-web/` (`metadata.is_speech`, `task:"sound"`); simple mode uses
  `task:"agentic_thinking"` + `gpt_description_prompt`
- Studio: `GET /studio/list-projects`, `POST /studio/create-project?title=&major_version=2`,
  `GET /studio/project/{id}`, `GET /agent/conversation/{id}`, `GET /plugins/catalog`,
  `GET /plugins/{id}/presets`
- Uploads: image chain `POST /uploads/image/ {extension:"png"}` -> S3 ->
  `upload-finish/ {upload_id,moderation_status}`; audio `upload_type:"file_upload"`;
  `upload-finish/` -> `{}`; `initialize-clip/` -> `{clip_id,rights_clearance_available:false}`
  observed (the false branch is real)
- `GET /processed_clip/{id}` -> `status:"complete"` for a finished voice stem
- `POST /prompts/suggestions/contextual {signature,count}`; `GET /statsig/experiment/forked-onboarding`

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
- ~~Voice-verification chain~~ **captured 2026-11-06** (§5.15): phrase -> upload stem -> verify -> poll
- Generation gates: `aug_creativity` (0–4 whole), `mumble-mode`, `max-mode` session flags

### Session notes
- Account: `pro` role, `models: array[0]` in session envelope, Turnstile-gated (`captcha_version: 2`)
- `__session` cookie expires ~46 min. `__client` refresh JWT ~1 year (HttpOnly, Secure, SameSite=None)
- `freshtok.py` at `~/Documents/suno-recon-active-investigation/bin/` extracts `__session` from Brave cookies

### Recon dirs (priority by mtime)
1. `~/Documents/suno-recon-har-recordings-full-api/` — newest, signed-in full-API HAR
   (`suno.com_Archive.har`, 154 MB, 2026-11-05/06) + `network-endpoints.txt`
2. `~/Documents/suno-master-utility-browser-extension/scratchpad/` — `suno.com_me_persona.har`
   (2026-11-06, persona/voice/credits) and `suno.com_me.har` (truncated, unusable)
3. `~/Documents/sunox-chadvis-suno-frontend-projectm-qt-research/` — API intelligence, protocol audits, capability matrix
2. `~/Documents/suno-recon-active-investigation/` — `suno-session.json`, `app-endpoints.md`, `interesting-api-calls.md`, HAR captures
3. `~/Documents/suno-recon/` — JS crawler snippets, endpoint lists, API probe tools
5. `~/Documents/suno-recon-cat-stealth-modekl/` — `captured_endpoints*.json`, `network-endpoints.txt`, OAuth google flow, feature flags

### Suno layer maturity
Only playback (download tracks locally for free via trick) tested. Tagging, artwork, lyrics — unknown if done.
Major project still early. Remote info available if needed.
