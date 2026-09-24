# TODO — ChadVis (Suno.com desktop client, projectM secondary)

> State marks: `[ ]` todo · `[~]` in progress · `[!]` blocked · `[?]` needs you · `[x]` done, awaiting verification
> Sequencing authority: `docs/PIVOT_PLAN.md` (P0–P7 + P2b) and the backlogs in `AGENTS.md`.
> Spec corpus (authoritative, Rust rewrite): `~/Documents/suno-media-station-glm5.2/docs`. Endpoint evidence tiers live
> in `src/suno/SunoEndpoints.hpp` (`[T1]`/`[LEAD]`). Deepwork state: `.slim/deepwork/suno-client-shell-sprint.md`.

## Current sprint — P2b Desktop Client Shell & Native Sign-in
Branch `feat/suno-client-shell-refactor`. Latest runtime evidence: window created, initialization complete,
**0 QML errors, 0 style warnings, ~2 s startup** (verified by smoke, not assumed).

### Landed (awaiting your verification)
- [x] **Window-load fix** — `onViewChanged` → `onActiveViewChanged`; engine loads and the window draws.
- [x] **Window-creation fix** — `AccountPage.qml` used an invalid `import "./File.qml"`; QML cannot import a single
      file that way, and the failure chained `AccountPage unavailable` → `SettingsWindow unavailable` → `Failed to
      create QML window`. Same-directory types resolve implicitly. **The app was failing to open its window at all.**
- [x] **Library full-sync pagination** — `feed/v3` cursor auto-paging (1 Hz), loading held while `hasMore`,
      viewport-fill guard, uniform failure clearing + watchdog.
- [x] **Bridge wiring bug** — `SunoBridge` signals were never connected (lazy singleton created *after*
      `setSunoController()` ran), so clips never rendered and `loading` only cleared via the watchdog.
- [x] **Endpoint map correctness** — ~21 latent `/api/api/...` 404s removed, aligned-lyrics slash fixed, 34
      capture-proven surfaces added, false HAR provenance replaced, all 71 constants tiered, single-`/api`
      regression test, doc drift corrected.
- [x] **Clerk envelope parser** — reads all three capture-proven token locations, so a session can survive refresh
      again; failures are classified instead of always being called "expired".
- [x] **Startup hang** — proven with `sample`: a keychain read ran on the GUI thread before the engine existed and
      blocked in `CSSM_DecryptDataFinal` → `mach_msg` on `securityd`. Now an owned worker thread with a queued hop
      back, coalesced requests, and the modern data-protection keychain. 10 s+ hang → 9 ms.
- [x] **Settings window + Suno-first shell** — paged Settings window over the existing panels; nav = Library /
      Create / Listen / Video / Settings; projectM + karaoke + presets + recorder moved to `VideoView` (kept alive so
      the GL context survives navigation); `ListenView` is playback-focused; `CreateView` hosts the Create surface.
- [x] **Fusion style pinned** — the macOS native style was silently discarding every custom `background`/`contentItem`;
      the warning flood is gone and controls render as designed.
- [x] **Native sign-in scaffold (flag-gated, offline-tested)** — CSPRNG state + nonce + PKCE S256, hardened loopback
      listener, single-consume transaction with a 7-minute deadline, coordinator, secure-backend gate, local-only
      sign-out. 23 offline tests. Deliberately contains no endpoint, client id, redirect, cookie parsing, or handshake.
- [x] **Auth UX honesty** — Google action disabled with an explanatory tooltip; Account page state driven by the
      authoritative `googleLoginState`; credential input labelled as a session cookie header (a bare `__session` JWT
      was the likely cause of the live auth failure).
- [x] **`sanitizeFilename`** — special characters, control chars, and traversal components now map to underscores.

### In progress
- [~] End-to-end auth verification with a real credential: paste the full `Cookie` header from a signed-in browser's
      `auth.suno.com/v1/client` request into Settings → Account, then confirm Library sync populates.

