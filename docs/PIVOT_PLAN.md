# ChadVis Pivot Plan: Suno.com Frontend First, Visualizer Second

> Status: ADOPTED 2026-08-26. Source material:
> `/Users/derekvanee/Documents/suno-media-station-glm5.2` (Rust rewrite — its `docs/`
> are the authoritative spec corpus). This plan maps that vision onto THIS Qt/QML codebase,
> which we keep as the shipping frontend because the GUI shell already exists and renders.

## Product Identity

ChadVis becomes a native desktop **full-surface Suno.com front-end**:
listen, browse, download, generate — with the projectM visualizer as an embedded
secondary surface used for (a) reactive listening visuals and (b) orchestrated
music-video rendering (text/image/karaoke elements + keyframes → batch export).

Not a Suno replacement. No local AI inference. No reverse-engineered generation
pipeline — only captured REST surfaces.

## Ground Rules (from the Rust project's post-mortem about US)

1. **Capture-driven API contract.** Our `docs/suno_api/` recon corpus has been
   proven WRONG in several places by real traffic captures (lyrics field names are
   `start_s`/`end_s` not `start_time`; playlist listing is `/api/playlist/me`;
   Clerk refresh is `touch`, not `tokens`). Treat every endpoint entry as LEAD until
   matched against `suno-media-station-glm5.2/docs/captures/raw/burp-session-2026-08/`.
   Copy that capture corpus's conclusions into our API layer; tier evidence T1/T2/T3.
2. **One authenticated client interceptor.** ALL Suno traffic through a single
   rate-limited queue with uniform 401 handling (transparent refresh → retry-once →
   re-auth prompt). Kills our current scattered 401 handling and orchestrator-bypass bugs.
3. **Crown jewels to port faithfully:** deque+1Hz rate limiter; JWT extraction incl.
   suffixed `__session_*` variants; polite lyric fetching (≤3 concurrent, jitter,
   requeue-on-"Lyrics processing:", pause-on-401→refresh→resume); centralized constexpr
   endpoint map + CDN fallback (`cdn1.suno.ai/{id}.mp3` when `audio_url` empty); ID3/Xiph
   tagging (SUNO_ID/PROMPT/MODEL/STYLE); `alignWordsToLines()`; dual-PBO GPU readback;
   named encoder presets (youtube1080p60…); GL-thread marshalling via atomics + echo de-dupe.
4. **Do-NOT-replicate list applies to this repo TODAY:** Application god object;
   unchecked `avcodec_alloc_context3`; RT-path scratch allocation + busy-waits; dual
   position-update race; plaintext token storage; uncancellable long ops; config drift.
5. **projectM landmines (keep):** PCM count = samples per channel; feed silence while
   paused; register failed-preset callback; set texture paths before presets; wrap GL
   calls in state guard; mesh size = quality dial; **v4.1.6 lacks `render_frame_fbo` /
   `set_frame_time` — pin CPM to master before headless export work.**
6. **GL embedding lesson:** the standalone-QWindow + WindowContainer path we now have IS
   the solution that survived. Do not regress to QQuickItem/QFBO embedding.

## Phases

### P0 — Cross-platform foundation ✅ DONE (2026-08-26)
- macOS 14 (AppleClang 16, homebrew Qt 6.11) configures/builds/launches clean.
- Retina DPR fix: visualizer renders full viewport (`devicePixelRatio()` scaling).
- Commits fd357ff, ca2f33e, e26841f pushed.

### P1 — Suno Core Correctness (auth + single client) ✅ DONE (2026-08-26, commits a578e1a, 8a2b898, 86ae631)
- [x] Rework auth: Clerk flow against `auth.suno.com/v1/client` (+`sessions/{sid}/touch`
      refresh, proactive ~55-min timer), browser-like header set (Device-Id, Browser-Token,
      Origin/Referer, UA) on every studio-api call.
- [x] Credential storage: OS Keychain (Security.framework; atomic 0600 file fallback);
      TOML→keychain migration on first init. Token-paste bootstrap works via Settings;
      webview/OAuth deferred.
- [x] Single queued authenticated client: uniform 401→touch→retry-once→needsReauth;
      lyrics manager duplicate path deleted; wav polling routed through queue + cancellable.
- [x] T1-captured shapes: canonical ClipParser (full clip schema), feed/v3 cursor
      pagination, session model catalog (`GET /api/session/`), billing
      (`GET /api/billing/info/`) surfaced as credits/planName/userName.
- [x] Dead routes retired (LOGIN/SIGN_IN/Clerk constants); SystemBrowserAuth archived.

### P2 — Remote Library First (UI pivot) — Library landing ✅ 2026-08-27, full sync ✅
- [x] Re-home navigation: Library becomes the default landing surface; visualizer moves to
      a secondary view/mode (still fullscreenable). Sidebar accordion → persistent nav rail
      (Library / Player+Visualizer / Canvas / Studio / Automation / Settings). `main.qml` nav rail + `activeView` persisted via `SettingsBridge.expandedPanel`; `onActiveViewChanged` crash fixed 2026-08-27 (55e8dd9).
- [x] Remote library browse/search/filter/sort over feed/v3 with debounced search;
      per-page sync signals, resumable cursor state. Full auto-pagination loop (feed/v3 `next_cursor`/`has_more`) with 1 Hz limiter + viewport-fill guard; incremental `libraryUpdated` per page; spinner stays true while `hasMore` (f669457). Debounced search 350 ms end-to-end.
- [~] Download manager: max ~3 concurrent, exponential backoff cap 3, HTTP-range resume,
      retryable queue, local↔remote parity columns (`remote_*` vs `local_*`). Queue exists (`SunoDownloader`/`DownloadQueue`) — needs range-resume verification + parity columns wired.
