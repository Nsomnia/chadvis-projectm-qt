# TODO — ChadVis (Suno.com desktop client, projectM secondary)

> State marks: `[ ]` todo · `[~]` in progress · `[!]` blocked · `[?]` needs you · `[x]` done, awaiting verification
> Sequencing authority: `docs/PIVOT_PLAN.md` (P0–P7 + P2b) and the backlogs in `AGENTS.md`.
> Spec corpus (authoritative, Rust rewrite): `~/Documents/suno-media-station-glm5.2/docs` — charter 00, API contract 06,
> storage 07, visualizer 09, canvas 10. Endpoint evidence tiers live in `src/suno/SunoEndpoints.hpp` (`[T1]`/`[LEAD]`).
> Deepwork state: `.slim/deepwork/suno-client-shell-sprint.md` (gitignored).

## Current sprint — P2b Desktop Client Shell & Native Sign-in
Branch `feat/suno-client-shell-refactor`. Kickoff budget: ~6 days of free model capacity.

### Landed (awaiting your verification)
- [x] **Window-load fix** — `onViewChanged` → `onActiveViewChanged`; the engine now loads and the window draws.
- [x] **Library full-sync pagination** — `feed/v3` cursor auto-paging (1 Hz), `loading` held while `hasMore`,
      viewport-fill guard, uniform failure clearing + watchdog. (Commits 1a929e6 / 1582b9f)
- [x] **Bridge wiring bug** — `SunoBridge` signals were never connected because the lazy singleton is created *after*
      `setSunoController()` runs; clips never rendered and `loading` only cleared via the watchdog. Wiring is now
      order-independent and idempotent. (0077db1)
- [x] **Endpoint map correctness** — removed ~21 latent `/api/api/...` 404s, fixed the aligned-lyrics trailing slash,
      added 34 capture-proven surfaces, replaced the false "HAR 2026-06-10" provenance, tiered all 71 constants,
      corrected the OAuth/API doc drift, added a single-`/api` regression test. (678be76)
- [x] **Settings is its own window** — paged `SettingsWindow.qml` over the existing per-category panels; Save / Reset /
      Esc; no duplicated bridge property.
- [x] **Suno-first shell** — nav = Library / Create / Listen / Video / Settings; `CreateView` hosts the
      previously-uninstantiated `SunoPanel` Create surface; projectM + karaoke/overlays + presets + recorder moved into
      `VideoView` (kept alive for the process lifetime so the GL context survives navigation); `ListenView` is
      playback-focused. (59d269b)
- [x] **Sign-in UX** — `AccountPage` + session card; Google action present but disabled-with-tooltip until a capture
      proves Clerk accepts a desktop callback; manual cookie/bearer paste remains the working path.

### In progress
- [~] Native sign-in scaffold (flag-gated, offline-tested): `LoopbackListener` + transaction (CSPRNG state, OIDC nonce,
      PKCE S256, single-consume, 7-min deadline) + coordinator. **No Clerk handshake, no Google client id, no cookie
      parser** until a capture exists.
- [~] `sanitizeFilename` correctness — two pre-existing red tests (control chars, mixed special chars).

### Next (integration lane, starts when the scaffold lands)
- [ ] Wire coordinator → `SunoController` → `SunoBridge` (`googleLoginState` / `googleLoginError` /
      `googleLoginAvailable` + `beginGoogleSignIn` / `cancelGoogleSignIn` / `signOutSuno` / `googleLoginCallbackReceived`)
      and re-source `AccountPage` state to that property — remove the QML-local 120 s watchdog so QML and C++ cannot
      disagree. Sign-out is local-only and must not claim a remote Suno logout.
- [ ] Pin `QQuickStyle::setStyle("Fusion")` before engine creation — the macOS native style currently ignores our
      custom `background`/`contentItem`, which is the source of the warning flood.
- [ ] Keychain reads must fail instead of prompting (`kSecUseAuthenticationUIFail`). Today an unanswered macOS
      keychain ACL prompt after a rebuild **hangs startup before the QML engine loads**; a refused read must be
      distinguishable from "no credential" and must never silently downgrade to the 0600-file fallback.
- [ ] Register the new sources/tests in `cmake/Sources.cmake` + `tests/unit/CMakeLists.txt`, then re-run the full
      verification bar and do a live smoke of Library → Create → Listen → Video → Settings window.
- [ ] Oracle gate on the integrated auth path (Gate 1 was architecture-only; this is the implementation review).

### Blocked / needs you
- [?] **Native-login capture (the only unblock for real Google sign-in).** No capture on this machine uses a loopback or
      custom-scheme redirect; every observed redirect is Suno-owned HTTPS. Needed: an app-owned disposable Google
      desktop client, registered loopback URIs on two ports, altered `client_id`/`redirect_uri` + PKCE + state on the
      initiation request, plus the handshake JWT claims, the `Set-Cookie` contract, and a remote sign-out path. Say the
      word and I will hand you the exact recipe.
- [ ] Fresh captures to promote LEAD → T1: personas (`/api/me/v2/personas`, `/api/persona/create/`), audio upload
      lifecycle, trash/restore, playlist mutations, omnisearch, billing clip download, and the error/rate-limit
      envelope.

## Product roadmap
### Near term — finish the desktop client (P2 remainder)
- [ ] Library: sort + tag filter chips on top of the live debounced server-side search; local↔remote parity columns.
- [ ] Downloads: verify HTTP-range resume; expose queue state/pause/resume in the UI.
- [ ] Playback parity: one transport over remote preview vs local file, buffer-ahead, device-disconnect recovery.
- [ ] Playlists: listing is T1-captured; mutations wait on captures.
- [ ] Create surface hardening: generation request is capture-shaped; verify the persona/style/seed controls against a
      live generation before calling parity.

### Mid term — the secondary video engine
- [ ] **Recorder correctness first** (it is not functional end-to-end today): renderer `frameCaptured` →
      `VideoRecorder::submitVideoFrame`; `RecordingBridge::startRecording()` must also start the visualizer-side
      capture; fix `setAudioQueue` ordering (called before the worker exists); assign `outputPath` from config; make
      codec/CRF controls real.
- [ ] P3 lyrics/karaoke data: aligned-lyrics v2 (`start_s`/`end_s`) as primary source, Whisper fallback, LRC/SRT export.
- [ ] P4 deterministic video export: frame index as master clock, hardware-encoder probe chain, named output presets.
- [ ] P5 brand canvas + keyframes; P6 batch automation.

### Far term
- [ ] Lightweight DAW loop: record a take → upload to Suno → bring the result back → record vocals over it.
- [ ] Creative assist adapters (LLM lyrics, image gen), plugin/extensibility story, multi-account.

## Verification bar (every phase)
`cmake --build build --parallel 4` green · `qmllint -I build/qml` clean on touched QML · `./build/tests/unit/unit_tests`
all green (no pre-existing failures left behind) · live smoke of the affected surfaces · CHANGELOG_CURRENT.md updated ·
granular focused commit · never `rm` (graveyard with timestamp).