### Next
- [ ] Verify Library end to end with a >20-track account (pagination, search, detail sheet) and confirm the Video page
      keeps its GL context across navigation.
- [x] **Recorder correctness** — frame signal now connected exactly once in the composition root; a single
      `startRecording()` starts encoder + renderer capture and stops capture before finalization; the audio queue
      attaches correctly regardless of worker-creation order; the encoder gets a sanitized timestamped output path
      whose extension matches the chosen container; codec/CRF controls drive the config. The missing
      `avcodec_alloc_context3` null check (a P0 crash) was added while in the path. Verified by
      `test_RecordingPipeline.cpp`, where frame submissions reach a real libx264 encoder. **Still needs a human
      with the GUI and real audio** to confirm an actual recording plays back. Deterministic/headless export,
      encoder probing, and the projectM `master` pin remain P4.
- [ ] Library polish: sort + tag filter chips on top of the live debounced search; local↔remote parity columns.
- [ ] Downloads: verify HTTP-range resume; expose queue state/pause/resume.
- [ ] Playback parity: one transport over remote preview vs local file, buffer-ahead, device-disconnect recovery.
- [ ] Typify the credential slot (exp-3 ranked fix #2): classify JWT vs cookie header at load instead of labelling
      whatever is stored a "cookie".
- [ ] Make `AuthFailureKind` reachable from `SunoController` (today only the sanitized `SunoClient::errorOccurred`
      string is forwarded, because `SunoClient::clerk_` is private).
- [ ] Replace the missing-font-family notice (`Inter, "Noto Sans", Sans-serif` costs ~175 ms at startup).
- [ ] Per-directory `qmldir` files before ever re-enabling QTP0004 (the policy is currently reverted as a precaution).

### Blocked / needs you
- [?] **Native-login capture** — no capture on this machine uses a loopback or custom-scheme redirect; every observed
      redirect is Suno-owned HTTPS. Needed to enable real Google sign-in: an app-owned disposable Google desktop
      client, registered loopback URIs on two ports, altered `client_id`/`redirect_uri` + PKCE + state on the
      initiation request, the handshake JWT claims, the `Set-Cookie` contract, and a remote sign-out path. Say the word
      and I will hand you the exact recipe.
- [ ] **Header contract question** — the newest full HAR records 56 × 200 `feed/v3` calls with `Browser-Token` +
      `Device-Id` and **no `Authorization` header**, contradicting the August capture. Either HAR metadata omission
      or a contract change; needs a fresh capture before anyone edits auth headers.
- [ ] Fresh captures to promote LEAD → T1: personas, audio upload lifecycle, trash/restore, playlist mutations,
      omnisearch, billing clip download, error/rate-limit envelope.
- [ ] Rotate the Suno session: live credentials were pasted into chat during debugging. Nothing was written to disk,
      logs, or git.

### Verified Complete (locked)
- [x] P0 cross-platform foundation; P1 Suno Core Correctness; `cmake --build build` green; `unit_tests` green
      (92 registered cases + the standalone ClerkAuthClient suite).

## Product roadmap
### Near term — finish the desktop client
Library sort/filter · downloads parity · playback parity · playlists · Create-surface verification against a live
generation.
### Mid term — the secondary video engine
Recorder correctness first, then lyrics/karaoke data (aligned-lyrics v2 primary, Whisper fallback), deterministic
video export with a hardware-encoder probe chain, brand canvas + keyframes, batch automation.
### Far term
Lightweight DAW loop (record → upload to Suno → bring back → record over) · creative-assist adapters · plugins ·
multi-account.

## Verification bar (every phase)
`cmake --build build --parallel 4` green · `qmllint -I build/qml` clean on touched QML · `./build/tests/unit/unit_tests`
green · a runtime smoke that actually reaches "QML window created successfully" (a stalled run proves nothing) ·
CHANGELOG_CURRENT.md updated · granular focused commit · never `rm` (graveyard with timestamp).