- [ ] Playback parity: one player interface over remote-preview URL vs local file;
      buffer-ahead next track; graceful device-disconnect handling.
- [ ] Playlist management once mutation captures land (listing endpoint is T1 today).

### P2b — Desktop Client Shell & Native Sign-in (branch `feat/suno-client-shell-refactor`, 2026-09-24)
- [x] Re-home the shell on the Suno client: nav = Library / Create / Listen / Video / Settings; Library is the default
      landing; the previously-uninstantiated `SunoPanel` Create surface is now hosted by `CreateView`.
- [x] Move projectM + karaoke/overlays + presets + recorder out of `ListenView` into `VideoView`, kept instantiated for
      the process lifetime so the native GL context is never recreated; `ListenView` is playback-focused.
- [x] Settings becomes its own window (`SettingsWindow.qml`) with a page rail over the existing per-category panels —
      no duplicated bridge property — plus Save / Reset / Esc-to-close.
- [x] Endpoint map correctness: `API_BASE`-relative normalization removed ~21 latent `/api/api/...` 404s, aligned-lyrics
      trailing slash fixed, 34 capture-proven surfaces added, false HAR provenance replaced, all constants tiered
      `[T1]`/`[LEAD]`, regression test locks the invariant.
- [x] Bridge wiring bug fixed: `SunoBridge` signals were never connected because it is a lazy singleton created after
      `setSunoController()` ran — clips never rendered and `loading` only cleared via the watchdog.
- [ ] Sign-in UX: standalone paged `AccountPage` + session card, Google action disabled-with-tooltip until capture-proven,
      manual cookie/token paste remains the working path.
- [ ] Integrate the native sign-in scaffold (loopback listener + transaction/CSRF/PKCE security + coordinator) behind a
      disabled flag; offline tests only — no speculative Clerk handshake.
- [ ] Pin `QQuickStyle::setStyle("Fusion")` before engine creation to stop the macOS native-style customization warnings
      (the components' custom `background`/`contentItem` are currently ignored by the native style).
- [ ] Keychain read must fail instead of prompting (`kSecUseAuthenticationUIFail`): an unanswered macOS keychain ACL
      prompt after each rebuild currently hangs startup before the QML engine loads.
- [ ] Recorder correctness (currently non-functional end-to-end): renderer `frameCaptured` → `VideoRecorder::
      submitVideoFrame`; `RecordingBridge::startRecording()` must also start the visualizer-side capture; fix
      `setAudioQueue` ordering; assign `outputPath` from config; make the encoder/codec controls real.

### P3 — Lyrics & Karaoke Data
- [ ] Aligned lyrics v2 (`start_s/end_s` word timing) as PRIMARY source; Whisper fallback
      only when absent/low-confidence. Waveform aggregates + downbeats where useful.
- [ ] Versioned lyric edits: new row + `is_current` flip, never overwrite source.
- [ ] LRC/SRT round-trip export; finish LyricsBridge stubs (exportToSrt/Lrc, search).

### P4 — Visualizer Music Videos (one-off render)
- [ ] Pin projectM CPM to master (frame-time API); verify headless fast-feed beat behavior.
- [ ] Headless deterministic export: frame index = master clock, integer timestamps,
      exact 1/fps PCM slices, decoupled from realtime; ffmpeg pipe process.
- [ ] HW encoder probe chain: VideoToolbox (mac) → NVENC → QSV → libx264 fallback.
      Named output presets (youtube1080p60, discord8mb…).
- [ ] Fix inherited recorder defects first: null codec ctx check, no RT-path allocs,
      cancellable pipeline.
- [ ] Dual-PBO triple-buffered readback port if FBO-direct path insufficient.

### P5 — Brand Canvas & Keyframes
- [ ] Scene model per Rust spec doc 10: elements (text/image/shape/karaoke_text),
      base properties, z-order = list order; whole-JSON-blob persistence +
      `schema_version`.
- [ ] Compositing service: `(visualizer texture, scene, t, resolved lyrics) → rendered
      frame`; framerate-agnostic, zero knowledge of preview-vs-export (parity guarantee).
      QML Canvas/Quick items for preview; same scene JSON drives headless export.
- [ ] Keyframe timeline: docked NLE-style panel, per-property tracks (t, value, easing:
      linear/in/out/in_out/step); effects library (fade_in_out, glow, particle) as
      parameterized behaviors that may synthesize tracks.
- [ ] `karaoke_text` flagship element bound at RENDER TIME to the track's lyric doc;
      style-once-apply-everywhere; line AND word modes with fallback indicator.

### P6 — Automation at Scale
- [ ] Batch render = pure fan-out of the single-track export service; durable run items,
      crash-resumable, per-item failure isolation, input snapshot at run start,
      "resume interrupted run?" prompt.

### P7+ — Deferred
Creation studio polish (generation surface exists in skeleton form — wire to v2-web
endpoint + captcha check), creative assist (LLM lyrics/cover adapters), plugins, DAW.

## Architecture En-Route Refactors (blocking debt, scheduled inside P1–P2)
- Split `Application` god object into subsystem managers (Audio/Ui/Suno/Recorder).
- Decode exists in exactly ONE place; pull-based normalized PCM stream consumed by
  playback/visualizer/recorder/export (also resolves AudioQueue triple-redundancy).
- Config single source of truth (struct defaults < TOML < CLI), kill hardcoded paths.
- Result<T>→std::expected monadic migration continues per AGENTS.md guidelines.

## Verification Discipline
Every phase exit needs: clean build both platforms where possible, unit tests for new
pure logic (rate limiter, JWT extraction, alignWordsToLines port, keyframe evaluator),
a manual smoke checklist, CHANGELOG updated, committed + pushed.
