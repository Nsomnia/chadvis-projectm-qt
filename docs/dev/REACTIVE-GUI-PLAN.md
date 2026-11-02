# Reactive GUI plan for ChadVis — buttons that move, sliders that glow, and a UI that answers back

> **Status:** Implementation-ready design document. Not code, not a merge request.
> **Status update 2026-11-01:** Derek authorised pushing this plan to `main`
> and beginning implementation under the agent's best judgement, with
> `TODO.md` tracking kept up to date (see T0358–T0361). The phases in §7
> are now the live work plan; this document itself is unchanged except
> for this note.
> **Baseline verified against:** `origin/main` at `9831417` ("build: pin projectM to
> unreleased master 349b7cc (4.2.0-dev)", 2026-10-31), Qt **6.8 floor**
> (`cmake/Dependencies.cmake`: 6.8+ required, 6.7 rejected), C++23, `version.txt`
> `1.2.0`. All file paths, property names, endpoint paths, and commit hashes in
> §1–§2 were read from that tree. Anything not read there is labelled
> `[UNVERIFIED]` or `[LEAD]` and must not be treated as a contract.
>
> **Audience:** Any LLM agent (or human) implementing this in this repo, with no
> prior ChadVis context. Read §0 first. Do not skip §10 ("Do not do this").
>
> **How to use this document:** Work in phase order (§7). Each phase ends with
> acceptance criteria and a verification command. Do not start Phase 3
> (audio-reactive) before Phase 0–1 (motion tokens + primitives) land, and do not
> wire a Suno route to QML unless §1 says it is `[T1]`-captured *and* wired in
> C++ — or the phase explicitly says to add the one missing service method.
>
> **What this document is NOT:** It does not change the Suno API posture, the
> theme engine's two-base-colour model, or the projectM visualizer itself. The
> visualizer stays in projectM; this plan makes the *chrome around it* —
> buttons, sliders, cards, sheets, transport, nav — react to the user, to
> playback, and to Suno state.

---

## 0. Agent operating rules for this repo (read before touching code)

These rules are repo-specific and override generic QML habits. Violating them
is how past work in this tree broke (see the examples).

1. **Every new QML file must be registered.** QML lives in
   `cmake/Sources.cmake` (`QML_SOURCES`, lines ~222–269) and is compiled by
   `qt_add_qml_module(project_lib URI ChadVis …)` in
   `cmake/TargetSetup.cmake`. An unregistered file fails at window load with
   "`X is not a type`". This is exactly what `50ee0c6` fixed for
   `PlaylistStrip.qml`. After adding any file, grep `Sources.cmake` for it.
2. **Imports are unversioned.** Every file in `src/qml/` writes
   `import QtQuick`, never `import QtQuick 6.x`. Keep it that way. This is
   why Qt 6.8+ types (`QtQuick.Effects.MultiEffect`, `SpringAnimation`,
   `SmoothedAnimation`) are available without import changes — but it also
   means **do not use any Qt 6.9/6.10/6.11/6.12-only API** (see §9.3).
3. **Theme is a singleton with exactly two mutable bases.** `src/qml/styles/Theme.qml`
   (`pragma Singleton`) derives everything from `accent` + `background`
   through `themed()` / `ramp()` / `lighten()` / `darken()` / `withAlpha()`.
   `overrides` is replaced wholesale, never mutated in place. The one entry
   point is `applyThemeById()`; the colour picker goes through
   `applyAccent()` / `applyBackground()`. Never add a second theme-apply path,
   and never hardcode the shipped cyan `#00bcd4` (the T0185 bug class).
   `ThemeDefinitions.qml` carries colours only — no motion tokens today.
4. **Seeding order matters.** `Theme` is lazily instantiated. Startup seeding
   happens in `main.qml`'s `Component.onCompleted`, *not* in the singleton's
   own completion (see the T0047 comment in `Theme.qml`). Any new motion
   preference must be seeded the same way, from `main.qml`, after
   `SettingsBridge` is constructed.
5. **Suno evidence grades are law.** `docs/suno_api/ENDPOINT-INVENTORY.md`
   grades every route `[T1]` (captured, byte-level) or `[LEAD]` (a bundle
   string, a web page, a replay click — never evidence). A `[LEAD]` route is
   not a contract. Before wiring any new Suno route, read the repo skill
   `.opencode/skills/suno-capture-client/SKILL.md` and the endpoint inventory
   §1.3 axis: *wired* ≠ *reachable* ≠ *captured*. The 11 mutations in §1.3
   are the exception that proves the rule: wired *and* captured, but with
   zero QML callers — that is the gap this plan fills first.
6. **Verification before completion.** This repo's skill
   `.opencode/skills/chadvis-verify/` is the gate: build with
   `./build.sh --fast --tests`, run `./scripts/lint-qml.sh` (which wraps the
   build's own `project_lib_qmllint` target — do *not* invoke `qmllint` by
   hand, it cannot resolve this tree's mixed relative + module imports),
   and run the Qt Quick Tests for touched components. A change that has not
   been read back after a write is not done; the GitHub write path in this
   project has historically hung client-side after the commit landed.
7. **No GitHub write without approval at the time.** Standing scoped grants
   in this project do not cover this document. Writing this file locally is
   the deliverable. Pushing it, opening a PR, or touching
   `.github/workflows/*` needs a fresh explicit approval, and the GitHub App
   used by some agents cannot touch workflows (hard 403) or push tags.
8. **Accessibility is not optional.** Every animated control must keep its
   existing `Accessible.role` / `Accessible.name`, keyboard path
   (Return/Space for buttons), focus ring, and a non-motion state signal
   (colour + icon + text, never motion alone). See §8.

### 0.1 Verification commands (run from the repo root)

```bash
./build.sh --fast --tests          # configure + build + unit tests
./scripts/lint-qml.sh              # filtered qmllint via the build target; fails on a real problem
./scripts/lint-qml.sh --strict     # everything, as a gate
./scripts/lint-qml.sh --list       # raw counts only
# QML Profiler (see §9.1 qt-qml-profiler skill) on every animated screen
# before claiming a phase complete; a dropped-frame animation is a defect.
```

### 0.2 Vocabulary used in this document

- **Micro-motion:** 50–300 ms feedback on a single control (press, hover, toggle).
- **Wiggle / shake / jiggle:** a short, decaying oscillation on `rotation`
  and/or `x` used *only* as feedback for refusal, error, or "look here".
  There is no built-in `Wiggle` type in Qt Quick — it is always composed
  (see §4.3).
- **Glow:** a soft halo. In this codebase today that means a bordered ring
  or a `MultiEffect` shadow/blur — *not* the old Qt 5 Graphical Effects
  `Glow` type, which must not be introduced (see §9.2).
- **Reactive:** a property that changes because a bridge signal changed
  (playback state, Suno state, generation state), not because the user just
  clicked it. Audio-reactive is a subset (§5).
- **MUST / SHOULD / MUST NOT:** RFC-style. A MUST NOT in §10 blocks a phase.

---

## 1. What actually changed: the Suno endpoints expansion (2026-10-26 → 2026-10-31)

All commits below are ancestors of baseline `9831417`. Base URL for the
Studio API in this tree is `https://suno.com/a/api`
(`src/suno/SunoEndpoints.hpp:48`, moved there from the old host by `661d19a`).

### 1.1 Commit map — what landed, what it touches in QML, what is still headless

| Commit | Change | QML consumer today | Still missing in QML |
|---|---|---|---|
| `7483bd0` (2026-10-26) | Docs: the 11 mutation routes were already sent by `SunoLibraryMutations.cpp` but mislabelled "not-in-code". Adds the third axis: *wired but runtime-unreachable until a UI path invokes them*. | None (docs) | The UI path itself |
| `dc8acc6` (2026-10-26) | Refactor: `SunoBridge` split into `SunoMutationsSection` (11 verbs + clip-vs-playlist routing) and `SunoDownloadSection`; 15 zero-reader QML-facing items deleted. `SunoBridge.cpp` 1645 → 1328 lines. QML singleton names/args unchanged. | None new; existing QML untouched | — (refactor) |
| `661d19a` (2026-10-27) | Notifications list moves to `GET /notification/v3?include_hooks=true` (caption = concatenated `text[].text` segments, v2 caption fallback). Badge count + read/clear stay on v2 — "there is no v3 badge route". | `src/qml/views/NotificationsView.qml` (pre-existing; this commit touched no QML) | Per-notification mark-one-read; notification → clip navigation (no bridge method exists) |
| `bef6305` (2026-10-27) | New `SunoPlaylistService`: `GET /playlist/me` and `GET /playlist/{id}/?page=N` (1-based paging; end = empty page or `clips.size() >= num_total_results`; body: playlist object + `playlist_clips[] = {clip, relative_index, created_at}`). Bridge: `playlists`, `playlistClips`, `openPlaylistId/Name`, `playlistHasMore` + `refreshPlaylists()` / `openPlaylist()` / `requestNextPlaylistPage()` / `closePlaylist()`. Pages upsert into the DB so playlist clips are playable. | `src/qml/components/PlaylistStrip.qml` (new chip row) + `LibraryView.qml` (grid model swaps to the open playlist — "a playlist is a lens, not a second page") | Playlist create / trash / recover / delete (see §1.4) |
| `50ee0c6` (2026-10-30) | Fix: register `PlaylistStrip` in `cmake/Sources.cmake` (window previously failed to load); `LibraryView` fill/paging loop returns early while a playlist is the grid model; gate regrouping (visibility + two remix toggles are `Library`, not `Sharing`). | `PlaylistStrip` / `LibraryView` (fix) | — |
| `a5d2ae8` (2026-10-30) | Fix (T0352): gate loop calls `attemptEnable()` on `gatesChanged` and at service (re)creation — fixes cold-start `LocallyDisabled` and post-sign-out re-creation. | None directly; enables the mutation surface for QML | The mutation QML itself |
| `96fdaab` (2026-10-30) | **Enables the library mutation surface** at the single service-construction site (T0011 had held it behind `enabled_{false}`; §5.13 captured 7 responses on 2026-10-26 + owner live-work grant 2026-10-30). Gate refusal is now logged with its reason; `mutationsAvailable` answers from the real predicate. Commit message, verbatim in spirit: *"Still a UI gap, not a gate: no QML connects the four result signals yet."* | **None** | **All 11 invokables + result signals (§1.3)** |
| `4203403` (2026-10-30) | New `SunoGenerationService` replacing the refusal in `SunoWorkspace::startGeneration` / `SunoBridge::generate()`. Measured live 2026-10-30: `POST /c/check {"ctype":"generation"}` → 200 `{required:true, captcha_version:2}`; submit body is FLAT (`{params:{...}}` = 422 trap); `task` is a closed enum, ordinary generation = `"generate"`; without a token submit = 422 `token_validation_failed` (refused, never retried, no credit spent). Flow: preflight → submit `POST /generate/v2-web/` → bounded poll 4 s × 90 on `GET /clips/get_songs_by_ids?ids=…`, token + token provider attached only when a challenge was actually solved. | `src/qml/views/CreateView.qml` (form restored; captcha notice states the token must come from a generation started at suno.com/create — browser handoff) | **In-app Turnstile token input does not exist and must not be faked.** Token acquisition is browser-only. |
| `c4257e4` (2026-10-30) | New `SunoClipDetailService`: 4 GETs, each settling independently; a failed section renders nothing. Bridge: `clipDetail` (QVariantMap) + `loadClipDetail(clipId)`; `load("")` on close drops detail so a late reply cannot attach the previous clip's ancestry to the next. Attribution labels pass through unmapped (`COV` → "Cover of", `EX` → "Extended from"). Parser keys are "catalogue-grade, not captured: a renamed key renders nothing instead of guessing." | `src/qml/components/ClipDetailSheet.qml` (~lines 299–378): "Based on" ancestry list, remix-count line (only if > 0), comments list, similar list | Remix *list* and parent chain (constants exist, service does not call them — §1.4); similar-item click navigation not verified in the sheet |
| `1c1ecf8` (2026-10-30) | New `SunoOAuthLoopback` (T0356a): dynamic client registration, `127.0.0.1` redirect, PKCE S256, system browser, one-shot loopback listener, token exchange. Token set stored under `'suno/mcp'` — deliberately NOT `'suno/default'`, because the MCP bearer session is still T0356(b). | `src/qml/settings/AccountPage.qml` + `AccountSessionCard.qml`: third sign-in path "Sign in with Suno (official OAuth)" via `beginSunoMcpSignIn()` (name is historical: it is the OAuth loopback, not MCP chat) | MCP bearer session itself (T0356b — §9.4) |
| `fc4d2f4` (2026-10-30) | Docs only: `docs/suno_api/MCP.md` (Suno MCP Beta, announced 2 days earlier). | None | Whole MCP client (T0357 — §9.4) |
| `f99710f` (2026-10-30) | Docs only: `docs/suno_api/OBSERVED-LEADS.md` §Web route manifest (~155 page routes from a `_sentryRouteManifest` paste; 4 `/oauth-redirect*` landings + `/sso-callback`; stem-separation / remix / hooks / spark / credits / collab / labs as capture leads; ~90 `/b-side/*` staff paths deliberately excluded, T0146). **Page routes, not API routes — zero request data.** | None | Everything listed there |
| `0871981` (2026-10-30, updated 2026-10-31) | Docs only: `docs/suno_api/UI-SURFACE-INVENTORY.md` (87 lines). Source: 732-step Puppeteer replay, user-recorded 2026-10-30. Grade: **UI evidence only — [LEAD]; `api/` appears 0 times.** New host lead `platform.suno.com` (separate "platform account" — do NOT wire, not a `CapturedHosts` entry). | None | All surfaces in §1.5 |

Two adjacent, non-Suno commits matter for this plan and must not be lost:

- `fe46e75` (2026-10-26) "real animations, and no more `@undefined`" —
  `KaraokeMaster.qml`, `VisualizerOverlay.qml`, `NotificationsView.qml`.
  This is the commit that proved the current animation patterns work; §2
  inventories what it left behind.
- `607781f` (2026-10-26) "lift ChadVis Dark off pure black and widen its
  surface ramp" — `ThemeDefinitions.qml`. Any new glow/shadow token must be
  derived from the ramp (§3), not hardcoded, or it reintroduces the
  pure-black / hardcoded-cyan failure on light or custom backgrounds.

### 1.2 Bridge API the GUI already has (and this plan reuses, not reinvents)

Full reference is Appendix A. The properties that drive reactive UI today:

- **SunoBridge:** `loading`, `clips`, `hasMorePages`, `currentPage`,
  `filterText` (RW), `isAuthenticated`, `models`, `credits`, `planName`,
  `userName`, `generationAvailable`, `clipDetail`, `generationState`,
  `generationError`, `generationCaptchaRequired`, `generationUnavailableReason`,
  `gateStatuses`, `generationStatus`, `audioUploadBusy`,
  `audioUploadProgress`, `audioUploadStatus`, `audioUploadError`,
  `statusMessage`, `errorMessage`, `downloadStatus`, `discover`,
  `discoverLoading`, `discoverHasMore`, `discoverLoaded`, `discoverError`,
  `notifications`, `unreadCount`, `notificationsLoading`, `notificationsError`,
  `playlists`, `playlistsLoading`, `playlistsError`, `playlistClips`,
  `openPlaylistId`, `openPlaylistName`, `playlistHasMore`,
  `mutationsAvailable`, `mutationsUnavailableReason`, `mutationsBusy`.
  Invokables include `submitGeneration`, `refreshPlaylists`,
  `openPlaylist`, `closePlaylist`, `loadClipDetail`,
  `markAllNotificationsRead`, `playClip`, `downloadClip`, plus the 11 in §1.3.
  Signals include `clipSaved`, `clipSaveRefused`, `clipDownloadSettled`,
  `clipMutationSettled`, `playlistMutationSettled`.
- **AudioBridge:** `position` (ms), `duration`, `volume` (RW), `isPlaying`,
  `currentTrack` (QVariantMap with `title`/`artist`). Invokables `play`,
  `pause`, `stop`, `togglePlayPause`, `seek`, `next`, `previous`, `setVolume`,
  `loadFile`. *Today these drive only a static seek slider and time labels.*
- **LyricsBridge:** `lineProgress` (0–1), `wordProgress`, `currentLineIndex`,
  `currentWordIndex`, `lines`. *The only per-position QML signals in the app.*
- **VisualizerBridge:** `visualizerWindow` (QWindow*, embedded via
  `WindowContainer` in `VideoView.qml`), `active`, `currentPreset`
  (QString — note: `PresetBridge.currentPreset` is a QVariantMap; they are
  different properties). Invokables `nextPreset`, `previousPreset`.
  **No beat / level / energy property is exposed.** Nothing in QML today can
  react to the music itself except `isPlaying`. §5 closes that gap in C++.
- **RecordingBridge:** `isRecording`, `recordingTime`, `framesWritten`,
  `fileSize`, `encodeFps`, `bufferHealth`, `videoCodec` (RW), burn-in state.
- **PresetBridge / PlaylistBridge / OverlayBridge / SettingsBridge:** see
  Appendix A. **No animation/motion preference property exists** in
  `SettingsBridge` today — Phase 0 adds one (§3.3).

### 1.3 The single largest ready-made opportunity: the 11 mutations with zero QML callers

Verified by grep over `src/qml/` for every invokable name,
`clipMutationSettled`, `playlistMutationSettled`, `mutationsAvailable`, and
`mutationsBusy`: **zero hits**. The backend, gates, busy state, and
per-verb settled signals already exist; only buttons, menus, and toasts are
missing. This is Phase 2, and it needs **zero new C++** (except the
one-method additions in §1.4 if Derek prioritises them).

Service: `src/suno/SunoLibraryMutations.cpp`
(`SunoLibraryMutations.hpp` enum). Bridge:
`src/qml_bridge/SunoMutationsSection.{cpp,hpp}` +
`SunoBridge.hpp` invokables (~lines 259–287), result signals (~372–393),
status properties (~133–138). All paths are relative to
`https://suno.com/a/api`. Bodies are quoted from
`docs/suno_api/ENDPOINT-INVENTORY.md` §5.9; §5.13 (2026-10-26) pins several
success shapes — everywhere else, treat a 2xx as "accepted, shape unverified".

| # | Verb | QML-callable invokable (unused today) | Endpoint | Body / success notes |
|---|---|---|---|---|
| 1 | SetVisibility | `setClipVisibility(clipId, isPublic)` | `/gen/{}/set_visibility/` | `{is_public: bool}`. Server rule: "Cannot make trashed clips public." Success returns the entire updated clip incl. new key `pending_publish`. |
| 2 | ToggleRemixPermission | `setClipRemixPermission(clipId, canRemix)` | `/clips/{}/toggle_remixes/` | `{can_remix: bool}`; success `{clip_id, can_remix}` (71 bytes). |
| 3 | ToggleShowRemixes | `setClipShowRemixes(clipId, showRemix)` | `/clips/{}/toggle_show_remixes` (no trailing slash — reproduced exactly; a trailing slash is a different path) | `{show_remix: bool}`; success `{clip_id, show_remix}`. |
| 4 | UpdateFeedbackState | `setClipFeedback(clipId, reason)` | `/gen/{}/update_feedback_state/` | `{feedback_reason: string}`. |
| 5 | ShareLink | `shareClip(clipId, contentType)` | `/share/link` | `{content_id: string, content_type: string}`. |
| 6 | PlaylistSetMetadata | `setPlaylistMetadata(playlistId, name, …)` | `/playlist/set_metadata` | `{name, description, playlist_id}`; success = playlist object again (456 bytes, without `image_url`/user fields). |
| 7 | PlaylistUpdateClips | `updatePlaylistClips(playlistId, updateType, …)` | `/playlist/update_clips/` | `{playlist_id, metadata, update_type: ENUM add, remove, remove_by_id, reorder}` — unrecognised string is refused locally before sending. |
| 8 | PlaylistTracksAdd | `addClipsToPlaylist(playlistId, clipIds)` | `/playlist/v2/{}/tracks/add` | `{clip_ids: array}` — **1–100 items**; success `{successes, failures}` per clip. A 200 with all ids in `failures` is a failure. |
| 9 | PlaylistTracksRemove | `removeClipsFromPlaylist(playlistId, clipIds)` | `/playlist/v2/{}/tracks/remove` | `{clip_ids: array}` — 1–100 items; `{successes, failures}`. |
| 10 | PlaylistTracksReorder | `reorderPlaylistTracks(playlistId, …)` | `/playlist/v2/{}/tracks/reorder-by-index` | `{positions: [{clip_id, index}]}`; success `{success, updated_fields, errors}`. |
| 11 | PlaylistCoverImage | `setPlaylistCoverImage(playlistId, imageId)` | `/playlist/v2/{}/cover-image` | `{id, type: ENUM generated}` per §5.9; §5.13 adds `type ∈ {"generated","visual"}` with `cover_source:{id, type}` and warns: **a cover image is an image id, not a clip id** (clip id → 404 `cover_source_not_found`, `retryable:false`). |

Result channel already built but unconsumed:
`clipMutationSettled(clipId, verb, outcome, …)`,
`playlistMutationSettled(playlistId, verb, …)`,
`mutationsAvailable` / `mutationsUnavailableReason`, `mutationsBusy`.
GUI rules for Phase 2 (§6.2): disable mutation controls while
`mutationsBusy`, show `mutationsUnavailableReason` verbatim (never invent a
reason), toast on settled, and **wiggle the invoking control on failure**
(§4.3) — never a silent no-op, never a success toast on a partial
`successes`/`failures` split (report "3 added, 1 failed" explicitly).

### 1.4 One-method-away surfaces (captured, but no service/QML yet)

These are the *next* slice after §1.3. Each is one service method + one
bridge invokable + one button away — but each also carries a stale-doc trap.
Fix the doc trap in the same change (T0348 owns the inventory corrections).

1. **Like / Unlike — CAPTURED, but no constant, no service, no QML.**
   `POST /gen/{id}/update_reaction_type/` with
   `{"reaction":"LIKE","recommendation_metadata":{}}`; unlike =
   `{"reaction":null,…}` (inventory §4.5, 2026-10-26 signed-in browser
   capture, 200). Response is tiny (159 bytes):
   `{play_count, skip_count, flagged, reaction_type:"L", clip:<id string>, updated_at}`;
   the unlike response **omits** `reaction_type` entirely (never `""`),
   and `clip` is an id string, not an object — a parser requiring either
   breaks. **Trap:** `SunoEndpoints.hpp` still claims this route was
   "never probed" and its "deliberately ABSENT" block is stale. Also:
   §5.13's table lists Dislike → `"D"`, but §4.5 says Dislike is **not
   captured** ("presumably `"DISLIKE"`… a guess"). Treat Dislike as
   uncaptured: ship Like/Unlike only, and gate a Dislike button behind a
   fresh capture.
2. **Playlist create — response CAPTURED, no constant/service/QML.**
   `POST /playlist/create/` `{name, description}` → full 25-key playlist
   object (§5.13). The web UI exposes create/rename/description/cover, and
   rename/description/cover already have wired verbs (#6, #11) — so a
   "New playlist" button completes playlist CRUD except trash/recover.
   `SunoEndpoints.hpp` still calls it bundle-only `[LEAD]` — stale per §5.13.
3. **Playlist trash — CAPTURED, no constant/service/QML.**
   `POST /playlist/trash/` flat `{"playlist_id":"<id>"}` → **204 No Content,
   empty body** (§5.13). Trap: the nested
   `{"spec":{"playlist_id":…}}` shape 422s with a misleading `loc` path;
   success parses no body at all. Web UI also shows **Recover playlist /
   Delete playlist** — those two routes are `[LEAD]` only, not endpoints.

### 1.5 Capture-gated surfaces (GUI design may start; wiring MUST NOT start)

These surfaces are *proven to exist on suno.com* by the UI inventory, but
**zero request data exists** for them. Designing their QML (layout, motion,
empty states) is fine. Writing a service method that guesses a path, verb,
or body is forbidden — it is exactly how the stale "deliberately ABSENT"
block and the like/unlike confusion happened. Each needs one HAR export
(DevTools → Network → Export HAR, "with content") repeating the surface,
filed under `docs/suno_api/` and promoted to `[T1]` first:

- Clip trash / restore / permanent delete (`POST /gen/trash` never
  captured; `POST /clips/delete/` answered 404; no DELETE verb probed
  anywhere). Web UI: Trash with Restore-to-Library + Delete-Permanently
  (T0267).
- Comment writes (read is wired via `GEN_COMMENTS` GET into
  `ClipDetailSheet`; writes are leads only: comment block/unblock,
  reaction, replies, report — `OBSERVED-LEADS.md` §4.8).
- Remix lists beyond the count (`CLIP_REMIXES` `/clips/remixes` and
  `CLIP_PARENT` `/clips/parent` exist as constants but the detail service
  never calls them; leads include `displayable_remixes`,
  `displayable_remixes_by_user`, `set_remix_type`).
- Queue management (playbar Queue + Add-to-Queue + Like/Dislike + Copy
  Link in the web UI; queue has no ChadVis counterpart — T0321's open half).
- Library filters/sort/views (Liked, Disliked, Public, Private, Uploads,
  Full song, Cover, Voices-only, downloaded-only, Hide Disliked/Stems;
  Oldest/Newest/Most/L least liked; grid/list; sidebar sections Albums,
  Workspaces, Studio Projects, Cover Art, Hooks, Liked Hooks, History,
  Trash, Playlists).
- Playlist "More options" (Reuse Prompt, Mashup, remove-clip, Cover,
  trim handles), Explore multi-modal picker (Speech script+tone, Sounds
  description+BPM/key/mode, Record → "Name this Voice", cover-art prompt),
  and the web-route-manifest leads (stem-separation, remaster, hooks,
  spark, collab, labs: canvas / turntable / verse / vocal-isolator /
  pedalboard / drum-like).
- **Deliberately excluded, do not surface:** billing mutations / checkout /
  portal / payment methods (inventory §5.10); B-Side/Orpheus
  (`/v1/orchestrator/chat`, `/v1/orchestrator/history` — `[LEAD]` constants
  only); WAV conversion (`/gen/{}/convert_wav/`, `/gen/{}/wav_file/` —
  `[LEAD]` only); ~90 `/b-side/*` staff paths (T0146 exclusion stands);
  `platform.suno.com` (no account exists there for this user; not a host row).

**Stale-doc warnings an implementer will trip over if not warned:**
`ENDPOINT-INVENTORY.md` §1.4 "generation submission remains intentionally
disabled" is stale after `4203403`; §1.3's "12 referenced constants" audit
(2026-09-26) is stale after playlists/generation/clip-detail/notifications
were wired; the like/unlike ABSENT block is stale per §1.4. Correct the
inventory in the same commit that wires the route — do not leave a second
stale claim behind.

---

## 2. Current GUI baseline — what is already reactive, and what is not

Counts verified on baseline: **55 app QML files** under `src/qml/` (vendor
QML under `.opencode/skills/` excluded), **10,953 lines** total. Registration:
`qt_add_qml_module(project_lib URI ChadVis … NO_PLUGIN)` in
`cmake/TargetSetup.cmake`, `Theme.qml` + `ThemeDefinitions.qml` marked
`QT_QML_SINGLETON_TYPE TRUE`; bridges additionally registered in
`src/qml_bridge/BridgeRegistration.cpp` (`qmlRegisterSingletonType<…>("ChadVis", 1, 0, …)`)
and each bridge header carries `QML_ELEMENT` + `QML_SINGLETON`.
`Application.cpp` loads `qrc:/qt/qml/ChadVis/src/qml/main.qml`.
No `src/*.qrc` exists for QML.

Imports across `src/qml/` (unversioned everywhere): `QtQuick` 55,
`ChadVis` 54, `QtQuick.Layouts` 46, `QtQuick.Controls` 38,
`QtQuick.Dialogs` 5, `QtQuick.Window` 2, `Qt.labs.platform` 1.
`QtQuick.Effects` appears in only **6 files**: `AppButton`, `NavRail`,
`VisualizerOverlay`, `KaraokeMaster`, `PresetsPanel`, `PlaylistPanel`.

### 2.1 Theme tokens today (`src/qml/styles/Theme.qml`, 641 lines)

- Durations: `durationInstant` 50, `durationFast` 150, `durationNormal`
  250, `durationSlow` 400, `durationVerySlow` 600. **Only
  Instant/Fast/Normal are used anywhere in scope; Slow/VerySlow are
  defined but unused** — Phase 0 gives them jobs (sheet/panel motion)
  instead of inventing new numbers.
- Radii 4/8/12/16/999; glass `glassBackground` (surface @ 0.85),
  `glassBorder` (accent @ 0.30), `glassHighlight` (accent @ 0.10).
- Shadow dicts `shadowSmall/Medium/Large` and `glowAccent`
  (accent @ 0.5, radius 12) are defined as plain `var` objects — and
  **have zero consumers anywhere in `src/qml/`** (grep-verified). Real
  shadows today come only from inline `MultiEffect` blocks.
  Phase 1 wires these tokens into real components instead of adding a
  second shadow system.
- Fonts: `fontFamily "Helvetica Neue"` + fallback list; sizes 10–32.
- No animation code lives in `Theme.qml` itself; it only supplies durations.
- `ThemeDefinitions.qml` (176 lines): `defaultId "material"`,
  `fallbackId "chadvis-dark"`; 4 builtins (`material` purple `#BB86FCFF`,
  `chadvis-dark` green `#00FF88FF`, `gruvbox`, `nord`), each `#RRGGBBAA`
  parsed through `Theme.parseRgbaHex` (Qt reads 8-digit hex as
  `#AARRGGBB` — never bypass this parser). Themes carry colours only,
  no motion tokens. A planned move to JSON under `resources/themes/` via
  a not-yet-existing C++ `ThemeCatalog` is noted in a comment — do not
  build motion tokens on that unbuilt catalog; put them in `Theme.qml`
  (§3).

### 2.2 Per-component motion inventory (verified, file:line where it matters)

| Component | Motion / effect today | Gap this plan fills |
|---|---|---|
| `components/AppButton.qml` (167 lines) — richest primitive. Root `Rectangle`; props `text`, `icon`, `highlighted`, `flat`, `buttonRadius` (= `Theme.radiusMedium`); aliases `pressed`/`hovered`; signals `clicked`, `pressAndHold`, `released`; Return/Space keys; `Accessible.Button`. | Hover scale 1.02 / press 0.96 (`Behavior on scale`, `durationFast`, asymmetric easing: `InCubic` on press, `OutCubic` on release — the comment explains why). Looping highlight glow ring (bordered `Rectangle`, margins −2, `PropertyAnimation on opacity` 0.3↔0.6, 1500 ms, `InOutSine`, infinite, only when `highlighted && !flat`). Focus ring: static border on `activeFocus`. Icon tint via `layer.effect: MultiEffect { colorization: 1.0 }` (colour only). Glass layer opacity `flat ? 0 : highlighted ? 0.3 : 0.85`. | No spring overshoot on release, no ripple/press halo, no success/error wiggle, no loading state, glow is a ring not a blur halo, focus ring does not animate. §4.1 extends this component — **do not replace its public API** (every view depends on `text`/`icon`/`highlighted`/`flat`/`clicked`). |
| `components/AppSlider.qml` (152 lines) — custom, *not* stock `Slider`. Props `from`/`to`/`value`/`stepSize`; aliases `pressed`/`hovered`; signal `moved(real value)`; hand-rolled `MouseArea` drag with step rounding. | Handle scale 1.05 hover / 1.1 press, `Behavior` at `durationInstant` (50 ms — fastest in the app). "Glow" is a static bordered rect around the filled track (opacity 0.5, visible on hover/press, **no animation, no `MultiEffect`**). Handle border 1→2 on hover/press (instant). Inner highlight static. | No track glow animation, no spring on release, no value bubble, no tick feedback, no keyboard parity discussion, filled-track width jumps (it is a binding, not an animated property). §4.2. |
| `components/AppSwitch.qml` (31 lines) — stock `Switch`, custom indicator. | Thumb slide only: `Behavior on x { NumberAnimation { durationFast } }`. Track colour switches instantly (no `ColorAnimation`). | No hover/pressed handling at all (`hoverEnabled` count 0), no scale, no glow. §4.6. |
| `components/AppComboBox.qml` (39 lines) / `AppTextField.qml` (24 lines) — stock controls, chrome only. | **Zero animation/effect code.** ComboBox popup uses stock animation only. TextField focus-border colour change is an instant binding (no `Behavior on border.color`). | §4.6 gives both a focus/hover treatment consistent with `AppButton`, without restyling the popup from scratch. |
| `components/AppColorPicker.qml` (392 lines) — HSV + alpha, modal popup centred on `Overlay.overlay`. Props `value`, `working`/`committed` (Cancel semantics), `hovered`; signal `picked(color)` (continuous while dragging). | Swatch colour fade (`Behavior on color`, `durationFast`); hover ring opacity (`durationFast`); popup enter/exit opacity transitions (`durationFast`); gradients for SV area, hue strip (7-stop rainbow), alpha strip over a `Repeater` checkerboard (no `Canvas`, by design — comment explains). | Popup has no scale/spring entrance; swatch drag has no haptic-like tick. Low priority (§7 Phase 4). |
| `components/ClipCard.qml` (385 lines) — library grid tile. Props `clipData` (QVariantMap), `saveState` (idle/saving/saved/refused), `savedPath`, `saveErrorText`, `selectionMode`, `selected`, `isReady`/`canPlay`/`canSave`, `isHovered`; signals `opened`, `saveRequested`, `selectionToggled`, `contextRequested`. | Hover lift: scale 1.02 (`durationFast`, `OutCubic`) + border-width `Behavior` (`durationInstant`). Cover fade (`durationNormal`). Play overlay: opacity + scale 0.7→1.0 with `Easing.OutBack` — **the only overshoot easing in scope**. Scrim gradient (transparent → background @ 0.85). Deliberate decision (comment ~line 357): the save button avoids `highlighted` (looping glow) because a batch save could light a dozen cards at once — **preserve this decision**; use a one-shot pulse instead (§6.1). | No playing-state glow (a playing card looks like every other card), no save-progress shimmer, no selection wiggle, no drag-reorder motion (reorder is a playlist mutation, §6.2). |
| `components/ClipDetailSheet.qml` (510 lines) — modal `Popup`. Owns no bridge state except via `SunoBridge.loadClipDetail(id)` on `clipData` change and `loadClipDetail("")` on close. | **Zero own `Behavior`/`Transition`/`Animation`** — appears/disappears with stock `Popup` behaviour only. Its only motion is inherited from child `AppButton`s (Save `highlighted` on refusal → looping glow). Banner + scrim gradient. | Sheet entrance/exit, section stagger (ancestry → remixes → comments → similar), mutation controls (§6.2), similar-item navigation. |
| `components/PlaylistStrip.qml` (145 lines) — one-row playlist strip over `SunoBridge.playlists`. | Refresh spinner only: `rotation: playlistsLoading ? 360 : 0`, `Behavior on rotation { SequentialAnimation { duration: 900; InOutCubic } }` — **the same 900 ms spinner is copy-pasted in 4 places** (Library, Discover, Notifications, PlaylistStrip). Chip hover colour is an instant binding. | Consolidate into one `RefreshSpinner` component (§4.7); add chip press scale, open-playlist transition, drag-to-reorder affordance (§6.2). |
| `components/TransportBar.qml` (275 lines) — floating glass transport, shell-wide (T0321). Prev/play-pause/stop/next `AppButton`s (play-pause `highlighted` when playing → looping glow), seek `AppSlider` (`value: AudioBridge.position`, `to: AudioBridge.duration || 1`, `onMoved: AudioBridge.seek`), volume `AppSlider` (0–100 ↔ `volume*100`), record button (`highlighted` when `RecordingBridge.isRecording`), `FileDialog` open-file, `Connections` on recording error/state. Bar border switches instantly to `Theme.recording` while recording. | **Zero own `Behavior`/`Transition`/`Animation`/`MultiEffect`** — all motion inherited. Seek/volume move only because their `value` bindings change. | Beat/level-reactive glow (§5), recording pulse that is more than a border colour, seek-thumb glow while dragging, play/pause icon morph (scale snap on track change). |
| `components/NavRail.qml` (271 lines) — collapsible rail, 7 entries. | Rail width `Behavior` (`durationNormal`, `InOutCubic`, 224 ↔ 64). Label fades (`durationFast`). Entry background `ColorAnimation` (`durationFast`). Active indicator bar height (`durationNormal`, `OutCubic`). Icon tint via `MultiEffect` colorization. Collapse chevron rotation 180° (`durationNormal`, `InOutCubic`). | No entry press scale, no unread-badge pulse (notifications count exists — §6.4), no hover glow. |
| `components/PulseIndicator.qml` (44 lines) — pulsing status dot. Props `active`, `baseColor` (default `Theme.recording`), `size` 8, `dimOpacity` 0.3, `periodMs` 500. | Only animation: `SequentialAnimation on opacity` (infinite, `dimOpacity` ↔ 1.0). **No scale pulse, no glow halo — a flat fading dot.** Used by `main.qml` header (play dot 800 ms / 0.5, REC dot), `VideoView` REC badge, `RecordingPanel`. | Add optional scale + halo variant for hero states (recording, generation rendering) without changing the default dot (§4.4). |
| `components/VisualizerOverlay.qml` (120 lines) — most animation-dense file in scope. `Repeater` over `OverlayBridge.overlays`; per-delegate defaults guard `undefined` (`x ?? 0.5`, `opacity ?? 1.0`, `glow === true`, `animation ?? 0`). | Glow backdrop: `Rectangle` (colour, opacity 0.15, visible when glowing), `SequentialAnimation on scale` 1.0↔1.05, 500 ms each, `InOutSine`, infinite, **gated on `AudioBridge.isPlaying`** — a true breathing scale, clock-driven not beat-driven. Text shadow via `MultiEffect` (shadow, blur 0.5, offset 2,2) + `Text.Outline`. Animation index: 0 static; 1 fade pulse (opacity ↔ 0.3×, 1000 ms, `InOutQuad`); 2/3 scroll left/right (x, `root.width` ↔ `-text.width`, 5000 ms); 4 bounce (y ±5% height, 500 ms `OutQuad`/`InQuad`). | Make the breathing beat-aware (§5) instead of clock-only; add scale/rotation variants as new animation indices (do not renumber existing 0–4). |
| `components/SectionHeader.qml` (19 lines) / `components/AccountChip.qml` (103 lines). | SectionHeader: **nothing** — plain accent `Text`. AccountChip: press-only scale 0.97 (`durationInstant`), **no hover scale** (unlike `AppButton`); glass colours; `ToolTip` 400 ms. Shows user name, plan badge, `credits + " cr"`, else "Suno · sign in via Settings". | AccountChip hover parity (§4.6); SectionHeader stays static by design (headers must not compete with content motion). |
| `views/LibraryView.qml` (644 lines) — main grid. Grid: `cellWidth = floor(width/columns)`, `columns = max(2, floor(width/cardTileMinimum))`, delegate `ClipCard` centred by a **static** `transform: Translate` (not animated). | Refresh spinner (same 900 ms pattern). Grid entry `add: Transition` (opacity 0→1 + scale 0.92→1.0, `durationNormal`, `OutCubic`) — **the only `add` transition in scope**. `BusyIndicator` for empty state + footer; search clear icon fades instantly (no `Behavior`); selection bar height `visible ? 44 : 0` (instant). | `remove`/`move`/`displaced` transitions, staggered entry, selection-bar slide, playlist-lens transition (§6.1). |
| `views/CreateView.qml` (230 lines) — generation + upload. Model default `"chirp-v3-5"`; Generate → `SunoBridge.submitGeneration(prompt, tags, "", instrumental, model, "")`; upload `FileDialog` (M4A only) + progress bar (`width: parent.width * audioUploadProgress / 100`, **instant binding, no `Behavior`**); embeds `SunoPanel`. | **Zero own `Behavior`/`Transition`/`Animation`/`MultiEffect`** — all motion inherited. | Generation state machine visuals (§6.3) — the highest-value reactive surface after mutations. |
| `views/DiscoverView.qml` (307 lines) / `views/NotificationsView.qml` (249 lines). | Refresh spinner (same 900 ms pattern); `boundsBehavior: Flickable.StopAtBounds`; `BusyIndicator`. Bespoke `Rectangle` tiles in Discover (not `ClipCard`s, so they lack even the card hover lift). | List transitions, tile hover parity (reuse `ClipCard` or extract its hover behaviour — do not duplicate it a third time), unread pulse (§6.4). |
| `views/ListenView.qml` (145 lines) — Now Playing + Queue + Lyrics. `compact: width < 760` (1- vs 2-column `GridLayout`). | **Zero animation/effect/transform code of any kind.** Only animated child is its `PulseIndicator`. | The most static view in the app and the natural home for audio-reactive chrome (§5, §6.5). |
| `views/VideoView.qml` (222 lines) — projectM surface + dock. `WindowContainer` embedding `VisualizerBridge.visualizerWindow`; `VisualizerOverlay`; `KaraokeMaster` (`showGlow: true`, `accentColor: Theme.accent`); REC badge; dock width `min(360, max(300, width*0.34))`. | Drawer handle: opacity (`durationFast`) + rotation (`durationNormal`, `InOutCubic`); side dock slide on `x` (`durationNormal`, `InOutCubic`). | Preset-change flash / crossfade cue (§6.5); dock content motion parity. |
| `main.qml` (695 lines) — shell. `ApplicationWindow` 1400×900 (min 800×600); 6 views; `SettingsWindow`; global `TransportBar`; shortcuts (play/pause, next/prev, record, preset, fullscreen, `M` = rail toggle). | **Only one pattern: view cross-fades.** Each view: `opacity: visible ? 1 : 0` + `Behavior on opacity { durationNormal, OutCubic }`. Header/footer chrome, rail, transport have no own animation. | View transitions with slide + fade (§4.5); keep `VideoView` persistent (it must never be unloaded — `WindowContainer` owns the native projectM `QWindow`). |
| `panels/SunoPanel.qml` (205 lines) — legacy "Create Magic" + library list (also embedded in `CreateView`). | **Zero own motion** — child `AppButton`s + stock `BusyIndicator` only; `ListView` paging via `onAtYEndChanged`. | Decide: deprecate in favour of `CreateView` *or* give it the same generation-state visuals (§6.3). Do not maintain two divergent generation UIs silently — record the decision in `TODO.md` in the same commit. |
| `panels/PresetsPanel.qml` (116 lines) / `panels/LyricsPanel.qml` (215 lines) / `panels/RecordingPanel.qml` (373 lines). | Presets: list highlight (`highlightMoveDuration: 150`, accent @ 0.2); icon/star tint via `MultiEffect` colorization; row hover instant. Lyrics: list highlight (`highlightMoveDuration: 200`, accent @ 0.15, `highlightResizeDuration: 0`); `positionViewAtIndex(..., Center)` recentring; karaoke progress fill (`width: lineProgress * parent.width`, opacity 0.3, **instant binding** — the current-line change jumps; highlight move is the only easing). Recording: inherited motion only (`AppButton` glow/scale, `PulseIndicator` fade); stats bound to `RecordingBridge` (`framesWritten`/`fileSize`/`encodeFps`/`bufferHealth`). | Lyrics: spring the highlight + ease the karaoke fill (§6.5); Presets: row press scale + favourite-star pop (§4.1 pattern); Recording: buffer-health colour pulse only when unhealthy (never a permanent alarm). |

### 2.3 Global absences (verified by grep over all of `src/qml/`)

- **No `SpringAnimation`, no `ParticleSystem`/`Particle`, no `ShaderEffect`,
  no `DropShadow`, no `Glow` (Qt5Compat.GraphicalEffects) type anywhere.**
  The only "glow" implementations are a bordered ring (`AppButton`), a
  static bordered rect (`AppSlider`), an opacity-0.15 backdrop rect
  (`VisualizerOverlay`), and `MultiEffect` shadow/colorization blocks.
- **No `State`/`states`/`Transition` state machines and no `Timer`-driven
  UI animation** in the requested files (the only `Transition`s are
  `AppColorPicker` popup enter/exit, `LibraryView` grid `add`, and list
  `highlight` moves).
- **No rotation/scale wiggle/shake/bounce on any control** except:
  `ClipCard` play-button `OutBack` scale (hover only),
  `VisualizerOverlay` animation-index 4 bounce (text overlays only),
  and the 900 ms loading spinners (rotation 0→360, `InOutCubic`).
- **Easing variety is narrow:** `OutCubic` (cards, rail bar, view fades),
  `InOutCubic` (rail width, dock, chevrons, spinners),
  `InCubic`/`OutCubic` asymmetric press (`AppButton`),
  `InOutSine` (button glow, overlay glow pulse),
  `InOutQuad`/`OutQuad`/`InQuad` (overlay fade/bounce),
  `OutBack` (card play button). **No `OutElastic`, `OutBounce`,
  `InOutBack`, or spring physics.**
- **Motion is concentrated in 5 files:** `AppButton`, `ClipCard`,
  `NavRail`, `PulseIndicator`, `VisualizerOverlay` (+ `VideoView` dock +
  `main.qml` fades + `AppSlider`/`AppSwitch` micro-interactions). The
  other files have zero own animation and inherit only what
  `AppButton`/`AppSlider`/`PulseIndicator` give them.
- **Out-of-scope context, useful as the proven pattern:**
  `components/KaraokeMaster.qml` (~lines 117–125, 139–142) is the only
  place `MultiEffect` does true glow/blur/brightness/masking
  (`shadowBlur 0.8`, shadow opacity / blur / brightness all driven by
  `LyricsBridge.lineProgress`) — it is the proven pattern for
  audio-reactive glow in this codebase and §5 generalises it.
  `panels/PlaylistPanel.qml` (~251–255) uses a `MultiEffect` accent
  shadow. `src/qml/shaders/shadow.frag` exists but **no QML file
  references it** — either wire it deliberately in Phase 1 with a
  measured reason, or delete it in a cleanup commit; do not leave a
  third, orphaned glow path.
- **No audio-energy signal, no motion preference, no per-theme motion.**
  Those three gaps are Phase 0 (§3) and Phase 3 (§5), respectively.

---

## 3. Phase 0 — Motion system: one set of tokens, one kill-switch, no new singletons (yet)

**Goal:** Every later phase reads its durations, easings, and motion
preference from one place, so "make it snappier" is a one-line change and
"turn motion off" actually turns *all* motion off.

### 3.1 Add motion tokens to `Theme.qml` (do NOT create a new singleton)

Creating a new `Motion.qml` singleton requires a `QT_QML_SINGLETON_TYPE`
registration in `cmake/TargetSetup.cmake` and a seeding-order analysis
(§0 rule 4). That cost buys nothing Phase 0 needs: `Theme` is already
the singleton every component imports. Add to `Theme.qml`, next to the
existing durations, and document them in the same comment block:

```qml
// Motion — the whole app reads these; never hardcode a duration/easing
// in a component when a token exists. durationSlow/VerySlow were
// previously defined-but-unused; they now own sheet/panel motion.
readonly property int durationMicro: 80     // NEW: icon snaps, ticks
readonly property int durationPop: 200      // NEW: spring-backed pops (paired with OutBack, §4)
readonly property real motionScale: reducedMotion ? 0 : 1
// Easings are values, not strings: store the enum where QML allows it,
// otherwise document the canonical easing per motion class in comments
// and use it verbatim (see table below). Do NOT invent a per-component
// easing without adding it here first.
```

Canonical mapping (MUST — a reviewer should be able to grep for a
deviation and ask why):

| Motion class | Duration | Easing / animation | Used for |
|---|---|---|---|
| Hover / focus fade | `durationFast` (150) | `NumberAnimation`, `OutCubic` | Button hover scale, card lift, rail label fade, chip hover |
| Press / release | `durationFast` (150) | Asymmetric: `InCubic` on press, `OutCubic` on release (existing `AppButton` pattern — keep it) | All pressable controls |
| Pop / appear | `durationPop` (200) | `OutBack` (existing `ClipCard` play-button pattern) or spring (§3.2) | Favourite star, play overlay, toast entry, grid add |
| View / panel transition | `durationNormal` (250) | `OutCubic` (existing `main.qml` pattern) + slide (new, §4.5) | View cross-fade+slide, dock, rail |
| Sheet / modal | `durationSlow` (400) | `OutCubic` on enter, `InCubic` on exit + scale 0.96→1.0 | `ClipDetailSheet`, colour picker popup (upgrade from opacity-only) |
| Ambient / breathing | 1000–1500 | `InOutSine` / `InOutQuad` | Highlight glow ring, overlay breathing, recording pulse |
| Loading spin | 900 | `InOutCubic`, rotation 0→360 (existing pattern — consolidate, §4.7) | Refresh spinners |
| Spring settle | — (physics, not duration) | `SpringAnimation { spring: 3; damping: 0.35; mass: 1; epsilon: 0.005 }` for scale; `spring: 2; damping: 0.5` for position | Release overshoot, sheet settle, card return (§3.2) |

**Rules:**

- Animate `transform` (`scale`, `rotation`, `x`, `y` via `transform`) and
  `opacity` only. Animating `width`/`height`/anchors forces layout
  recalculation and jank. The existing `NavRail` width `Behavior` and
  `VideoView` dock `x` `Behavior` are grandfathered (they animate chrome,
  not list content) — do not add new width/height animations inside
  delegates or lists.
- Micro-interactions: 100–300 ms. Anything longer must be a panel/view
  transition or an ambient loop, and ambient loops are limited to a few
  hero elements (see §8 budgets).
- Never animate a property that is also bound to a rapidly changing
  bridge value without a `Behavior` that can be interrupted (springs and
  `SmoothedAnimation` handle mid-flight retargeting; a fixed-duration
  `NumberAnimation` restarted every frame does not — see §5.2).

### 3.2 Springs: the one new primitive, with fixed tunings

Qt Quick has no `Wiggle` type, but it does have `SpringAnimation`
(`spring` 0–5, `damping` 0–1, `mass`, `epsilon`; for scale use
`epsilon: 0.005`, for pixels `0.25`). Use exactly these tunings unless a
profiler trace justifies a deviation, and name them in code comments:

```qml
// Canonical springs (Theme durations do not apply to springs —
// physics, not time, decides when they settle).
//   Gentle (cards, chips, sheet settle):  spring 2.0, damping 0.55, mass 1.0
//   Playful (pops, favourite star):      spring 3.0, damping 0.35, mass 1.0
//   Tight (handle return, toggle):       spring 3.0, damping 0.65, mass 1.0
//   NEVER damping 0 (undamped = oscillates forever) and never
//   damping < 0.2 on a control the user can trigger repeatedly —
//   rapid re-triggering with a bouncy spring reads as broken.
Behavior on scale {
    SpringAnimation { spring: 3.0; damping: 0.35; mass: 1.0; epsilon: 0.005 }
}
```

`SmoothedAnimation` is the companion for *tracking* targets (a slider
handle following a dragged value, a progress bar following
`audioUploadProgress`, a lyrics highlight following `currentLineIndex`):
velocity- or duration-driven, splices cleanly when the target moves
mid-flight. Use `SmoothedAnimation` for tracking, `SpringAnimation` for
settle/overshoot, `NumberAnimation` for everything else. Do not use all
three on the same property.

### 3.3 Reduced motion + intensity (the kill-switch)

Qt 6.8 has **no built-in equivalent of CSS `prefers-reduced-motion`**
(the Qt UI-design skill says so explicitly; Qt 6.12 adds
`QAccessibilityHints.motionPreference`, which this tree cannot use on
its 6.8 floor — see §9.3). Therefore:

1. **C++ (one property, Phase 0):** add `SettingsBridge.reduceMotion`
   (`bool`, persisted like `accentColor`/`backgroundColor`, default
   `false`) following the existing `SettingsBridgeSettings.inc` macro
   pattern (read that file first — `Q_PROPERTY` signatures there are
   macro-generated and moc has edge cases). Add a toggle in
   `SettingsWindow` → Appearance page, next to the theme selector.
   Seed it from `main.qml`'s `Component.onCompleted` (§0 rule 4).
2. **QML:** `Theme.motionScale` is `0` when `reduceMotion` is true,
   else `1`. Every `Behavior`, spring, particle burst, and ambient
   loop in §4–§6 MUST consult it. The reduced-motion rendering is an
   **instant transition to the final state, not a slower animation**:
   scale jumps to its target, opacity jumps, glow appears at full
   strength or not at all, particles do not emit, wiggle does not run
   (the error is still shown — colour + icon + text, §8).
3. **Pattern** (use verbatim; do not re-derive per component):

```qml
// Reduced-motion gate: when Theme.motionScale is 0, Behaviors must
// be disabled, not slowed. The binding below is the canonical form.
Behavior on scale {
    enabled: Theme.motionScale > 0
    NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic }
}
scale: Theme.motionScale > 0 ? (pressed ? 0.96 : hovered ? 1.02 : 1.0)
       : 1.0   // reduced motion: no scale change at all
```

A component that animates without consulting `motionScale` fails review.
Test it: toggle reduced motion in Settings, then exercise every screen
in §7 — nothing should move except view opacity cross-fades collapsing
to instant (which is also gated).

### 3.4 Phase 0 acceptance criteria

- [ ] `Theme.qml` gains `durationMicro`, `durationPop`, `motionScale`,
  and the canonical-mapping comment; no component hardcodes a duration
  that a token now covers (grep for `duration: 1[0-9][0-9]` outside
  `Theme.qml` should return only justified exceptions, each commented).
- [ ] `SettingsBridge.reduceMotion` persists across restart; toggling it
  in Settings immediately gates all existing animations (`AppButton`
  scale/glow, card lift, rail, spinners) — verified by manual run, not
  by reading code.
- [ ] `./scripts/lint-qml.sh` clean; `./build.sh --fast --tests` green;
  no new QML file left unregistered in `cmake/Sources.cmake`.
- [ ] `durationSlow`/`durationVerySlow` are now used (sheet/panel
  motion, §4.5) or explicitly documented as reserved — no longer
  "defined but unused".

---

## 4. Phase 1 — Primitives: juicy buttons, glowing sliders, wiggle, glow, transforms

**Rule for the whole phase: extend, do not replace.** `AppButton`,
`AppSlider`, `AppSwitch`, `AppComboBox`, `AppTextField` are imported by
every view. Their public API (property names, signal names, signal
argument types) MUST stay backward-compatible. New behaviour is opt-in
via new properties with safe defaults, so an old caller gets the new
motion for free and a new caller can tune it.

### 4.1 JuicyButton — extend `AppButton.qml`

Keep: root `Rectangle`, `text`/`icon`/`highlighted`/`flat`/`buttonRadius`,
`pressed`/`hovered` aliases, `clicked`/`pressAndHold`/`released`,
keyboard, `Accessible.Button`, glass layer, focus ring.

**Add (all new properties default to the current behaviour, so existing
callers are unaffected except for the spring release, which is the point):**

| New property | Type / default | Behaviour |
|---|---|---|
| `springRelease` | `bool`, default `true` | Release uses the playful spring (§3.2) instead of plain `OutCubic`. Press stays `InCubic` (a decision feels instant; a release may overshoot). |
| `ripple` | `bool`, default `false` | Press halo: a circle expanding from the press point, fading out in `durationNormal`. Opt-in because a ripple on every transport button at once is noise; enable it on hero buttons (Generate, Record, Save) first. Implementation: a `Rectangle` with `radius: width/2`, scale 0.2→1.4 + opacity 1→0, clipped to the button (`clip: true` on root — note: root currently does not clip; adding `clip: true` changes the glow-ring overflow, so test the ring after enabling). |
| `loading` | `bool`, default `false` | Replaces content with a spinner (reuse `RefreshSpinner`, §4.7) and disables clicks. MUST be driven by a bridge busy flag (`SunoBridge` loading/mutationsBusy, `RecordingBridge` burn-in), never by a local timer. When `loading` clears, the button pops (§4.1 pop rule). |
| `successFlash` / `errorFlash` | `bool`, write-once triggers (set true, component resets to false after the flash) | One-shot feedback: success = accent glow pulse + scale pop 1.0→1.06→1.0 (spring); error = shake (§4.3) + error-colour ring for 600 ms. These are *triggers*, not states — a caller sets them from a settled signal (§6.2), the button animates once and clears. |
| `glowStrength` | `real`, default `1.0` (0 = no glow) | Scales the highlight glow ring opacity and, when `glowBlur` is true, a `MultiEffect` halo (§4.4). Lets `ClipCard` keep its "no looping glow on batch saves" decision (§2.2) while still allowing a one-shot pulse. |

**Press/hover/focus specification (the contract a test can assert):**

```
enabled=false  → scale 1.0, cursor Forbidden, no hover/press reaction
                 (existing gating on root.enabled MUST be preserved —
                 the MouseArea stays enabled when the button is not, so
                 its cursor can still say "not now"; both pressed and
                 containsMouse must be gated on root.enabled).
hovered        → scale 1.02, border → Theme.accent, glow ring opacity 0.3
pressed        → scale 0.96 (InCubic, durationFast)
released       → spring to 1.0 (playful spring) or 1.02 if still hovered
highlighted    → looping glow ring 0.3↔0.6, 1500 ms, InOutSine (existing)
focus (keyboard) → focus ring visible; on Return/Space, the same press
                 scale + spring release as a mouse click (today keyboard
                 activation has no press visual — fix it).
reduced motion → all scales 1.0, glow ring static at its hover opacity,
                 ripple off, success = colour change only, error = colour
                 + text only (no shake).
```

**Do NOT:** add a second `MouseArea` (one already exists; stacking them
breaks `pressAndHold`); change `implicitWidth`/`implicitHeight` math
(buttons in rows depend on it); restyle `flat` buttons into raised ones
(`flat` is used for icon-only toolbar buttons — their motion is scale
only, no glow ring, as today).

**Qt Quick Test cases to add** (see `qt-qml-test` skill): hover scale
reaches 1.02; press scale reaches 0.96; disabled button never scales;
`loading=true` suppresses `clicked`; `errorFlash` triggers exactly one
shake and resets; reduced-motion mode produces no scale change.

### 4.2 ReactiveSlider — extend `AppSlider.qml`

Keep: `from`/`to`/`value`/`stepSize`, `pressed`/`hovered` aliases,
`moved(real value)` signal, hand-rolled drag with step rounding,
`normalizedValue`, `handleWidth` 16, `trackHeight` 6.

**Add:**

1. **Animated fill + handle glow.** The filled track gets a real glow:
   a `MultiEffect` shadow/blur halo in `Theme.accent` whose opacity
   follows `hovered || pressed` (0 → 0.6, `durationFast`) and, while
   dragging, pulses subtly with the handle. The handle gets a
   `MultiEffect` accent shadow when hovered/pressed (not permanently —
   a permanent halo on every slider in Settings is visual noise).
   Replace the current static bordered "glow" rect with this; delete
   the static rect in the same commit (one glow system per component).
2. **Spring release.** Handle scale: pressed 1.15 (up from 1.1 — the
   current 1.1 is barely visible at 16 px), hover 1.08, rest 1.0.
   Press uses `durationFast`; release uses the tight spring (§3.2).
   The current `durationInstant` (50 ms) snap stays for the *press*
   direction only — at 50 ms a grow reads as flicker on release, which
   is why the release must be the spring.
3. **Value bubble (opt-in, `showValueBubble: bool`, default `false`).**
   While dragging (and only while dragging), a small pill above the
   handle shows the formatted value. The pill scales in with `OutBack`
   on drag start and fades out on release. Formatting is a caller
   property: `valueText: function(v) { return v }` default; TransportBar
   passes `Theme.formatTime`, volume passes `v + "%"`. Never show the
   bubble on a slider the user is not touching — a persistent bubble
   over every Settings slider is clutter.
4. **Tick feedback (opt-in, `ticks: var`, default `[]`).** When `ticks`
   is non-empty, the track draws tick marks and the handle *snaps*
   with a tiny scale pop when crossing a tick while dragging. Use for
   volume detents and recorder CRF steps; do not invent ticks for
   continuous sliders (seek, karaoke position).
5. **Tracking animation.** When `value` changes from a bridge (seek
   position, upload progress) rather than from dragging, the handle
   and fill must *track* smoothly: use `SmoothedAnimation` on the
   handle `x` (or on `value` via a `Behavior` that is disabled while
   `pressed` — a `Behavior` fighting the user's drag is the classic
   slider bug). Rule: `Behavior on value { enabled: !pressed;
   SmoothedAnimation { velocity: … } }`. The drag path writes `value`
   directly and must never be smoothed.
6. **Keyboard parity.** Today the slider is mouse-only (no Keys handler).
   Add Left/Right (and Shift+Left/Right for ×10) stepping by `stepSize`,
   with the same handle pop as a drag tick. The seek slider in
   `TransportBar` is the first beneficiary; test it with keyboard only.

**Existing callers to update in the same phase (not later):**
`TransportBar` seek + volume (glow + bubble with `formatTime` / `%`),
`RecordingPanel` slider, `VisualizerSettings` / `AudioSettings` sliders
via `AppSlider` where they use it (grep for `AppSlider` — do not assume
the list). Settings sliders that are *not* dragged often (e.g. mesh
resolution) keep `showValueBubble: false`.

### 4.3 Wiggle / shake / jiggle — one reusable component, strict usage rules

**New file:** `src/qml/components/Wiggle.qml` (register it — §0 rule 1).
It is *not* a visual item; it is an animation controller attached to a
target. The canonical implementation is a `SequentialAnimation` on
`rotation` and `x` — there is no built-in wiggle type, and the Qt
"Wiggly Text" example pattern (per-element bindings + spring) is the
reference for the spring variant:

```qml
// Wiggle.qml — attach to any Item; call wiggle.shake() / wiggle.nudge().
// Rotation wiggle (error/refusal): 0 → -3° → 3° → -2° → 0, 350 ms total.
//   Each step: NumberAnimation { duration: 70; easing.type: Easing.InOutQuad }
//   (5 steps × 70 ms; amplitude decays — that decay is what makes it read
//   as a shake, not a vibration).
// Nudge (attention, "look here"): x 0 → 4 → 0, once, 180 ms, OutCubic —
//   used for "new notification" / "generation finished, come look".
// Jiggle (playful, opt-in only): continuous low-amplitude rotation
//   ±1.5°, 1200 ms loop, InOutSine — ONLY for hero/celebratory moments
//   (first successful generation, first playlist created). NEVER on
//   a control the user must click while it jiggles; jiggle stops on
//   hover/press (a moving target is hostile UX).
// Reduced motion: shake() and nudge() do nothing visible (the caller
//   must still show colour + text — see §8). jiggle never runs.
// Guard: if a shake is already running, ignore a second trigger
//   (rapid error spam must not queue shakes — it must coalesce into
//   one shake + one message).
pragma ComponentBehavior: Bound
import QtQuick
import ChadVis
QtObject {
    id: root
    required property Item target
    property bool enabled: Theme.motionScale > 0
    function shake() { /* start rotation SequentialAnimation on target */ }
    function nudge() { /* start x SequentialAnimation on target */ }
    function jiggleStart() { /* start looping rotation animation */ }
    function jiggleStop() { /* stop + reset rotation to 0 */ }
}
```

**When to wiggle (exhaustive — if it is not on this list, do not wiggle):**

| Trigger | Animation | Example |
|---|---|---|
| Mutation / save / generation refused or failed | Shake (rotation) + error ring 600 ms | `ClipDetailSheet` Save refused; `CreateView` submit 422; playlist add with all ids in `failures` |
| Invalid input submitted (empty prompt, bad playlist name) | Shake on the offending field/button | Create prompt empty; "New playlist" with empty name |
| New item needs attention (not an error) | Nudge (x) once | Notification badge when `unreadCount` increases; "generation finished" toast |
| Celebratory milestone (first generation, first playlist) | Jiggle, once, then stop | Hero Generate button after first `generationClipsReady` — once per session, never looping forever |
| Anything else | **Nothing.** No wiggle on hover, no wiggle on every click, no wiggle on load. A UI that wiggles constantly is a UI the user learns to ignore, and then the error wiggle is invisible. |

**Transform note:** Wiggle animates `rotation` (and `x` for nudge) on
the *target*. The target's `transformOrigin` MUST be `Item.Center`
(the default) unless the design explicitly wants a hinge (e.g. a
notification bell swinging from its top — then set
`transformOrigin: Item.Top` and document it). Never wiggle `scale`
and `rotation` simultaneously on the same target — the combination
reads as a glitch, not feedback.

### 4.4 Glow system — from bordered rings to real halos, with a budget

**Today's glow is three different hacks** (bordered ring, static
bordered rect, opacity-0.15 backdrop). Phase 1 unifies them into two
primitives, both built on `QtQuick.Effects.MultiEffect` (Qt 6.5+, one
pass for blur + shadow + colorize — the successor to chained
Qt5Compat Graphical Effects; do not introduce `Qt5Compat.GraphicalEffects`):

1. **`GlowFrame` (new component, `src/qml/components/GlowFrame.qml`).**
   A `Rectangle` wrapper that applies a `MultiEffect` shadow halo in
   `Theme.accent` (or a caller colour) with properties:
   `glowColor` (default `Theme.accent`), `glowRadius` (default
   `Theme.glowAccent.radius` = 12 — finally consuming that token),
   `glowOpacity` (0–1, animated), `glowEnabled` (bool — the layer is
   *transient*: enabled only while glowing, see budget below).
   Usage: wrap a button/card/handle that needs a halo; do not put a
   `GlowFrame` on every delegate (see budget).
2. **Activate the dormant tokens.** `Theme.glowAccent` and
   `shadowSmall/Medium/Large` become the *defaults* for `GlowFrame`
   and for card shadows. A component that hardcodes a shadow colour
   or radius instead of reading these tokens fails review — with one
   exception: `Theme.recording` (#ff0000 family) for REC states, which
   is semantic, not decorative.

**Glow budget (MUST — profiled, not guessed):**

- At most **one live-blur halo per screen** at rest (the currently
  playing card, the recording button, *or* the generating button —
  never all three at once if they are on the same screen).
- **Transient layers:** `layer.enabled` renders the subtree to an
  offscreen texture. Enable it just before a glow/transform animation
  and disable it when the animation finishes; set `layer.samples`
  (4/8) + `smooth: true` if layered rotation/scale looks jagged; a
  zero-size item with a layer renders nothing (debug that first if a
  glow disappears).
- **Never** put a live blur/shader on every list delegate. Pre-render
  static glows as images, or use a bordered ring (cheap) for delegates
  and reserve `MultiEffect` blur for the selected/playing/hero item.
- Cap blur radius (≤ 16 for UI chrome; the KaraokeMaster
  `shadowBlur 0.8` scale is a different unit — read that file before
  copying numbers), cap particle counts (§4.8), and keep infinite
  glow loops to hero elements only.
- **Static glow alternative:** for cards in a grid, prefer the existing
  bordered ring + `border.color: Theme.accent` on hover/playing, and
  reserve the blur halo for the *one* playing card. The user must be
  able to tell "this is playing" at a glance without the whole grid
  glowing.

**KaraokeMaster is the reference implementation** for glow driven by a
bridge value (`lineProgress` → shadow opacity / blur / brightness).
Read it before writing `GlowFrame`'s animated properties — it already
solved the "glow follows a value" problem; `GlowFrame` generalises it
to any 0–1 value, not just lyrics progress.

### 4.5 Transforms & transitions — views, lists, sheets

**View transitions (`main.qml`).** Today: opacity-only cross-fade
(`durationNormal`, `OutCubic`). Upgrade to fade + slide, keeping the
existing opacity `Behavior` and adding a *single* slide that does not
fight the `VideoView` persistence rule:

```
Outgoing view: opacity 1→0, x 0→-24 (OutCubic, durationNormal)
Incoming view: opacity 0→1, x 24→0 (OutCubic, durationNormal)
Direction: forward navigation (rail top→bottom order) slides left;
  backward slides right. The rail order is the source of truth for
  direction — do not derive it from view names ad hoc.
VideoView: opacity only, NO slide (its WindowContainer owns a native
  QWindow; sliding it is both pointless and risky). This exception
  must be commented in main.qml.
Reduced motion: opacity jumps instantly, x stays 0.
```

Do NOT unload views to animate them (all 6 views are persistent
today for a reason — `VideoView` must never be unloaded, and the
others keep bridge state). A `StackView` rewrite is out of scope for
Phase 1; the fade+slide on the existing persistent views is the
deliverable.

**List transitions.** Add, in this order of priority:

1. `LibraryView` grid: add the missing `remove`, `move`, and
   `displaced` transitions to match the existing `add` (opacity +
   scale 0.92→1.0). `displaced` MUST be `NumberAnimation { properties: "x,y"; duration: durationNormal }` — without it, items teleport when
   the grid reflows. Test by deleting a clip from the grid and by
   switching the playlist lens (§6.1).
2. `PlaylistStrip` chips: `add`/`remove` scale+fade (same tokens).
3. `NotificationsView` / `DiscoverView` lists: `add` fade+slide from
   below (y +12→0) for newly arrived items only (a full-list stagger
   on every refresh is nausea, not polish — see stagger rule below).
4. `LyricsPanel` / `PresetsPanel` highlight moves already exist
   (150/200 ms); spring the lyrics highlight (§6.5) rather than
   rewriting the highlight mechanism.

**Stagger rule:** Staggered grid entry (each card delayed by
`index * 15 ms`, capped at 150 ms total stagger) is allowed **only on
first load of a view**, never on refresh, filter, or pagination. A
refresh that re-staggers 100 cards is the most common "juicy UI"
mistake in this codebase's future — forbid it in review.

**Sheet / popup transitions.** `ClipDetailSheet`: enter =
opacity 0→1 + scale 0.96→1.0 (`durationSlow`, `OutCubic`); exit =
opacity 1→0 + scale 1.0→0.98 (`durationFast`, `InCubic`). Same for the
colour picker popup (upgrade from opacity-only). `AppColorPicker`
already defines enter/exit `Transition`s — extend them, do not add a
second transition mechanism.

**Transforms catalogue (what to animate, and how):**

| Transform | When | How |
|---|---|---|
| `scale` | Hover (1.02), press (0.96), pop (1.06 spring), card lift | `Behavior` / spring, `transformOrigin: Center` |
| `rotation` | Spinners (0→360), chevrons (0/180), wiggle (±3°), bell swing (hinge) | `Behavior` / `SequentialAnimation`, explicit `transformOrigin` |
| `x` / `y` (via `transform: Translate` or direct `x`/`y` on chrome) | Dock slide, view slide, nudge, lyrics scroll | `Behavior`, never inside a delegate's layout (use `transform`, not anchors, for transient motion) |
| `transform: [Rotation, Scale, Translate]` | Combined 3D-ish tilt on hero cards (optional, Phase 4) | Explicit transform list, `transformOrigin` documented; test on HiDPI (layered textures blur if sizes are not DPR-aware) |

**Never animate:** `width`/`height` inside delegates, `anchors`, `Layout`
preferred sizes, or `font.pixelSize` (text reflow during animation is
jank + accessibility breakage).

### 4.6 Parity pass — Switch, ComboBox, TextField, AccountChip

- **AppSwitch:** add hover scale on the thumb (1.1) + track colour
  `ColorAnimation` (`durationFast`) instead of an instant switch.
  Keep the existing thumb `x` `Behavior`. No glow (a glowing switch
  in every Settings row is noise).
- **AppComboBox:** add focus/hover border `ColorAnimation`
  (`durationFast`, `Theme.accent` on focus, `Theme.border` otherwise)
  matching `AppTextField`'s focus rule, and a chevron rotation on
  popup open (0→180, `durationFast`). Do NOT custom-animate the popup
  itself in Phase 1 — stock popup animation is correct; custom popup
  motion is a Phase 4 nicety.
- **AppTextField:** add `Behavior on border.color { ColorAnimation {
  durationFast } }` so focus does not snap. Add error shake via
  `Wiggle` (§4.3) when a caller sets an `error` property (new, default
  `""`; non-empty = error ring + shake once on change, not on every
  keystroke).
- **AccountChip:** add hover scale 1.02 to match `AppButton` (today it
  is press-only 0.97). Keep the `ToolTip` (400 ms) and the
  `credits + " cr"` / plan badge content unchanged.

### 4.7 Loading states — one spinner, one progress rule

**New file:** `src/qml/components/RefreshSpinner.qml` — the 900 ms
rotation spinner currently copy-pasted in `PlaylistStrip.qml:77`,
`LibraryView.qml:234`, `DiscoverView.qml:58`, `NotificationsView.qml:64`.
Replace all four call sites with it in the same commit (a fifth copy
is a review failure). Props: `running` (bool), `size` (default 40,
matching the current icon-button size), `icon` (default the existing
expand.svg path — do not change the icon in the consolidation commit).

**Progress rule:** Any bar driven by a bridge progress value
(`audioUploadProgress`, generation poll, download progress) MUST ease:
`Behavior on width { SmoothedAnimation { … } }` (tracking, §3.2) or a
`Behavior` on the normalised value — never a raw binding that jumps.
`CreateView`'s upload bar is the first fix (§6.3); audit every
`width: … * progress / 100` binding in the same phase (grep for
`Progress` + `width:`).

### 4.8 Particles & celebration (small, bounded, opt-in)

Qt Quick Particles (`ParticleSystem`, `Emitter`, `ImageParticle`,
`Wander`, `Turbulence`, `Gravity`) are available on the 6.8 floor and
are the right tool for *one-shot* celebration — confetti/burst on a
track change is wrong, but a small burst on "first generation complete"
or "playlist created" is the kind of moment users remember. Rules:

- **One `ParticleSystem` per screen, max, bounded** (`maxAmount`,
  `emitRate` capped; burst, not continuous emission).
- **Never in a delegate.** Particles live on the view, triggered by a
  bridge signal (`generationClipsReady`, playlist-created settled),
  not per-card.
- **Reduced motion: no emission at all** (the toast + colour change
  carry the message).
- **Phase 4 only.** Do not build particles in Phase 1 — they are the
  easiest motion to overdo and the hardest to profile. When Phase 4
  arrives, profile with `qmlprofiler` (qt-qml-profiler skill) and
  delete the system if it drops frames on the target hardware.

### 4.9 Phase 1 acceptance criteria

- [ ] `AppButton` / `AppSlider` extended without breaking any existing
  caller (all existing views still load; `lint-qml.sh` clean; no
  public property/signal renamed or removed).
- [ ] `Wiggle.qml`, `GlowFrame.qml`, `RefreshSpinner.qml` exist,
  registered in `cmake/Sources.cmake`, and used by at least one real
  caller each (not just a demo page).
- [ ] All four spinner copies replaced by `RefreshSpinner`.
- [ ] `Theme.glowAccent` / shadow tokens have at least one real
  consumer each.
- [ ] Qt Quick Test cases for button scale/loading/error, slider
  spring/bubble/tracking, wiggle coalescing, reduced-motion gating.
- [ ] QML Profiler trace on Library + Transport screens shows no new
  dropped frames vs. baseline (record the trace, do not assert it
  from memory).

---

## 5. Phase 3 (read before Phase 2's UI, build after Phase 1) — Audio-reactive UI

> Ordering note: §5 is written before §6 because §6's view designs
> assume §5's signals. Build order is still §7: Phase 0 → 1 → 2 → 3 → 4.
> Do not build §5's C++ before Phase 1's primitives exist to consume it.

### 5.1 What exists today, and why it is not enough

- `AudioBridge.isPlaying` (bool) — drives the transport play-pause
  icon, the `VisualizerOverlay` breathing scale gate, and
  `PulseIndicator` activity. Binary: playing or not.
- `AudioBridge.position` / `duration` (ms) — drives seek + time labels.
  Continuous, but at seek granularity, not at audio-energy granularity.
- `LyricsBridge.lineProgress` / `wordProgress` (0–1) — drives the
  lyrics fill bar and, in `KaraokeMaster`, glow/blur/brightness.
  Per-position, but lyrics-gated (instrumental tracks have no words).
- `VisualizerOverlay` breathing scale — clock-driven (500 ms
  `InOutSine` loop), gated on `isPlaying`. It *looks* audio-reactive
  and is not; on a quiet intro it pulses exactly as hard as on a drop.
- **Missing:** smoothed level, beat pulse, band energies. The projectM
  visualizer *has* this data internally (it renders from PCM), but no
  bridge exposes it to QML. That is a C++ gap, not a QML gap — QML
  cannot invent audio data from `isPlaying`.

### 5.2 The C++ addition (one bridge extension, throttled, smoothed)

**Add to `AudioBridge` (or a new `AudioEnergyBridge` singleton if the
reviewer prefers a separate surface — but do not create two sources of
truth for "is playing"):**

| New property | Type | Contract |
|---|---|---|
| `audioLevel` | `qreal` 0–1 | Smoothed RMS/peak level, attack fast / release slow (e.g. attack ~50 ms, release ~300 ms — tune by ear, not by spec). Smoothing happens in C++, not in QML. |
| `beatPulse` | `qreal` 0–1, decays to 0 | 1.0 on a detected beat, decaying exponentially. QML reads it as a *value*, not an event — a bool `beat` signal that QML must catch is racy; a decaying value is always readable. Beat detection itself: reuse the visualizer's existing beat sensitivity (`SettingsBridge.visualizerBeatSensitivity`) — do not invent a second beat detector with different tuning. |
| `bandLow` / `bandMid` / `bandHigh` | `qreal` 0–1 each | Smoothed band energies, same attack/release as `audioLevel`. Only if the audio pipeline already computes bands; if it does not, ship `audioLevel` + `beatPulse` first and add bands in a follow-up — do not block Phase 3 on an FFT pipeline that does not exist. |
| `energyActive` | `bool` | False when nothing is playing *or* when the audio device is unavailable. QML must check this before glowing — a glow driven by stale energy after playback stops is a bug. |

**Throttling (MUST):**

- Emit to QML at **~30 Hz maximum** (every ~33 ms). Raw per-frame
  FFT bindings thrash QML bindings and are the #1 cause of
  "reactive UI drops frames" in audio apps. The spring in QML (§3.2)
  does the smoothing between 30 Hz samples — that is what springs are
  for.
- Never emit on the realtime audio callback thread. Marshal to the GUI
  thread (queued signal) — the 2026-10-23 commit "keep the realtime
  callback off the GUI thread" exists for a reason; do not regress it.
- When `energyActive` is false, `audioLevel`/`beatPulse`/bands decay
  to 0 in C++ (do not leave a frozen 0.8 glowing after stop).
- Headless / CI: these properties must have safe defaults (0 / false)
  and must not require an audio device to construct the bridge. A
  green CI run is not evidence for audio (repo AGENTS.md) — verify
  energy on a real run with real audio, and record that observation.

**QML consumption pattern (canonical — copy this, do not re-derive):**

```qml
// Beat-reactive glow on the transport play button (and, by the same
// pattern, on the playing ClipCard border and the ListenView hero).
// audioLevel is 0–1, smoothed in C++, throttled to ~30 Hz (§5.2).
// The spring does the inter-sample smoothing; QML must NOT add its
// own Timer-based smoothing on top.
GlowFrame {
    glowOpacity: AudioBridge.energyActive && AudioBridge.isPlaying
                 ? 0.25 + AudioBridge.audioLevel * 0.55 + AudioBridge.beatPulse * 0.2
                 : 0
    // Clamp in the binding's spirit: glowOpacity is a 0–1 property;
    // if the sum above can exceed 1, Math.min(1, …) it explicitly.
    Behavior on glowOpacity {
        enabled: Theme.motionScale > 0
        SpringAnimation { spring: 2.0; damping: 0.55; mass: 1.0 }
    }
}
```

**What audio-reactive must never do:**

- Never drive `width`/`height`/layout from energy (see §3.1).
- Never drive a *list* from energy (every card pulsing with the beat
  is unreadable — only the playing card, the transport, and the hero
  surface pulse).
- Never use raw energy as opacity directly (flicker). Always through
  a spring or `SmoothedAnimation`, with a floor (e.g. base 0.25) so
  quiet passages do not make the UI vanish.
- Never beat-pulse on every beat at full amplitude for a whole song.
  Beat pulse is a *kick* (short, decaying) on top of a level glow —
  the level carries the song, the beat accents it.

### 5.3 Audio-reactive mapping table (the design, view by view)

| Surface | Driven by | Effect (all gated on `motionScale` + `energyActive`) |
|---|---|---|
| Transport play-pause button | `audioLevel` + `beatPulse` | Glow halo strength (§5.2 pattern); icon scale 1.0 + `beatPulse * 0.04` (subtle — the button must remain clickable and legible) |
| Playing `ClipCard` (only the card whose clip is `currentTrack`) | `audioLevel` | Border → accent + bordered-ring glow opacity follows level; non-playing cards stay static (see §4.4 budget) |
| `ListenView` Now Playing hero | `audioLevel` + bands (if shipped) | Hero background gradient intensity + title glow; band energies drive three small bars (low/mid/high) — a level meter, not a spectrum analyser (do not draw 64 FFT bins in QML) |
| `VisualizerOverlay` breathing | `beatPulse` (new) + existing clock loop | Replace the pure clock loop with: scale = 1.0 + `beatPulse * 0.03` + clock breathing 0.01. The overlay must still breathe gently when no beat is detected (quiet intro), and must not freeze when energy stops (decay to the clock baseline). Existing animation indices 0–4 are unchanged; beat-awareness is an *additional* modulation, not a renumbering. |
| `KaraokeMaster` glow | `lineProgress` (existing) + `audioLevel` (new) | Keep the existing `lineProgress` → glow/blur/brightness mapping; add `audioLevel` as a secondary multiplier (quiet lyric line glows less). Do not replace the lyrics mapping — it is the proven pattern. |
| Lyrics fill bar | `lineProgress` / `wordProgress` (existing) | Ease with `SmoothedAnimation` (tracking, §3.2) instead of the current instant width binding; the fill must not jump on every position update. |
| NavRail / Library grid / Settings | — (nothing) | Deliberately static. Audio-reactive chrome belongs on playback surfaces, not on navigation or settings. |

### 5.4 Phase 3 acceptance criteria

- [ ] `audioLevel` / `beatPulse` (+ bands if the pipeline allows)
  exist, are throttled to ~30 Hz, smoothed in C++, decay to 0 on stop,
  and are safe headless (0 / false, no device required).
- [ ] Transport glow, playing-card glow, and overlay beat modulation
  work on a real run with real audio (record the observation — CI
  green is not evidence here).
- [ ] Profiler trace: no new dropped frames on the Listen + Transport
  screens vs. Phase 1 baseline.
- [ ] Reduced motion + `energyActive=false` both collapse all
  audio-reactive effects to their static baselines.

---

## 6. Phase 2 & 4 — Suno surfaces, made reactive (design per view)

Phase 2 is *only* §6.2 (mutations) + §6.1 (library) + §6.3 (generation
state) + §6.4 (notifications) in their minimal reactive forms.
Phase 4 polishes the rest (§6.5, particles, hero tilts, Figma tokens).
The designs below are written once, so Phase 2 does not paint itself
into a corner Phase 4 must repaint.

### 6.1 LibraryView + ClipCard + PlaylistStrip

**Library grid:**

- **Playing state (Phase 2):** the card whose clip is playing gets:
  border `Theme.accent`, a bordered-ring glow (cheap, §4.4), and —
  after Phase 3 — a level-following halo. No other card glows. The
  playing card is found by comparing `clipData.id` to
  `AudioBridge.currentTrack` (document the exact key in code — today
  `currentTrack` exposes `title`/`artist`; if it does not expose an id,
  add it in the Phase 2 C++ change rather than matching on title,
  which collides on duplicate titles).
- **Save states (Phase 2):** `saveState` already exists
  (idle/saving/saved/refused). Today the transitions are silent except
  for a tooltip. Add: saving = card's save button shows `loading`
  (§4.1) + a subtle shimmer on the cover (opacity pulse, *not* a
  looping glow — see the batch-save comment in §2.2); saved = one-shot
  pop (§4.1) + check icon; refused = shake (§4.3) + error text (the
  existing `saveErrorText` — show it, do not just tooltip it).
  Batch saves: at most one pop at a time (stagger pops by 60 ms if
  several settle in the same frame; a dozen simultaneous pops is noise).
- **Selection (Phase 2):** selection bar slides in (y + height
  `Behavior`, `durationNormal`) instead of appearing instantly
  (`visible ? 44 : 0` today). Selected cards get an accent border +
  check badge pop (`OutBack`). Exiting selection mode slides the bar
  out and clears badges with a fade (not a snap).
- **Playlist lens transition (Phase 2):** opening a playlist from
  `PlaylistStrip` cross-fades the grid (opacity, `durationNormal`)
  *and* slides the strip's active chip into place; closing returns to
  the full library with the same transition in reverse. The grid must
  not re-stagger on lens change (stagger rule, §4.5 — first load only).
- **Grid transitions (Phase 2):** add `remove`/`move`/`displaced`
  (§4.5). Test: delete a clip, switch lens, filter — items must glide,
  not teleport.
- **Discover tiles (Phase 4):** Discover's bespoke `Rectangle` tiles
  must gain the same hover lift as `ClipCard` — by reusing `ClipCard`
  or extracting its hover behaviour into a shared component. Do not
  write a third hover implementation.

**PlaylistStrip:**

- Chips: press scale (§4.1 pattern), active chip = accent background
  + `ColorAnimation` on change (today hover colour is instant).
- Refresh: `RefreshSpinner` (§4.7).
- **Reorder (Phase 4, after §6.2 mutations land):** drag a chip/card
  to reorder → `reorderPlaylistTracks` (§1.3 #10) with a displaced
  transition (§4.5) + optimistic UI (move locally first, revert with a
  shake + toast if the settled signal reports failure — never leave
  the UI in a state the server rejected).
- **New playlist (Phase 2 if §1.4 is prioritised, else Phase 4):**
  "New playlist" button → inline name field (shake on empty, §4.3) →
  `POST /playlist/create/` (§1.4 #2, one new service method) →
  success pop + toast + open the new (empty) playlist lens.
  Rename/description/cover already have wired verbs (§1.3 #6, #11) —
  expose them as a playlist "…" menu in the strip (Phase 2): rename =
  inline title edit (web UI pattern, §1.5), description = small
  editor, cover = image-id picker with the §1.3 #11 warning surfaced
  ("cover needs an image id, not a clip id" — show that error
  verbatim if the server returns `cover_source_not_found`).

### 6.2 ClipDetailSheet — the mutation menu (Phase 2's centrepiece)

**This is the highest-value slice in the whole plan: 11 wired,
captured, gated verbs with zero QML callers.** The sheet already
renders ancestry, remix count, comments, and similar from
`clipDetail`. Phase 2 adds a mutation section to the sheet (and, for
clip verbs, a context menu on `ClipCard` that opens the same controls
— one menu component, two entry points, not two menus).

**Layout (do not redesign the sheet — extend it):**

```
[Banner + title + Play / Save / Show file (existing)]
[Based on … ancestry list (existing)]
[Remix count line, only if > 0 (existing)]
[NEW: Actions — a row of icon buttons + a "…" overflow menu]
  Row (always visible, disabled while !mutationsAvailable):
    [Public/Private toggle] [Remix: allow] [Show remixes] [Share]
    [Like] — ONLY if §1.4 like/unlike is shipped in the same phase;
             otherwise the Like button is absent, not disabled-with-
             a-mystery-tooltip (a disabled Like that can never work
             is worse than no Like).
  Overflow menu (…):
    Feedback (Like/Dislike reason picker → setClipFeedback)
    Add to playlist… (playlist picker → addClipsToPlaylist)
  [NEW, playlist context only — when the sheet is opened from a
   playlist lens:]
    Remove from this playlist → removeClipsFromPlaylist
    (Sheet needs to know its playlist context; if it does not today,
     pass openPlaylistId through — do not guess it from the grid.)
[Comments list (existing, read-only — writes are capture-gated, §1.5)]
[Similar list (existing; similar-item click navigation is Phase 4 —
  today items render without a verified click action, so do not
  invent one: Phase 2 keeps them non-navigable and documents that)]
```

**Control behaviour (MUST):**

- Every mutation control is disabled while `mutationsBusy` is true,
  and while `!mutationsAvailable` — in the latter case the section
  shows `mutationsUnavailableReason` **verbatim** as its subtitle
  (never "Something went wrong", never an invented reason).
- On invoke: the invoking button shows `loading` (§4.1) until its
  settled signal arrives. The sheet does not close on invoke (the
  user must see the result).
- On `clipMutationSettled(clipId, verb, outcome, reason)`:
  - Success → button success flash (§4.1) + toast (see toast spec
    below) + refresh the affected sheet field (e.g. visibility toggle
    reflects the new `is_public`; remix toggles reflect the returned
    `can_remix`/`show_remix` — do not assume the toggle flipped just
    because the call returned; read the settled payload).
  - Failure → button shake (§4.3) + error ring 600 ms + toast with
    `reason` verbatim.
  - Partial (playlist add/remove with `successes`/`failures`) →
    toast "3 added, 1 failed" (exact counts from the payload) +
    shake only if *all* failed.
- **Optimistic UI is forbidden for mutations** except playlist reorder
  (§6.1, where the revert path is specified). A visibility toggle
  that flips before the server answers and then silently stays flipped
  on failure is a data-integrity bug wearing a UI costume.
- **Toast component (new, `src/qml/components/Toast.qml`, Phase 2):**
  bottom-centre, above the transport (`z` above `globalTransport`),
  slides + fades in (`durationNormal`, `OutCubic`), auto-dismisses
  after 4 s (success) / 6 s (error, with a "Dismiss" button), queues
  (max 3 visible, older ones fade out — never stack 11 toasts from a
  batch). Toast content: verb in plain words ("Made public",
  "Remix allowed", "Added to playlist X"), never the raw verb enum
  (`SetVisibility`) and never a raw endpoint path. Reduced motion:
  fade only, no slide.

**Playlist mutations (same toast + loading + shake rules):**

- Playlist "…" menu (in `PlaylistStrip` / library header when a
  playlist is open): Rename, Edit description, Set cover (image-id
  picker, §6.1 warning), Add clips (multi-select from library →
  `addClipsToPlaylist`, 1–100 ids per call — chunk larger selections
  in QML or refuse with a clear message; do not send 101 ids and let
  the server fail), Remove clips, Reorder (drag, §6.1), Trash
  playlist (only if §1.4 #3 is shipped; 204 = success, no body parse;
  confirm dialog first — trash is destructive and the web UI's
  Recover/Delete are *not* wired, so there is no undo to offer).
- `playlistMutationSettled` drives the same toast system. After a
  successful metadata change, `PlaylistStrip` + `LibraryView` header
  must reflect the new name/description without a full refresh
  (the settled payload carries the playlist object — use it).

### 6.3 CreateView — generation as a visible state machine (Phase 2)

Today `CreateView` is a form + a captcha notice + an upload bar that
jumps. The bridge already exposes the full state machine — the UI
just does not *show* it. Wire every state to a distinct visual:

| `generationState` / bridge flag | Visual (all via `AppButton`/`Toast`/progress, §4) |
|---|---|
| Idle / ready | Generate button enabled (auth + prompt non-empty + `generationAvailable`); hero button with ripple (§4.1) enabled. |
| `preflight` | Generate button `loading` (§4.1); status line "Checking…" (verbatim from `generationStatus` if non-empty, else that word — never invent a more specific claim). |
| `submitting` | Generate button `loading`; progress: indeterminate shimmer (not a fake percentage — the submit has no progress value, so do not invent one). |
| `rendering` (poll 4 s × 90) | Generate button `loading`; **determinate-ish progress**: poll count / 90 as a `SmoothedAnimation` bar (§4.7) labelled "Rendering… (poll N/90)" — the poll count is real, so the bar is honest. On poll exhaustion, error state (below), not a silent return to idle. |
| Success (`generationClipsReady`) | Button success flash + pop (§4.1) + toast "Song ready — open in Library" (with an action that navigates to Library) + optional one-shot particle burst (Phase 4 only, §4.8) + jiggle on the Library nav entry *once* (nudge, §4.3 — not a loop). New clips appear in Library with the grid `add` transition (§4.5). |
| `generationCaptchaRequired` | **Captcha card, not a generic error.** Text: the token must come from a generation started at suno.com/create (the existing notice's wording — keep it, it is accurate). Action: "Open suno.com/create" (system browser, same pattern as the OAuth loopback's browser launch) + "I have a token" is **absent** — there is no in-app token input (§1.1), and Phase 2 must not add a fake one. The Generate button shakes (§4.3) once when the user tries to submit while captcha-required, to make the block unmissable. |
| `generationError` / `generationUnavailableReason` | Error card + Generate button error flash + shake (§4.3) + `generationError` verbatim. Never swallow the reason into "Failed". If `generationAvailable` is false, the Generate button is disabled with `generationUnavailableReason` as its tooltip (verbatim). |
| Upload (`audioUploadBusy` / `audioUploadProgress` / `audioUploadStatus` / `audioUploadError`) | Upload button `loading` while busy; progress bar eased with `SmoothedAnimation` (§4.7 — fix the current instant jump in the same commit); on error, shake + verbatim error; on success, success flash + toast. `FileDialog` stays M4A-only (do not widen formats without a service change). |

**SunoPanel decision (Phase 2, one paragraph in `TODO.md`):**
`SunoPanel` embeds a second generation form inside `CreateView`.
Either (a) delete `SunoPanel`'s generation form and keep it as a
library list only, or (b) give it the *same* state visuals as
`CreateView` via a shared `GenerationStatus` component. What is
forbidden is (c) two generation forms with divergent visuals. Extract
the status card into `src/qml/components/GenerationStatus.qml` and
use it in both places if (b) is chosen — that extraction is the
Phase 2 deliverable either way, so Phase 4 does not duplicate it.

### 6.4 NotificationsView — unread as a living badge

- **NavRail badge (Phase 2):** the notifications rail entry shows a
  badge with `unreadCount` (accent pill, count text). When
  `unreadCount` increases, the badge pops (`OutBack`) + nudges (§4.3)
  once. When it reaches 0 (mark-all-read settled), the badge fades
  out (not a snap). Today the rail has no badge at all — the count
  exists only inside the view.
- **List (Phase 2):** unread items get an accent left-bar + bolder
  title (existing `unread` styling — keep it); newly arrived items
  (list grows while the view is open) enter with fade+slide (§4.5),
  not by rebuilding the whole list. "Mark all read" button shows
  `loading` while the v2 read call is in flight, then success flash;
  on failure, shake + verbatim error (today failures are silent
  except for `notificationsError` text — surface it in the toast
  system, §6.2).
- **Per-notification actions (capture-gated, Phase 4+):** mark-one-read
  and notification → clip navigation have **no bridge method** (§1.1).
  Do not fake them with mark-all + filter. They need a capture first
  (§1.5), then a bridge method, then UI — in that order.
- **Refresh:** `RefreshSpinner` (§4.7). Badge count stays on v2
  (§1.1 — "there is no v3 badge route"); do not "fix" it to v3.

### 6.5 Phase 4 polish — Listen, Video, Lyrics, Presets, Settings, hero moments

- **ListenView (the static view):** Now Playing hero gains the §5.3
  audio-reactive gradient + level bars; queue (`PlaylistPanel`)
  gains `moveDisplaced` transitions (§4.5 — it already has a
  `moveDisplaced` on `y`, 200 ms; extend to `x,y` + scale for the
  playing item); track change = hero cross-fade + play-button pop,
  never a snap. Queue drag-reorder (if shipped) uses
  `PlaylistBridge.moveItem` with the same optimistic-revert rule as
  §6.1.
- **VideoView / Karaoke:** preset change (`nextPreset` /
  `previousPreset`) flashes the preset name (fade + scale pop,
  600 ms) over the visualizer; `KaraokeMaster` keeps its
  `lineProgress` glow mapping and gains the §5.3 level multiplier.
  REC badge: `PulseIndicator` with the new scale+halo variant (§2.2)
  while recording, plain dot otherwise.
- **LyricsPanel:** highlight move springs (§3.2 gentle spring) instead
  of the current 200 ms linear-ish move; karaoke fill eases with
  `SmoothedAnimation` (§5.3); current line scales 1.0→1.04 (subtle —
  lyrics must stay readable) + accent bar glows while the line is
  current. Word-level progress (`wordProgress`) drives a per-word
  underline sweep only if profiling shows it is free — otherwise
  line-level only (a per-word animation that drops frames is worse
  than no per-word animation).
- **PresetsPanel:** favourite star pops (`OutBack`) on toggle;
  blacklist action shakes the row once (it is a destructive-ish
  action — the shake says "this did something you should notice");
  random-preset button pops on `selectRandom` settling.
- **Settings / Account:** OAuth sign-in states (`googleLoginState`,
  `sunoMcpSignInState`, `authFailureKind`) get the same
  loading/success/error button treatment as §6.3 (today outcomes
  "surface from bridge state, not feedback spoofing" — keep that
  honesty, just make the state *visible*: loading spinner, success
  flash, error shake + verbatim `authFailureKind`). Theme picker:
  theme change cross-fades the whole window's accent (a
  `ColorAnimation` on the root background + accent-derived tokens is
  already binding-driven — add a 250 ms fade so a theme change does
  not flash; do not animate every token individually).
- **Hero moments (Phase 4, sparingly):** first generation, first
  playlist, first successful mutation — one jiggle + one toast +
  (Phase 4 only) one particle burst, then never again that session.
  Record "celebrated" flags in memory (not in config — a user who
  reinstalls should not be re-celebrated for the same milestone
  every launch, but a fresh install celebrating once is correct).
- **Figma tokens (Phase 4, optional):** if Derek supplies a Figma
  file, use the vendored `qt-figma-token-extraction` skill to emit
  `design-tokens.json` + QML singletons, and `qt-figma-component-generation`
  to generate controls — then *reconcile* the generated tokens with
  `Theme.qml` (two token systems is a bug, §0 rule 3). Without a
  Figma file, skip this entirely; do not invent a design system
  from screenshots.

---

## 7. Implementation plan — phases, files, acceptance, rollback

**Sizing:** slices, not hours. Each phase is independently shippable
and independently revertible. Do not batch phases into one commit —
repo practice is decisive commits with a real message, docs updated
in the same commit, and one fact owning one document (if this plan
and `ENDPOINT-INVENTORY.md` ever disagree on a route, the inventory
+ `src/` win — see §1.5 and `docs/README.md`'s hub rule).

### Phase 0 — Motion system (foundation)

- **Files:** `src/qml/styles/Theme.qml` (tokens, §3.1),
  `src/qml_bridge/SettingsBridge*` + `SettingsBridgeSettings.inc`
  (`reduceMotion` — read the macro pattern first),
  `src/qml/SettingsWindow.qml` + `src/qml/settings/AppearancePage.qml`
  (toggle), `src/qml/main.qml` (seeding, §0 rule 4).
- **Also:** gate the *existing* animations on `motionScale`
  (`AppButton`, `ClipCard`, `NavRail`, spinners, view fades) — Phase 0
  is not complete if reduced motion only gates *new* motion.
- **Acceptance:** §3.4 checklist. Manual run: toggle reduced motion,
  exercise Library / Create / Transport / Settings — nothing moves
  except instant state changes.
- **Rollback:** revert the `Theme.qml` token additions + the
  `reduceMotion` property; components fall back to their current
  hardcoded durations (keep those durations as the token *values*,
  so rollback is a deletion, not a redesign).

### Phase 1 — Primitives (the reusable kit)

- **Files:** `src/qml/components/AppButton.qml`,
  `AppSlider.qml`, `AppSwitch.qml`, `AppComboBox.qml`,
  `AppTextField.qml`, `AccountChip.qml` (extend, §4.1/4.2/4.6);
  **new** `Wiggle.qml`, `GlowFrame.qml`, `RefreshSpinner.qml`,
  `Toast.qml` is Phase 2 — do not pull it forward unless Phase 2
  starts early (it is small; pulling it forward is allowed, but
  record it).
  Register every new file in `cmake/Sources.cmake` in the same commit.
- **Also:** replace all four spinner copies; activate `glowAccent` /
  shadow tokens; fix `CreateView` upload-bar easing (§4.7) as the
  first tracking-animation proof.
- **Acceptance:** §4.9 checklist. Qt Quick Tests for each primitive.
  Profiler baseline recorded (save the trace / numbers in the
  commit message or a `STATUS/` log, per repo practice — not in this
  plan document, which must not become a run log).

### Phase 2 — Suno mutations + generation + notifications (the payoff)

- **Files:** `src/qml/components/ClipDetailSheet.qml`,
  `ClipCard.qml` (context menu entry point),
  `PlaylistStrip.qml`, `src/qml/views/LibraryView.qml`,
  `CreateView.qml`, `NotificationsView.qml`,
  `src/qml/components/NavRail.qml` (badge),
  **new** `Toast.qml`, `GenerationStatus.qml` (shared status card,
  §6.3), `src/qml/components/RefreshSpinner.qml` if not done in
  Phase 1.
- **C++ (only if Derek prioritises §1.4 in this phase):** one service
  method each for like/unlike, playlist create, playlist trash +
  bridge invokables + inventory corrections (T0348). If §1.4 is
  deferred, Phase 2 ships *without* Like / New-playlist / Trash
  buttons — absent, not disabled (§6.2).
- **Acceptance:** every §1.3 verb reachable from the GUI with
  loading + settled toast + failure shake; generation state machine
  visuals for every row of §6.3 (including captcha-required and poll
  exhaustion); notification badge + mark-all-read feedback; no new
  C++ endpoint guessed (grep the phase's diff for any path not in
  `SunoEndpoints.hpp` or the inventory — a hit blocks the phase).
- **Rollback:** the mutation menu + toast + badge can be hidden
  behind `mutationsAvailable` (already a bridge property) — if the
  menu must be pulled, it disappears cleanly because it never owned
  state, it only called invokables.

### Phase 3 — Audio-reactive (C++ energy + QML consumption)

- **Files:** `src/qml_bridge/AudioBridge.*` (or new energy bridge,
  §5.2), `src/qml/components/TransportBar.qml`,
  `ClipCard.qml` (playing glow), `src/qml/views/ListenView.qml`,
  `components/VisualizerOverlay.qml` (beat modulation),
  `components/KaraokeMaster.qml` (level multiplier),
  `panels/LyricsPanel.qml` (eased fill).
- **Acceptance:** §5.4 checklist. Real-audio observation recorded
  (CI green is explicitly *not* evidence for audio — repo AGENTS.md:
  headless runs skip GL/burn-in/audio and ctest counts a skip as
  success; always name the profile build directory and read the test
  count, never just the exit code).
- **Rollback:** `energyActive=false` collapses everything to Phase 1
  baselines (§5.4) — the rollback *is* the fallback, so it must be
  tested, not just designed.

### Phase 4 — Polish (views, particles, hero moments, Figma if supplied)

- **Files:** `ListenView`, `VideoView`, `LyricsPanel`,
  `PresetsPanel`, `Settings` theme fade, Discover tile parity,
  **new** particle burst (bounded, §4.8), hero tilt (optional).
- **Acceptance:** profiler traces on all six views; manual QA
  checklist `docs/dev/manual-qa.md` (MT-001) updated for the new
  motion; reduced-motion pass over every new animation.
- **Rollback:** per-surface — particles and hero tilts delete
  cleanly because they are additive layers, never structural.

### Phase 5 — MCP chat surface + capture-gated surfaces (separate track)

- **Do not start Phase 5 until Phases 0–2 are green.** It depends on
  §9.4's authenticated MCP tool enumeration (T0356b/T0357) and on HAR
  captures for §1.5 surfaces. Its GUI is a chat surface in ChadVis
  (TODO item T0357) that reuses `Toast`, `GenerationStatus`, and the
  motion system — it does not invent a second design language.
- **Acceptance (Phase 5 only):** live `tools/list` enumeration
  recorded; chat surface sends no Suno request that is not in the
  inventory; every capture-gated surface promoted to `[T1]` *before*
  its UI is wired.

### 7.1 File-by-file change checklist (the implementer's tick list)

```
Phase 0:
[ ] Theme.qml — durationMicro, durationPop, motionScale, canonical comment
[ ] SettingsBridge — reduceMotion (macro pattern, persisted, seeded in main.qml)
[ ] AppearancePage — reduceMotion toggle
[ ] AppButton / ClipCard / NavRail / spinners / view fades — motionScale gate

Phase 1:
[ ] AppButton — springRelease, ripple (opt-in), loading, success/error flash, glowStrength
[ ] AppSlider — MultiEffect glow, spring release, value bubble (opt-in), ticks (opt-in),
    SmoothedAnimation tracking (disabled while pressed), keyboard stepping
[ ] AppSwitch / AppComboBox / AppTextField / AccountChip — §4.6 parity
[ ] NEW Wiggle.qml, GlowFrame.qml, RefreshSpinner.qml (+ registration in Sources.cmake)
[ ] Replace 4 spinner copies; activate glowAccent + shadow tokens; ease upload bar
[ ] Qt Quick Tests: button, slider, wiggle, reduced motion

Phase 2:
[ ] ClipDetailSheet — mutation section + overflow menu (11 verbs, §6.2)
[ ] ClipCard — context menu entry point to the same mutation menu
[ ] PlaylistStrip + LibraryView — playlist menu (rename/description/cover/add/remove/reorder)
[ ] NEW Toast.qml, GenerationStatus.qml
[ ] CreateView — generation state visuals (§6.3 table, every row)
[ ] NotificationsView + NavRail — badge, mark-all-read feedback, RefreshSpinner
[ ] (Optional, if prioritised) §1.4: like/unlike, playlist create, playlist trash + T0348 doc fixes

Phase 3:
[ ] AudioBridge — audioLevel, beatPulse, bands (if pipeline allows), energyActive (throttled ~30 Hz)
[ ] TransportBar / playing ClipCard / ListenView hero / VisualizerOverlay / KaraokeMaster / LyricsPanel
[ ] Real-audio observation recorded; profiler traces; reduced-motion + energyActive=false fallbacks

Phase 4:
[ ] Listen / Video / Lyrics / Presets polish; Discover tile parity; theme-change fade
[ ] Particles (bounded, one system per screen, Phase 4 only) + hero moments (once per session)
[ ] Manual QA (MT-001) updated; full reduced-motion pass

Phase 5 (separate track):
[ ] Authenticated MCP tools/list enumeration (T0356b) → chat surface (T0357)
[ ] HAR captures → [T1] promotion → UI for one §1.5 surface at a time
```

### 7.2 Decisions Derek needs to make (defaults if he does not answer)

| Decision | Default this plan assumes (change it in one place: §3 tokens / §6) | Why it matters |
|---|---|---|
| Motion intensity (subtle vs. playful) | Subtle default: hover 1.02, press 0.96, pops via `OutBack`/gentle spring; playful spring only on pops and hero moments | A music tool is used for hours; playful-everywhere fatigues. Playful is opt-in per control, not global. |
| Reduced motion default | Off by default (motion on), toggle in Appearance, persisted | Matches the current app (motion on today); users who need it can find it next to themes. |
| Like button in Phase 2? | Only if §1.4 like/unlike ships in the same phase; otherwise absent | A disabled Like that can never work is worse than no Like (§6.2). |
| Playlist create / trash in Phase 2? | Create yes if one-method work is approved; trash only with a confirm dialog | Create completes CRUD; trash has no undo (Recover/Delete unwired). |
| Beat-reactive scope | Transport + playing card + Listen hero + overlay only (never nav/grid/settings) | §5.3 — audio-reactive everywhere is unreadable. |
| QML Live Preview investment | Defer until a Qt 6.12 build exists (§9.3); use `qmlpreview` CLI + profiler on 6.8 meanwhile | Live Preview MCP is 6.12-only; building the GUI workflow on it today blocks the work. |
| Figma? | Only if Derek supplies a file | No file = no Figma work (§6.5). Do not invent a design system from screenshots. |

---

## 8. Performance, accessibility, and testing guardrails (non-negotiable)

### 8.1 Performance budgets

- **Animate transform + opacity only** (GPU-composited). Animating
  `width`/`height`/anchors forces layout recalculation. Grandfathered
  exceptions: `NavRail` width, `VideoView` dock `x` (§3.1) — no new
  ones inside delegates.
- **`layer.enabled` transiently only** (§4.4). Offscreen textures cost
  memory and hurt scrolling; enable before a glow/transform animation,
  disable after. `layer.samples` 4/8 + `smooth: true` if layered
  rotation/scale looks jagged.
- **One effect, one pass.** One `MultiEffect` (or one Effect-Maker
  shader) per glowing item, never chained Glow → Blur → DropShadow.
  Cap blur radius (≤ 16 for UI chrome), particle counts, glow area.
  Pre-render static glows as images. Never a live blur/shader on
  every list delegate.
- **Micro-interactions 100–300 ms;** `Easing.OutCubic`/`OutBack` for
  press/release; springs only where overshoot is wanted; infinite
  animations (pulse, shimmer) limited to a few hero elements.
- **Throttle audio-driven properties to ~30 Hz** (§5.2); springs do
  the rest. Raw per-frame FFT bindings thrash bindings.
- **Verify with the profiler, not by eye.** Run the QML Profiler
  skill / `qmlprofiler` on every animated screen. A dropped-frame
  animation is a defect, not a style choice. Record traces; do not
  assert performance from memory.
- **HiDPI:** vector `Shape`/SVG icons; layered textures and `Canvas`
  need explicit DPR-aware sizing or they blur at 125%/200% scaling.
  Test glow/shadow padding at 125% and 200%.

### 8.2 Accessibility

- **Reduced motion (§3.3) is the headline rule**, but it is not the
  whole rule:
  - Motion must never be the *sole* state indicator. Pair every
    animation with colour + icon + text (playing = accent border +
    glow + "Playing" label, not glow alone; error = shake + error
    ring + error text, not shake alone).
  - Hover glow/scale must never be the only affordance — touch has
    no hover. Pressed states, focus rings, and `Accessible` names
    must work with keyboard + touch. (`QT_QUICK_CONTROLS_HOVER_ENABLED`
    can globally disable hover effects — document it in Settings
    help text if the toggle is ever exposed; do not set it silently.)
  - Focus rings animate (fade in, `durationFast`) but never disappear
    because of an animation — a focused control with no visible ring
    after a transition is a keyboard-accessibility failure.
  - Toasts must be announced to assistive tech (use `Accessible`
    announcements where the platform supports them; at minimum, the
    toast text must also appear in a persistent status surface —
    `SunoBridge.statusMessage` / the view's status line — so a user
    who misses a 4 s toast can still read it).
  - Colour contrast: every new glow/shadow colour must pass the
    existing WCAG checks (`Theme.contrastRatio` / `formatContrast`
    in `Theme.qml`; the T0119-measured 6.53–9.26:1 range for primary
    text is the bar — a glow that washes out its label fails).
  - Animation must not trigger vestibular discomfort by default:
    no full-screen motion, no rapid flashing (keep ambient loops
    ≥ 500 ms period, no strobe), no parallax on the visualizer chrome.

### 8.3 Testing (what "done" means per phase)

- **Qt Quick Tests** (`qt-qml-test` + `qt-qml-test-run` skills):
  `TestCase` + `SignalSpy` + `tryCompare` for: button hover/press/
  disabled/loading/error; slider spring/bubble/tracking/keyboard;
  wiggle coalescing (second trigger ignored while running);
  reduced-motion gating (no scale change); toast queue (max 3);
  generation state transitions (mock the bridge properties — do not
  call the real Suno API from a unit test); mutation settled →
  toast/shake mapping (mock `clipMutationSettled` payloads,
  including the partial `successes`/`failures` split).
- **qmllint:** `./scripts/lint-qml.sh` clean on every phase
  (filtered gate; `--strict` before merge). Never invoke `qmllint`
  by hand (§0).
- **QML Profiler:** traces for Library, Create, Listen, Video,
  Transport screens per phase (§8.1).
- **Manual QA:** `docs/dev/manual-qa.md` (MT-001) — extend it with
  the new motion checklist in Phase 4; real-GUI vs. offscreen
  coverage per item, as that document already distinguishes.
- **C++ tests:** every new bridge property/invokable gets a unit
  test in `tests/unit/qml_bridge/` (following
  `test_SunoBridge.cpp`, `test_SunoMutationsSection.cpp` patterns);
  every new service method gets a service test (following
  `test_SunoPlaylistService.cpp` etc.). Sanitizer profiles
  (`--tsan`/`--asan`) before merging any recorder/session-adjacent
  C++ (repo AGENTS.md) — audio-energy C++ touches the audio path,
  so it qualifies.
- **Regression guard:** the existing `test_ThemeQml.cpp`
  (`replacingOverridesInvalidatesTheTokens`) must keep passing —
  Phase 0 touches `Theme.qml` and must not break token invalidation.

---

## 9. External resources — repos, MCP servers, agent skills

> **Verification status, read this first.** Qt's own documentation
> and MCP endpoints below were verified by search in this pass
> (verbatim URLs in §9.1–§9.2). Third-party library homepages are
> named so an implementer can open them and verify branch + license
> before depending on them — **open each one once and confirm its
> default branch is Qt 6 before citing it in a commit message.**
> No dedicated, maintained "juicy UI / game-feel QML" library was
> found in this pass; the honest finding is that juicy QML motion in
> 2026 is composed from Qt primitives (spring + easing + MultiEffect
> + particles), not bought from a single library. Do not invent one
> in a commit message.

### 9.1 Use Qt's own stack first (in priority order)

1. **Qt Quick Effects — `MultiEffect`** (`import QtQuick.Effects`,
   Qt 6.5+). One pass for blur, shadow/glow, brightness, saturation,
   colorize; `autoPaddingEnabled` / `paddingRect` control clipping
   when animating blur/shadow. **The default choice for all glow in
   this plan.** Replaces chaining old Graphical Effects.
   Docs: `https://doc.qt.io/qt-6/qtquick-effects-qmlmodule.html`
   (verify the exact page from the Qt Docs MCP — the module page
   moves between Qt versions).
2. **SpringAnimation** — `spring` / `damping` / `mass` / `epsilon`.
   The spring-physics primitive (§3.2).
   Docs (verified): `https://doc.qt.io/qt-6/qml-qtquick-springanimation.html`
3. **SmoothedAnimation** — velocity/duration-driven tracking that
   splices cleanly when the target moves mid-flight (§3.2, §4.2, §5.2).
   Docs: Qt 6.8 `qml-qtquick-smoothedanimation` page (via Qt Docs MCP).
4. **Animation & Transitions overview** (`Behavior`, `Transition`,
   `State`) — states + transitions for button hover/pressed/disabled
   and panel open/close; `Behavior` for continuous values.
5. **Transforms** (`scale`, `rotation`, `transformOrigin`,
   `transform: [Rotation, Scale, Translate]`) — default
   `transformOrigin` is `Item.Center`; explicit `Rotation`/`Scale`
   give arbitrary origins and 3D axes. Animate these, never layout
   geometry (§3.1).
6. **Qt Quick Particles** (`ParticleSystem`, `Emitter`,
   `ImageParticle`, `Wander`, `Turbulence`, `Gravity`) — 2D
   burst/confetti/beat-pulse, cheap if `maxAmount`/`emitRate` are
   bounded (§4.8). 3D Particles (`Particles3D`) exist since Qt 6.2
   but are overkill for button/slider feedback — do not use them
   for UI chrome.
7. **Canvas / Context2D** — HTML5-like 2D drawing; good for custom
   waveform/level-meter/slider-track painting, bad for per-frame
   full repaints at 60 fps on large areas (prefer `Shape` or shaders
   for hot paths). The vendored `qt-canvas2d` skill documents the
   Qt 6.12+ painter APIs — on the 6.8 floor, verify each API against
   the Qt Docs MCP before using it.
8. **Qt Quick Effect Maker** — node-based shader builder,
   Shadertoy-compatible, combines effects into one shader; includes
   a "Wiggly" example. Best route for a custom wiggle/glow/bloom
   shader without hand-writing `.qsb` pipelines. `ShaderEffect` in
   Qt 6 requires compiled `.qsb` files (offline/build time,
   SPIR-V → HLSL/MSL/GLSL) — use Effect Maker to generate them
   rather than hand-rolling. Docs:
   `https://doc.qt.io/qt-6/qtquickeffectmaker-index.html`
9. **Qt Quick Timeline** — keyframe-based animation, supported by
   Qt Design Studio's timeline editor. Use for choreographed
   intro/transition sequences, *not* for per-interaction micro-motion
   (springs/`Behavior`s are simpler there). Still shipped in Qt 6.
10. **Qt Lottie Animation** (`import Qt.labs.lottieqt`,
    `LottieAnimation`, Qt 6.8+) — one-shot celebratory/loading
    animations exported from After Effects/Bodymovin. **Limits:**
    `labs` (no stability promise), shape layers only, integer frame
    mode, no expressions, only `Slide` and `Layer Fill` effects.
    Useful for exactly one thing in this plan: a Phase 4 celebration
    animation. Not a general button-motion system.
11. **Qt Quick Controls styling** — built-ins: Basic (fastest),
    Fusion, Material, Universal, macOS/iOS/Windows (platform-gated),
    FluentWinUI3 (WinUI 3 / Fluent, runs on all platforms per Qt
    docs). For this app: start from Basic or Material and customise
    `background`/`contentItem`/`handle` — do not restyle by swapping
    the whole style per control. The canonical pattern is "copy the
    Basic style's `Button.qml`/`Slider.qml`, override
    `background`/`handle`, animate `down`/`hovered`/`pressed`" — and
    for sliders, `visualPosition` is the correct handle/track anchor
    (RTL-aware). Docs: `qtquickcontrols-styles` + `qtquickcontrols-customize`
    pages (via Qt Docs MCP).

### 9.2 Open-source QML libraries — steal patterns, do not vendor blindly

**Rule:** Pattern-mine, do not adopt wholesale. Every library below
brings a dependency chain, a theming system, and a motion language
that would fight `Theme.qml`'s two-base model. The value is in
reading their button/slider/list-item code and copying the *pattern*
(e.g. how a ripple is clipped, how a chip animates its checked state)
into `AppButton`/`AppSlider` — with ChadVis tokens, not theirs.

| Library / demo | What to steal | Verdict / caution |
|---|---|---|
| Qt's own `qtquickcontrols2` + animation examples (Basic-style overrides, "Wiggly Text" springs: per-letter bindings + `Behavior { SpringAnimation { spring: 3; damping: 0.3 } }`) | The safest pattern source in existence for this plan | Maintained (Qt). Start here, always. |
| Awesome Qt/QML curated index | Discovery only | Verify each entry's own freshness before adopting anything from it. |
| FluentUI for QML (zhuzichu520) — Fluent Design, 50+ controls (`FluButton`, `FluProgressRing`, `FluInfoBar`, frameless window, light/dark theming) | `FluButton` hover/press motion + theming structure | **Caution:** its own README now says "PLEASE USE THE BRAND NEW FluentUI Pro INSTEAD!" — check what FluentUI Pro is and whether the free repo is still maintained before depending on it. Windows-centric (frameless window is Windows-only). MIT per its README snippet — verify in-repo. |
| KDE Kirigami — convergent QQC2 framework (page stack, `ColumnView`, cards, actions) | Navigation/page structure + convergent desktop/mobile behaviour | Maintained (KDE Frameworks 6). Value is structure, *not* juicy button physics. License not verified in this pass — check the repo before citing. |
| MauiKit (Maui Project) — convergent controls on Kirigami + QQC2 (`Chip`, `Badge`, `FlexListItem`, animated viewers) | Chip/badge/list-item motion patterns | Maintained, Qt 6 (v4.x). Heavy KDE dependency chain — pattern-mine only. LGPL-3.0 per a secondary snippet — verify in-repo. |
| Lirios Fluid — Material Design Qt Quick controls for Qt 6.8+ / C++23 (v2.0, `ListItem`, `AppBar`, `Units` singleton) | Material ripple/elevation patterns | **Mixed signal — check before use.** Described as modern Qt 6.8+ code, MPL-2.0, but an index mirror records its last commit "over 1 year ago". Successor to Papyros qml-material. |
| Papyros qml-material — original Material Design in QtQuick | Historical reference only | **Stale — last commit "over 8 years ago" per index mirror. Do NOT vendor.** LGPL-2.1 per its README snippet. Fluid (above) is its successor. |
| projectM (projectM-visualizer) — the visualizer ChadVis already embeds | The pattern that matters for §5: *driving QML properties from audio features* (beat, band energy, smoothed level) via C++-exposed properties. The visualizer itself stays in projectM; the chrome pulses from the same smoothed values, throttled (§5.2). | Maintained (updated Oct 2026 per org listing; `frontend-qt` Qt frontend). **License version conflict in sources** (LGPL-2.1 vs LGPLv3 in different mirrors) — check the repo LICENSE file; do not cite a version from memory. |
| Game-feel references (conceptual, not QML repos) | Squash-and-stretch on press (scale 0.94 + spring back), overshoot on appear, anticipation (tiny scale-up before a panel opens), beat-synced glow pulse | Adapt the *concept* with §3.2 springs. Do not claim a QML game-feel library exists — none was verified. |

**Do NOT introduce:** `Qt5Compat.GraphicalEffects` (`DropShadow`,
`Glow`, `GaussianBlur` as separate chained items) for new code —
compatibility only, multiple effects chained is slower than one
`MultiEffect` pass, and it is not in the default install on every
platform. **Do NOT build on Qt Labs StyleKit** (declarative
`Style { … pressed.background.scale: 0.95 }` with state-based styling
— snapshot/dev docs only, `Qt.labs` = no stability promise). Watch
it; copy its *pattern* (one motion system for all controls) by hand
today via §3 tokens.

### 9.3 MCP servers & agent skills — what exists, what is blocked on Qt version

**Already registered in this repo (verified — complete file read):**

- `.opencode/opencode.jsonc` registers **exactly one** MCP server:
  `qt-documentation` (`type: remote`,
  `url: https://qt-docs-mcp.qt.io/mcp`, `oauth: false`) — "Hosted Qt
  API documentation lookup (Qt 6.8 LTS / 6.11) — no auth required."
  **Not registered:** no QML Live Preview MCP, no Suno MCP, no other
  MCP entry. QML Live Preview appears only as a *skill reference doc*
  (`.opencode/skills/qt-qml/references/qmlpreview.md`) — documentation
  for the `qmlpreview` tool, not a registered MCP server.

**Qt official (highest priority for an implementing agent):**

- **Qt Agentic Tools hub** (verified):
  `https://doc.qt.io/agentictools/` — skills are BSD-3-Clause, work
  in Claude Code / Codex / Gemini CLI / GitHub Copilot; docs MCP
  serves Qt 6.8.4 (LTS) + 6.11.0.
- **Qt Documentation MCP** (verified):
  endpoint `https://qt-docs-mcp.qt.io/mcp`; tools
  `qt_documentation_search` (version/module/intent/type filters) +
  `qt_documentation_read`. Install: Claude marketplace plugin
  `qt-development` (auto-wires MCP), or manual HTTP client config.
  No local binary; hosted by The Qt Company. Blog (verified):
  `https://www.qt.io/blog/introducing-the-mcp-tool-for-qt-documentation`
  — "covering the latest Qt release and the latest LTS release…
  currently Qt 6.12 LTS, Qt 6.8 LTS, and Qt 5.15 LTS."
  **Use this for every Qt API question in §4–§5 instead of
  training-data recall** — especially: MultiEffect property names,
  `SpringAnimation` ranges, `SmoothedAnimation` velocity semantics,
  and whether a given API exists on the 6.8 floor.
- **Qt agent skills repo** (verified):
  `https://github.com/TheQtCompanyRnD/agent-skills` (also via the
  `qt-development` marketplace plugin). Families confirmed: QML
  Coding, QML Code Review (deterministic linter + parallel analysis,
  read-only, optionally invokes `qmllint`), QML Profiler (2D Qt Quick
  only; rendering/logic/memory/full modes; needs `qmlprofiler` from
  a Qt 6 install), Qt GUI Design / `qt-ui-design` (already encodes
  transform/opacity-only + reduced-motion rules — read it before
  Phase 0), C++ Code Review + Code Documentation, Qt Quick Test
  (generate & run), Figma Token Extraction + Figma Component
  Generation (emit `Colors/Typography/Spacing/Radii/Shadows/Motion.qml`
  singletons + `Theme.qml` from Figma via Figma MCP + token).
  **Note:** exact skill directory names (`qt-qml`, `qt-qml-review`,
  etc.) were *not* listed verbatim in search results in this pass —
  they are confirmed by the repo's own vendored copy (§9.3, in-repo
  list), which is the authoritative source for this project.
- **QML Live Preview MCP — the single biggest accelerator, and
  blocked on Qt version** (verified):
  blog `https://www.qt.io/blog/introducing-the-qml-live-preview-mcp-tool-for-agentic-development-of-embedded-devices`
  — "runs the app and hot reloads changed QML files, without a
  rebuild and without a restart"; exposes one tool, `run_qmlPreview`
  (pass the app executable path); installed via Qt Installer /
  Maintenance → "Agentic Development Tools" → "QmlPreview MCP
  Server" (binary lands in the Qt 6.12 `bin/` folder).
  **Limits (all verified): Qt 6.12+ only. QML only** (C++ / qmldir /
  CMake / new-resource-QML changes need a rebuild). Single
  `QQmlEngine`. QML must come from a `qt_add_qml_module()`.
  **Consequence for this plan:** ChadVis is on a 6.8 floor, so
  Live Preview MCP cannot be the workflow today. The 6.8 workflow
  is: `qmlpreview` CLI (exists in Qt 6.8, needs `QT_QML_DEBUG` /
  QML debugging enabled) + rebuild for C++/module changes +
  `qmlprofiler`. When/if the tree moves to Qt 6.12, Live Preview
  MCP becomes the Phase 1–4 workflow (agent edits motion, Derek
  watches it live) — record that as a follow-up in `TODO.md`, do
  not block Phase 0–2 on it. The `qt-qml` skill has been updated to
  steer Live Preview usage when the tool is connected *and* the
  project uses Qt 6.12+ (checks `QT_QML_DEBUG`, offers the exact
  CMake line, asks before editing `CMakeLists.txt`).
- **Figma to Qt (plugin, not MCP)** — runs inside Figma, previews
  as real QML, exports a `.qmlproject` ZIP; maps components to Qt
  Quick Controls and variables to `DesignTokens` modules. Use for
  designer handoff; use the Figma *skills* (above) when an agent
  must do the extraction. Needs a Figma file + API access — without
  either, skip (§6.5).
- **Qt Design Studio / Qt Creator QML Profiler** — non-MCP
  companions to the profiler skill.

**Third-party / generic (fallback only — Qt's own MCP wins for Qt APIs):**

- **Context7 MCP (Upstash)** — version-pinned docs for any library
  (`resolve-library-id` + `query-docs`); useful for non-Qt deps
  (e.g. an easing library being ported), **inferior to Qt Docs MCP
  for QML**. Install via `npx -y @upstash/context7-mcp` or hosted
  endpoint — verify the current install command from its docs
  before using it; do not copy a command from memory.
- **Community offline Qt docs MCP** (`qt4-doc-mcp-server`, despite
  the name serves Qt 4.8/5/6 local HTML docs via FTS5; PyPI page
  verified: `https://pypi.org/project/qt4-doc-mcp-server/`).
  Only if offline docs are required; Qt's hosted MCP supersedes it
  online.
- **Figma MCP (official Figma server, required by Qt's Figma
  skills)** — needs a Figma personal access token with read access
  to the target file; configure via the Figma-skills instructions
  in the agent-skills repo, not from memory.

**Vendored in this repo (`.opencode/skills/`, 34 directories, verified
listing — load these before writing code, per repo AGENTS.md: prefer
the Qt skill over the generic one when both apply, and refresh
vendored Qt skills from upstream rather than hand-editing them):**

| Skill | Role in this plan |
|---|---|
| `qt-qml` | Core: QML best practices for writing/fixing/optimizing QML; references `qmlpreview.md`, `qmllint`, `qmlls`, `qmlprofiler`, `qmltc`. Load first, every phase. |
| `qt-qml-review` | Deterministic lint (47+ rules) + six parallel analyses (bindings, layout, loaders, delegates, states). Run before merging every phase. |
| `qt-qml-profiler` | Profile QML perf (lag, stutter, dropped frames, binding/signal hotspots). Required evidence per §8.1. |
| `qt-qml-test` / `qt-qml-test-run` | Write Qt Quick Test cases / run `qmltestrunner` via CTest and report. Required for §8.3. |
| `qt-qml-docs` | Generate Markdown reference docs for new components (`Wiggle`, `GlowFrame`, `Toast`, `GenerationStatus`, `RefreshSpinner`). |
| `qt-ui-design` | Design/audit UI for Qt/QML. Already encodes transform/opacity-only + reduced-motion rules — read before Phase 0. |
| `qt-canvas2d` | Canvas2D painter APIs (gradients, shadows, patterns, transforms) — for custom slider tracks / level meters (§4.2, §5.3). Verify each API on the 6.8 floor via Qt Docs MCP. |
| `qt-figma-component-generation` / `qt-figma-token-extraction` | Figma → QML controls / tokens (§6.5, §9.1). Only with a Figma file + API access. |
| `qt-cpp-review` / `qt-cpp-docs` | Review/document bridge + service C++ (§5.2, §1.4 additions). |
| `qt-cmake-project` | Add QML sources/resources/targets in CMake — the exact failure mode `50ee0c6` fixed. Load before adding any file. |
| `suno-capture-client` | **Mandatory before any Suno wiring** — endpoints, auth, capture-derived routes, host allowlists, media. If this skill and this plan disagree on a route, the skill + inventory + `src/` win. |
| `chadvis-verify` | **Mandatory gate** — build/test/verify the way this tree actually works (profile-per-directory rule) before claiming a phase complete. |
| `projectm-gl-render` | projectM + OpenGL work — relevant only if reactive GUI touches the visualizer layer itself (it should not, except §5.3's overlay modulation). |
| `chadvis-audio-recorder` | FFmpeg/PCM recording pipeline — adjacent only (transport bar, recording UI). |
| `brainstorming` / `planning-and-task-breakdown` / `incremental-implementation` / `verification-before-completion` / `systematic-debugging` / `code-review-and-quality` / `documentation-and-adrs` / `git-workflow-and-versioning` / `ci-cd-and-automation` / `test-engineer` / `cpp-coding-standards` / `cpp-pro` / `modern-cpp` / `c-cpp-profi` | Workflow skills — use as the repo's AGENTS.md directs (planning before non-trivial work, thin verifiable slices, evidence before completion claims, root-cause-first debugging). `c-cpp-profi` is unlicensed upstream content — do not redistribute beyond this repo. |

### 9.4 Suno MCP Beta — what it is, what it is not (for Phase 5)

Server: `https://api.suno.com/mcp`, beta, early access via typeform
(24–48 h turnaround per the announcement), works on all plans
including Free; songs made through it save to the caller's library
and spend its credits. Connects to ChatGPT, Claude, Claude Code,
Cursor, Gemini, or any custom MCP client by URL + one-time sign-in
(no API key). Full record: `docs/suno_api/MCP.md` (165 lines,
updated 2026-10-31).

**Auth chain verified live 2026-10-31 (unauthenticated probes only):**
`GET api.suno.com/mcp` → 401 `{"error":"sign in to Suno to use this server"}`;
RFC 9728 pointer → `/.well-known/oauth-protected-resource/mcp` (200)
naming authorization server
`https://studio-api-prod.suno.com/api/v2/external/oauth`; RFC 8414
metadata complete (authorize/token/revoke_token/register/jwks,
PKCE S256, `client_id_metadata_document_supported: true`).
Resource scopes (8): `api:v0`, `credits:spend`, `download`,
`library:read`, `library:search`, `library:write`, `offline_access`,
`profile:read`. Authorization-server scopes (21): adds `read_profile`,
`generate_music`, `read_music`, `profile:write`, `create:read`,
`create:search`, `create:generate`, `create:upload`, `create:playback`,
`catalog:read`, `catalog:sync`, `catalog:link`, `studio:write`,
`telemetry:write`. **This closes the mapping side of T0356(b)** —
the loopback-registered client's token (stored under `'suno/mcp'`,
§1.1) is a bearer for this resource — but the authorized exchange
(mint a token for a registered `127.0.0.1` client and answer MCP
`initialize` with it) is **still untested** and is T0356(b)'s work.

**Tool inventory NOT FOUND:** `MCP.md` lists **no MCP tool names**
(no `generate`, `search`, etc.). Its "Announced capabilities" are
marketing prose at `[LEAD]` only: create conversationally
(plain-text prompt, assistant supplies lyrics/styles); upload own
audio (voice memos → full songs); remix anything (covers, mashups,
extend over library songs, from chat); use your voice (saved voices
sing new creations). Example prompts are "for prompt-engineering
later, not commitments."

**Consequence:** Any GUI/MCP-chat design (T0357: "Suno MCP Beta: chat
surface in ChadVis", open in `TODO.md`) MUST first enumerate the live
tool list with an authenticated `initialize` / `tools/list` call.
Do not assume tool names from the announcement, do not design a chat
UI around tools that may not exist, and do not register Suno MCP as
a helper MCP in `opencode.jsonc` until that enumeration exists —
Suno MCP is an *external server to be spoken as a client* (T0357),
not a registered development helper like Qt Docs MCP. Also:
`api.suno.com` is NOT yet a `CapturedHosts` entry (unauthenticated
probes do not count as capture) — Phase 5 must follow the repo's
host-allowlist rule (new hosts need a capture first; a lead is never
evidence) and the secret-handling rules (never commit or log a
bearer, cookie, `Authorization` header, or a full URL with a query
string — log `url.path()`, not `url.toString()`).

---

## 10. Do NOT do this (blocking list — a reviewer should reject on sight)

1. **Do NOT wire a `[LEAD]` route.** No guessed paths, verbs, or
   bodies. Capture → `[T1]` → service → bridge → QML, in that order
   (§1.5). This includes trash/restore, comment writes, remix lists
   beyond the count, queue management, hooks/voices/stem/remaster/
   spark/collab/labs, and anything from the web route manifest.
2. **Do NOT wire `platform.suno.com`, `/b-side/*` staff paths, or
   billing mutations/checkout.** Excluded by §1.5 and by the
   inventory (§5.10, T0146). No exceptions without a fresh capture
   *and* Derek's explicit approval.
3. **Do NOT invent a Turnstile token input, a like/dislike parser
   that requires `reaction_type` on unlike, a playlist-create body
   other than `{name, description}`, or a playlist-trash body other
   than flat `{"playlist_id":…}`.** The traps are documented in
   §1.1/§1.4 — hitting one means the implementer did not read them.
4. **Do NOT use Qt 6.12-only APIs on the 6.8 floor.** No QML Live
   Preview MCP workflow, no `QAccessibilityHints.motionPreference`,
   no Qt Labs StyleKit, no `qt-canvas2d` API that the Qt Docs MCP
   says is 6.12+. Verify every API against the 6.8 docs.
5. **Do NOT introduce `Qt5Compat.GraphicalEffects`, a second shadow/
   glow system, a second theme-apply path, a second token system
   (Figma tokens without reconciling with `Theme.qml`), or a second
   generation form with divergent visuals.** One system each (§0, §4.4, §6.3).
6. **Do NOT animate `width`/`height`/anchors/`font.pixelSize` inside
   delegates or lists; do NOT put a live blur/shader on every
   delegate; do NOT wiggle on hover, on load, or on every click;
   do NOT stagger a grid on refresh/filter/pagination; do NOT emit
   particles from a delegate; do NOT beat-pulse navigation or
   settings.** The budget and usage rules (§3, §4.3–§4.5, §5.3, §8)
   exist because each of these has been tried somewhere and regretted.
7. **Do NOT break public QML API.** No renamed/removed property or
   signal on `AppButton`, `AppSlider`, `AppSwitch`, `AppComboBox`,
   `AppTextField`, `ClipCard`, or any bridge. New behaviour is
   opt-in properties with safe defaults (§4).
8. **Do NOT leave a new QML file unregistered in
   `cmake/Sources.cmake`; do NOT invoke `qmllint` by hand; do NOT
   claim a phase complete without `./build.sh --fast --tests` +
   `./scripts/lint-qml.sh` + profiler traces + (for C++) the
   relevant unit tests.** See §0, §7, §8.3.
9. **Do NOT commit credentials, captures, cookies, tokens, account
   or clip identifiers, pasted private text, or complete media URLs.**
   Secrets go through `CredentialStore`; logs carry `url.path()`,
   never a bearer or a full query string (repo AGENTS.md).
10. **Do NOT push, open a PR, or touch `.github/workflows/*` on the
    authority of this document.** This document is a local design
    artifact. Pushing needs Derek's fresh explicit approval (§0 rule 7).
11. **Do NOT present a `[LEAD]` capability, an unverified library, or
    a stale inventory claim as fact in a commit message or a user
    reply.** If the inventory and `src/` disagree, `src/` wins; if
    this plan and the inventory disagree on a route, the inventory
    wins and this plan gets corrected in the same commit.
12. **Do NOT use `src/qml/shaders/shadow.frag` without either wiring
    it deliberately (with a measured reason) or deleting it.**
    An orphaned third glow path is how the next implementer wastes a
    day (§2.3).

---

## Appendix A — Bridge API reference (relevant subset, baseline 9831417)

Read the headers before coding — this table is a map, not a substitute
for the signatures (argument types and `NOTIFY` signals live in the headers).

**SunoBridge** (`src/qml_bridge/SunoBridge.hpp`):
properties ~40–138, invokables ~203–287, signals ~289+.
Key invokables: `generate`, `uploadAudio`, `refreshLibrary`,
`requestNextLibraryPage`, `searchLibrary`, `playClip`, `downloadClip`,
`downloadClips`, `sendChatMessage`, `fetchChatHistory`,
`beginGoogleSignIn`, `importSessionFromBrowser`, `beginSunoMcpSignIn`,
`cancelGoogleSignIn`, `signOutSuno`, `refreshAccount`,
`refreshDiscover`, `loadMoreDiscover`, `refreshNotifications`,
`submitGeneration`, `refreshPlaylists`, `openPlaylist`,
`requestNextPlaylistPage`, `closePlaylist`, `loadClipDetail`,
`markAllNotificationsRead`, plus §1.3's 11.
Key signals: `generationClipsReady`, `clipSaved(clipId, savedPath)`,
`clipSaveRefused(clipId, reason)`,
`clipDownloadSettled(clipId, state, reason)`,
`clipMutationSettled(clipId, verb, outcome, reason)`,
`playlistMutationSettled(playlistId, verb, outcome, reason)`,
`libraryRefreshRecommended`, `authenticationFailed(reason)`.

**AudioBridge:** `position`, `duration`, `volume` (RW), `isPlaying`,
`currentTrack` (QVariantMap); `play`, `pause`, `stop`,
`togglePlayPause`, `seek(positionMs)`, `next`, `previous`,
`setVolume`, `loadFile`.

**LyricsBridge:** `hasLyrics`, `lines`, `currentLineIndex`,
`currentWordIndex`, `lineProgress`, `wordProgress`, `isInstrumental`,
`title`, `artist`, `searchQuery` (RW), `searchResults`;
`seekToLine`, `exportToSrt/Lrc/Ass`, `assDocument`, `getLine`,
`getUpcomingLines`, `getContextLines`.

**VisualizerBridge:** `visualizerWindow` (QWindow*),
`active`, `currentPreset` (QString); `nextPreset`, `previousPreset`.

**RecordingBridge:** `isRecording`, `recordingTime`, `framesWritten`,
`fileSize`, `encodeFps`, `bufferHealth`, `videoCodec` (RW),
burn-in state (`burnInRunning`, `burnInInputFile`,
`burnInSubtitleFile`, `burnInOutputFile`, `burnInBlockedReason`);
`startRecording`, `stopRecording`, `refreshBurnInState`,
`burnInSubtitles`; signals include `recordingError(message)`,
`burnInFinished(outputPath)`.

**PresetBridge:** `presets`, `categories`, `currentPreset`
(QVariantMap), `currentIndex`, `presetCount`, `searchQuery` (RW),
`selectedCategory` (RW), `folder` (RW); `selectByIndex`,
`selectRandom`, `toggleFavorite`, `toggleBlacklist`, `setRating`,
`filteredPresets()`, `rescan`.

**PlaylistBridge:** `currentIndex`, `count`, `shuffle` (RW),
`repeatMode`; `addFiles`, `removeAt`, `clear`, `playAt`,
`toggleShuffle`, `setShuffle`, `cycleRepeatMode`, `moveItem`,
`getItemPath`.

**OverlayBridge:** `overlays` (QVariantList, RW; each map: `text`,
`x`, `y`, `opacity`, `color`, `fontSize`, `bold`, `glow`,
`animation`); `addOverlay`, `removeOverlay`, `updateOverlay`.

**SettingsBridge:** `version` (CONSTANT), audio
(`audioBufferSize`, `audioSampleRate`, `audioDevice`,
`audioDevices`, `audioDevicesAvailable`, `audioDeviceNotice`,
`audioError`, `audioOutputStatus`), visualizer
(`visualizerFps`, `visualizerMeshX/Y`, `visualizerBeatSensitivity`,
`visualizerPresetDuration`, `visualizerSmoothPresetDuration`,
`visualizerShufflePresets`, `visualizerAspectCorrection`),
recorder (`recorderCrf`, `recorderPreset`, `recorderWidth/Height/Fps`,
`recorderAudioCodec/Bitrate`), keyboard shortcuts, karaoke
(`karaokeFont`, `karaokeYPosition`, `karaokeEnabled`), Suno
(`sunoToken`, `sunoDownloadPath`), theme (`accentColor`,
`backgroundColor`, `themeName`, `sessionTheme` CONSTANT), shell
(`expandedPanel`, `sidebarWidth`, `drawerOpen`).
**New in Phase 0:** `reduceMotion` (bool, persisted).

## Appendix B — Endpoint quick reference (summaries only — inventory is master)

Base: `https://suno.com/a/api`. Full contracts, bodies, and evidence
grades: `docs/suno_api/ENDPOINT-INVENTORY.md` (sole API-spec master);
constants: `src/suno/SunoEndpoints.hpp`. If this table and either of
those disagree, those win.

- Feed/library: `POST /feed/v3`; Explore: `POST /unified/explore`;
  Following feed (constant, usage unverified): `/social/following-feed`.
- Playlists: `GET /playlist/me`; `GET /playlist/{id}/?page=N`;
  mutations §1.3 #6–#11; create/trash §1.4 (one method away).
- Generation: `POST /c/check {"ctype":"generation"}`;
  `POST /generate/v2-web/` (flat body, `task: "generate"`);
  poll `GET /clips/get_songs_by_ids?ids=…` (4 s × 90).
- Clip detail: `GET /clips/{}/attribution`;
  `GET /gen/{}/comments`; `GET /clips/remixes/count`;
  `GET /clips/get_similar/?id=`; constants-only:
  `GET /clips/parent`, `GET /clips/remixes`.
- Notifications: `GET /notification/v3?include_hooks=true`;
  badge `GET /notification/v2/badge-count`;
  mark-read `POST /notification/v2/read`.
- Mutations: §1.3 table (11 verbs, paths, bodies).
- Like/unlike (captured, no constant yet):
  `POST /gen/{id}/update_reaction_type/` (§1.4 #1).
- MCP: `https://api.suno.com/mcp` (Phase 5, §9.4 — tools/list
  enumeration required first).

## Appendix C — Verification commands (repo-correct)

```bash
./build.sh --fast --tests          # build + unit tests (fast profile, build-fast/)
./build.sh --fast --tests --safe   # + headless-safe entries
./build.sh --budget                # what this box can afford; builds nothing
# Sanitizers before merging recorder/session/audio-path C++:
./build.sh --tsan --tests && ./build.sh --tsan --safe
./build.sh --asan --tests && ./build.sh --asan --safe
./scripts/lint-qml.sh              # filtered qmllint via the build target
./scripts/lint-qml.sh --strict     # gate version
# ctest: ALWAYS name the profile directory and read the test count —
#   ctest --test-dir build-fast   (NOT bare `ctest --test-dir build`,
#   which finds zero tests and still exits 0)
# A green run is NOT evidence for GL, burn-in, or audio — those skip
# headless and ctest counts a skip as success. Only a real runtime
# observation counts there (repo AGENTS.md).
```

## Appendix D — Sources & provenance for this document

- Baseline tree: `/tmp/chadvis-main` worktree of `origin/main`
  @ `9831417` (2026-10-31); local checkout
  `~/workspace/chadvis-repo` was on `cmake/auto-install-deps`
  @ `a8359af` and was **not** switched (its unpushed commit is
  preserved) — this document was written into its `docs/dev/`
  so it survives, and must be moved/merged onto `main` by Derek
  (see the delivery note in chat, not in this file).
- Commits §1.1: `git log --all` + `git show --stat` on
  `7483bd0`, `dc8acc6`, `661d19a`, `bef6305`, `50ee0c6`, `a5d2ae8`,
  `96fdaab`, `4203403`, `c4257e4`, `1c1ecf8`, `fc4d2f4`, `f99710f`,
  `0871981`, plus `fe46e75`, `607781f`.
- QML inventory §2: direct reads of `src/qml/` (55 files) +
  greps for `Animation|Behavior|Transition|Spring|Easing|scale|
  rotation|opacity|layer|MultiEffect|ShaderEffect|DropShadow|Glow|
  Particle|Canvas|gradient` + bridge headers.
- Suno mapping §1.3–§1.5: `src/suno/SunoEndpoints.hpp` (287 lines),
  `docs/suno_api/ENDPOINT-INVENTORY.md` (1,932 lines),
  `docs/suno_api/UI-SURFACE-INVENTORY.md` (87 lines),
  `docs/suno_api/MCP.md` (165 lines),
  `docs/suno_api/OBSERVED-LEADS.md` (762 lines), plus bridge headers.
- External §9: Qt Docs MCP hub + Qt blogs (verified by search in
  this pass — see §9.1/§9.3 URLs); third-party libraries named
  for verification, not cited as verified (see §9 header).
- Repo house rules: `AGENTS.md` (build profiles, ctest-count rule,
  secret/host-allowlist rules, skill preference, "src wins" rule)
  and `docs/README.md` (one-fact-one-document hub rule).
- **What was NOT done:** no code changed, no file registered in
  `cmake/Sources.cmake`, no test run (no Qt build exists in the VM
  for this tree — the document's own verification commands are for
  Derek's desktop), no GitHub push, no HAR capture, no authenticated
  Suno or MCP call, no Figma access. Every "verified" claim above
  means "read in the baseline tree or in a search result returned
  this session", not "run".

---

*End of plan. The next step is Derek's: pick Phase 0 + Phase 1 as the
first implementation slice, or reprioritise §1.4 (like / playlist
create / trash) into Phase 2. Either way, the implementer starts at
§0, then §7's Phase 0 checklist — not in the middle of §6.*
