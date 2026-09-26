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
- [~] **Lyrics out-of-bounds subscripts** — *retargeted 2026-09-26: the reported class, `LyricsOverlayRenderer`, was deleted in `0530240` and replaced by QML `KaraokeMaster.qml`, so the original item named dead code.* Two unguarded subscripts survive in the current lyrics code, both reachable from QML: `LyricsData::getTimeRange` (`startIdx` clamped only at the low end, and `endIdx - 1` wrapped to `SIZE_MAX` by a near-`SIZE_MAX` `size_t` index) and `LyricsSync::getContextLines` (only the lower bound on the cached `currentPos_.lineIndex` is checked, so a stale index after loading a shorter song reads past the end). Root cause is the ad-hoc `int`/`size_t` mismatch at the QML bridge boundary with no shared bounds helper. Fix adds a `constexpr` checked-index helper and covers empty-`words` and `endTime < startTime` remote-data edge cases.
- [x] **Preset scanning blocks the GUI thread** — *the premise was half-stale: `PresetBridge::rescan()` was already async-ish, but the invalidation hazard was real and startup was fully synchronous.* Fixed in `f121901`. `PresetManager` gained `scanAsync`/`rescanAsync`/`setPublishContext` with **shared generations**: every scan builds a fresh vector and *swaps it in*, so a published generation is never cleared/resized/refilled in place and a `const PresetInfo*` taken before a rescan stays valid. Readers get a `Snapshot` (`shared_ptr<const PresetList>`) or a `PresetView` (query + pinned generation). Worker is a `vc::JThread` parked on a condition variable (`VideoRecorderThread` pattern) — not a moved-`QObject` `QThread`, because `PresetManager` is a *value member* of `pm::Bridge` — with `CredentialStoreWorker`'s `invokeMethod(..., Qt::QueuedConnection)` hand-off reused verbatim. Requests coalesce latest-wins (N rescans cost ≤2 walks); a failed scan retains the previous generation instead of silently emptying a working library. `Application.cpp:391` startup now uses `scanAsync` with a publish context. The synchronous `scan()` is **kept intact** on purpose: `pm::Bridge` reads `empty()`/`selectByIndex(0)` immediately after (Bridge.cpp:98) and never sets a publish context, so it never starts a worker. Also hoisted the per-file `std::regex` out of the scan loop (it compiled a grammar per preset, the dominant cost after the stat storm) with a provably-equivalent `find('-') == npos` early-out. **Pre-existing bug found en route:** `PresetBridge::connectSignals()` was declared, defined, and never called from anywhere — and because `qmlEngine_->load()` runs *inside* `init()`, QML's one-time `model: PresetBridge.filteredPresets()` binding evaluates before an async scan publishes, so the startup list would have stayed **permanently empty**. Wiring now happens in an idempotent `attachManager()` from the ctor. 8 preset tests, 0 skipped, including a pinned-generation case that fires 8 rescans and asserts the old pointers stay valid while a new generation appears at a different address.
- [ ] **Playlist not thread-safe** — accessed from UI and audio threads without synchronization.
- [x] **Dual position update path in `LyricsSync`** — *recon 2026-09-26: `updatePosition` has two independent entry points.* The `QTimer::timeout` lambda (`LyricsSync.cpp:18-22`, GUI thread) and **synchronous direct calls from `loadLyrics`/`seek`/`jumpToLine` (`:58`, `:111`, `:214`)**. `updatePosition` is a read-modify-write over `smoothedTime_` (a `lerp` at `:131`) plus a compare-then-write over `currentPos_` in `detectChanges` (`:161`), so the two paths double-apply smoothing for one audio instant and can drop or duplicate a `lineChanged`/`wordChanged` edge. `LyricsSync` has zero synchronization — no mutex, no atomics, no thread assertion. Two aggravating details: `trackChanged` is connected `Qt::DirectConnection` (`:30-33`) so `clear()` runs a full state reset off the event loop's ordering, while its `stateChanged` sibling correctly uses `Qt::QueuedConnection` (`:25-28`); and `AudioEngine::positionChanged` is consumed by `AudioBridge` but **not** by `LyricsSync`, so lyrics poll at 16 ms while the transport pushes. **Fixed in `f10f87d`:** the `QTimer` is now the sole writer via a one-shot seed drained by a new public `syncNow()`; `seek`/`loadLyrics` seed instead of applying; `trackChanged` switched to `QueuedConnection`; thread asserts added to the three public mutators. Deliberately **no mutex** — two writers plus an unguarded `lyrics_.vector` would be worse. Two subtleties found and handled: a seed must *snap* (the drain pre-sets `smoothedTime_` so the lerp degenerates) or `seek` stops being exact and a mid-playback `loadLyrics` ramps from 0 instead of snapping; and `seek` with a null `AudioEngine` never drains on its own, which is what forced `syncNow()` to be public rather than a test-only backdoor. Follow-ups still open: `AudioEngine::positionChanged` is deliberately still unconsumed (polling is unchanged behaviour, and a second trigger invites a second writer), and the `Seeking → Syncing/Paused` double `stateChanged` in `seek` remains. Related: `LyricsBridge::onLineChanged`/`onWordChanged` (`LyricsBridge.cpp:180-189`) are dead slots never connected in `connectSignals` (`:96-124`) — a third would-be writer of the five position members.
- [ ] **Playlist::addFile reads metadata synchronously** — blocks the UI thread on tag reads; move to a worker.
- [x] **PFFFT static locals thread-unsafe** — shared immutable RAII setup, 16-byte-aligned per-call FFT scratch, mutex-protected state, deterministic zero-on-setup-failure, plus normal and ThreadSanitizer tests.
- [x] **SunoClient use-after-free** — request epochs fence queued retries/waiters, tracked replies are disconnected and aborted, stale restore/upload callbacks cannot publish after credential invalidation.
- [x] **QML cached singleton lifetime** — cached bridge pointers reset when their singleton is destroyed.
- [x] **VideoRecorderFFmpeg nullptr deref** — both video and audio `avcodec_alloc_context3()` results checked.
- [x] **AudioEngine scratch buffer resize in the audio callback** — scratch preallocated during `init()`; oversized callback buffers dropped.
- [x] **Application destruction order** — QML, controller, lyrics, preset, recorder, audio, and Qt application torn down in dependency order.
- [x] **Playlist::loadM3U path traversal** — relative entries weakly canonicalized and rejected when they resolve outside the playlist directory.
- [x] **Recording video/audio settings persist** — re-verified after a false report: `ConfigParsers::serialize` builds the `[recording.video]` and `[recording.audio]` sub-tables from the live struct via `CHADVIS_VIDEO_FIELDS`/`CHADVIS_REC_AUDIO_FIELDS` before flattening the plain recording fields, which is the shape the parser reads and the shape `config/default.toml` ships. No bug.

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
- [x] `test_PresetScanner` wired into `unit_tests`; `test_projectm_render` stub archived. *Corrected 2026-09-26: it was compiled into `unit_tests` but had no runner and never executed, so the `[x]` was a declaration, not a verification — the exact failure `AGENTS.md` §3 warns about. `runTestPresetScanner` now exists and is called from `test_main.cpp`.*

### Music video creator — the primary end goal

Recon 2026-09-26. The **encoder is done and real** (`VideoRecorderCore` state machine, genuine `avformat`/`avcodec`/`sws`/`swr` in `VideoRecorderFFmpeg.cpp:88-328`, a `JThread` worker with a bounded queue, exclusive-create + `flock` output claiming, and 1920×1080@60/CRF18/AAC defaults), and per-frame capture of the live projectM state works via an **undocumented** path: `VisualizerRenderer::renderFrame` → FBO → `captureAsync` (PBO) → `frameCaptured` → `VisualizerWindow::frameCaptured` → `Application.cpp:419-425` → `VideoRecorderThread::threadLoop`. `test_RecordingPipeline.cpp` really does run (called from `test_main.cpp:30`) and covers frames-before-and-after worker creation. Everything below is what stands between that encoder and a batch music-video creator.

- [ ] **P0: projectM v4 cannot render into our FBO, so recorded video is an empty buffer** — *independently verified from source 2026-09-26; this outranks every other video item.* `ProjectM.cpp:169-170` reads `// ToDo: Allow external apps to provide a custom target framebuffer.` and then does `glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0)`, and the copy that follows (`CopyTexture.cpp:99-109`) binds only a *texture* to unit 0 and draws "to the currently bound buffer" — i.e. framebuffer 0. Our `renderTarget_` FBO, bound at `VisualizerRenderer.cpp:93`/`RenderTarget.cpp:146`, is therefore **never written by projectM**; it holds only the `glClearColor(0,0,0,0)` from `RenderTarget.cpp:92-93`. Consequences: recorded video is transparent black, and the blit fragment shader writes `color = vec4(c.rgb, 1.0)` over the whole viewport (`VisualizerRenderer.cpp:157-166`, `:119-122`), so **the visualizer window itself is predicted to go black the instant recording starts.** This has never been observed because `test_RecordingPipeline.cpp:60-64` submits *synthetic solid-colour buffers* straight to `submitVideoFrame` and never renders a pixel, and `FrameGrabber::grab`/`grabScreen` — the classes that would have grabbed real pixels — are called by nothing. **Fix: make framebuffer 0 *be* the target** (offscreen surface's default framebuffer) and capture from there, rather than patching a CPM-fetched dependency. Delete the dead `useFBO` branch, `flipImageGPU`, and the dead `flipVertical_`/`useGPUFlip_` members; reuse the correct CPU `FrameGrabber::flipImage` (`:146-158`) for the vertical flip the live path dropped. **Verify with the spike below before and after.**
- [ ] **Spike first: prove the destination, offscreen feasibility, and the empty-FBO prediction** — one integration test, no production code, using `pm::Engine` directly (a plain value type with no QObject, no QTimer, no window, no preset scan — `Engine.hpp:39-51`). Create a `QOffscreenSurface` + `QOpenGLContext` at **3.3 core** (to match `QOpenGLFunctions_3_3_Core` init at `VisualizerRenderer.cpp:21` and projectM's `#version 330` shaders), `makeCurrent`, `Engine::init`, push PCM, `render()`, then `glReadPixels` **from framebuffer 0** and assert **some pixel is non-zero** — the assertion that actually matters (`!frame.isNull()` at `tests/integration/test_main.cpp:38-40` passes on solid black and would catch neither bug). Write a PNG to eyeball. **Bonus at zero marginal cost: read the `RenderTarget` FBO in the same run and assert it is uniform/empty** — that converts the P0 claim above from static analysis into a runtime fact. Two build facts: `tests/integration/CMakeLists.txt:11-17` does not link `Qt6::OpenGL` (the only change needed), and the test must be run under **both** `QT_QPA_PLATFORM=offscreen` and `cocoa` — a green run under the `offscreen` plugin does not prove the cocoa path users actually run.
- [ ] **The frame clock is the actually hard problem, not `isExposed`** — corrected 2026-09-26. The earlier claim that an unexposed window blocks offline rendering is a **red herring**: `projectm_set_window_size` is an int stash (`ProjectM.cpp:219-224`) and `libprojectM` has no window-system dependency (the SDL UI is `ENABLE_SDL_UI OFF`, `cmake/FindProjectM4.cmake:66`), so projectM needs only a current GL 3.3 context. The real constraint: **projectM v4 is wall-clock only.** `TimeKeeper::UpdateTimers` (`build/_deps/projectm-src/src/libprojectM/TimeKeeper.cpp:17-26`) computes `m_secondsSinceLastFrame` from `high_resolution_clock::now()`, and that value drives `PCM::UpdateFrameAudioData` → `Loudness::AdjustRateToFps` (`Audio/Loudness.cpp:53-56`); `ctx.time` (`ProjectM.cpp:435`) is a standard milkdrop per-frame variable, so **preset animation phase is wall-clock bound.** No `projectm_set_time` or synthetic clock exists in any public header; `projectm_set_fps` only sets a shader uniform. So 30 frames in 0.1 s is 0.1 s of projectM time, not 0.5 s. **v1 must be realtime-throttled** (sleep to each frame's correct virtual instant; a 3-minute song takes 3 minutes, recovered by running N jobs in parallel, one GL context each). Put the clock behind an injected seam so a later `TimeKeeper` fork (~20 lines in a 118-line self-contained class, requiring vendoring the CPM dep) can turn 3 min into ~15 s. **Do not pay that fork until v1 works and the wall-clock cost is measured.** Note a constant 12 ms projectM analysis latency (`AudioBufferSamples = 576` at 48 kHz, `Audio/AudioConstants.hpp:8`) — an offset, not drift, so sync holds, but beat-reactive visuals sit behind the audio.
- [ ] **Overlays and karaoke cannot appear in recorded video at all** — the single most product-critical finding after the P0 above. `VideoView.qml` embeds the visualizer as a **native `QWindow` as a texture** (`WindowContainer`, `:18-23`) and paints `VisualizerOverlay` (`:25`) and `KaraokeMaster` (`:30`) as **QML siblings on top in the `QQuickWindow` scene**. The recorder captures inside the *native* `VisualizerWindow`'s GL context. The QML scene is never rendered there, so **no overlay text, no karaoke lyric, and none of `OverlayPanel`'s work can ever reach an encoded file** — 100% of the overlay system is invisible to 100% of the video output. **Decision 2026-09-26: post-process with FFmpeg, not GL compositing.** GL compositing is not merely harder, it is *structurally* impossible here: `ProjectM.cpp:170` forces DRAW to framebuffer 0, which is precisely the framebuffer Qt Quick's scene graph owns, so projectM and Quick would fight over the same draw target every frame (upstream's own `ToDo` confirms the design does not admit it). The `QQuickWindow::grabWindow` → upload idea is worse: a ~8 MB GPU→CPU→GPU round trip at 60 fps on top of a Quick render that cannot be paced deterministically, and it is not even WYSIWYG. The FFmpeg route is a pure function of (encoded file + ASS file) → output — deterministic, re-runnable, testable headlessly, and it makes the batch product *composable* (render → attach subtitles → optionally burn text; change the font and re-run a 2-second pass, not a 3-minute render). `libavfilter` must be added in **two** places: `src/recorder/FFmpegUtils.hpp:3-10` and `CHADVIS_FFMPEG_COMPONENTS` at `cmake/Dependencies.cmake:123`.
- [ ] **No frame pacing between render fps and record fps** — `videoCodecCtx_->time_base = {1, video.fps}` and `pts = frameCount++` (`VideoRecorderFFmpeg.cpp:247,349-350`) with **no check that `visualizer.fps == video.fps`** and no drop/duplicate compensation. Defaults agree by luck (60/60). Any divergence, or any frame dropped by `MAX_QUEUE_SIZE` overflow, silently changes playback speed and makes the video shorter in wall-clock terms than the audio. **Easy guardrail — validate and warn at configure/startup; land early.**
- [ ] **Likely upside-down recorded video (INFERRED, unverified)** — the live `captureAsync` applies **no vertical flip**, and `FrameGrabber::flipVertical_` is dead code. `glReadPixels` on a bound FBO returns rows from GL's bottom-left origin while the on-screen blit maps screen-top to texcoord `1.0` (`VisualizerRenderer.cpp:171-173`), i.e. it flips for display — which would make the FBO's raw content the opposite orientation. **Confirm with one 5-second real render** the moment headless rendering exists, before trusting any output.
- [ ] **Batch automation is entirely absent** — no render queue, no job model, no "render all". `--headless` is worse than useless: it only skips the window (and thus the frame-capture wiring), so it makes recording *impossible*. `DownloadQueue` (`src/suno/DownloadQueue.hpp:80`, `setMaxConcurrent` at `:99`, already tested) is the directly reusable template for a `RenderJob` model. Ship the job model + queue first, the batch **GUI** last — a polished queue over a broken render loop is a trap.
- [ ] **Download is welded to playback** — `SunoDownloader.cpp:274-276` makes `processDownloadedFile` a one-line alias for `addAndPlay`, which appends and `jumpTo`s (`:248-272`). So fetching N Suno clips to disk *audibly plays them one after another*, and `SunoBridge` has no `downloadClip` at all — only `playClip`. Split "save to disk" from "enqueue for playback"; batch needs the former anyway.
- [ ] **Scene composition, keyframes, transitions, timeline: 0% built** — verified absent: zero hits for `keyframe` across all 150 sources; every `transition` hit is projectM's own unrelated soft-cut; every `timeline` hit is a Suno *mashup* field. The word "scene" is not a domain concept anywhere. **Ship "presets-as-scenes" (a preset per segment with a duration + crossfade) before real keyframes** — the hard part is the authoring UX and interpolation semantics, not the C++, and projectM's existing soft-cut (`Engine.hpp:110-132`) is a free, working crossfade primitive.
- [ ] **Karaoke burn-in path** — no `libass`, no `.ass` writer, no VTT, no `avfilter` anywhere in `src/`; `toSrt` (`LyricsData.cpp:488`) and `toLrc` (`:514`) are sidecar-only. Cheapest useful win first: **mux as a soft subtitle track** (muxer only, no libass) by adding a `toAss()` writer for per-word karaoke (`\k`/`\kf` from the timings that already exist in `LyricsData::words`, ~80 lines), then burn in with the `subtitles=` filter once avfilter is linked.
- [ ] **Scene-graph compositing is the one genuinely hard overlay option** — rendering overlays into the *same* GL context as projectM is architecturally cleaner (one surface, true WYSIWYG) but means driving Qt Quick into a foreign GL context. Deferred behind the FFmpeg route deliberately.
- [x] **Recorded video/audio settings persist** — see P0 for the re-verification; not part of this section.

### Audio engine
- [ ] **Skipping a track while paused starts playback** — `onPlaylistCurrentChanged` (`AudioEngine.cpp:138-142`) calls `play()` **unconditionally** at `:141`, and that handler is bound to `currentChanged` at `:45`, i.e. to *every* index change including user-initiated jumps and playlist edits while paused. `autoPlayNext_` guards only the `EndOfMedia` path, so nothing guards the manual path. Small, contained, user-facing.
- [~] **`setSource()` runs inside the `mediaStatusChanged` emission** — *confirmed real, but the obvious fix is worse than the bug; do not queue a hop.* `onMediaStatusChanged` (`:122-132`) → `playlist_.next()` (`:127`) → `currentChanged` → `loadCurrentTrack` (`:154`) → `player_->setSource()` (`:158`), synchronously inside the emit. **Evidence recorded 2026-09-26:** in the normal gapless path this is usually a *no-op*, because `prepareNextTrack` (`:162-170`) already set the next URL on `nextPlayer_` before `swapPlayers` (`:144-152`) promoted it. The hazard only fires on the non-pre-buffered branch at `:128`. Queueing the hop with `Qt::QueuedConnection` would demote an already-decoded pre-buffer that *has data ready* until the next event-loop turn — trading a source-less teardown for a **guaranteed gap on every track transition**, which is the main quality feature of a music player. **Still open:** whether Qt documents the prohibition (the locally installed Qt 6.11.1 headers are comment-stripped, so the claim could be neither confirmed nor refuted offline — verify upstream before citing it), and whether a non-queued fix exists (defer only the *reconfiguration*, not the swap). No test drives a track transition today; `QtMultimediaTestLib` is present in the install, and only call *ordering* is assertable headless (no audio device in CI), so per `AGENTS.md` §3 this needs a manual listening pass regardless.
- [ ] **projectM's broken-preset retry path is dead** — `Bridge.cpp:53-54` calls `projectm_set_preset_switch_requested_event_callback`, which **overrides** projectM's own handler (documented at `build/_deps/projectm-src/src/playlist/api/projectM-4/playlist_callbacks.h:88-91`; single-slot, last-writer-wins). That kills `PlaylistCWrapper::OnPresetSwitchFailed`'s 5-retry recovery loop (`PlaylistCWrapper.cpp:74-103`), so a broken preset now aborts the chain permanently instead of retrying. ChadVis never registers `m_presetSwitchFailedEventCallback` either, so even if it ran it would go nowhere. Behaviour is preserved *by accident* on the happy path via a `pending*` latch drained on the next frame (`Bridge.cpp:150-153`, `:132-134`), which also costs a real ~16 ms switch delay. Behavioural regression vs stock projectM, not a safety bug.
- [x] **projectM playlist re-entrancy (investigated, closed as invalid)** — the suspected `OnPresetSwitchRequested` → `itemAt()` re-entry was **already prevented** by the `pending*` latch: `presetSwitchRequested` sets `pendingNext_` and does not touch `playlist_`. The single real re-entry (`Bridge.cpp:196` → `projectm_playlist_item`) is a bounds-checked **read** of a container that projectM is not iterating and does not mutate at that point (`PlaylistCWrapper.cpp:142-154`), and the documented infinite-recursion hazard is already handled by the `syncingFromNative_` guard at `:202`/`:177`. No fix wanted. Also refuted: `scanPresets` holds **no** `loadMutex_` (`loadMutex_` guards only `pendingLoadPath_` at `Bridge.cpp:120,190`) — its real cost is a synchronous GUI-thread scan in `init()`, deferred as below.
- [ ] **`pm::Bridge` startup preset scan is still synchronous** — `Bridge.cpp:98` runs a full `presetManager_.scan()` on the GUI thread inside `init()`. `Application.cpp:391` was already converted to `scanAsync` by `f121901`, but this instance cannot: `init()` reads `presetManager_.empty()` at `:69` and then `selectByIndex(0)`/`selectByName` at `:71,:75` before returning, and gates native-playlist population on the same pass at `:103-108`. An async scan would silently skip first-preset selection. Needs the selection logic deferred to the publish callback — a behaviour change, so a separate item.
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
- [x] `Application::printVersion()` hardcoded `1.0.0` while `version.txt` is the source of truth — the banner now consumes the `CHADVIS_VERSION` build definition (forwarded from `version.txt` to `project_lib` and the executable) via `vc::Cli::versionBanner()`, and `tests/unit/core/test_Version.cpp` pins both the banner text and the real binary's `--version` output to `version.txt`.
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
- [x] `ctest --test-dir build/tests --output-on-failure` runs all 8 registered suites. Note that `--test-dir build` discovers zero tests and still exits 0. *2026-09-26: found a second, quieter version of the same trap — `test_SunoEndpoints` defined `runTestSunoEndpoints` but `test_main.cpp` never declared or called it, so the fail-closed host-allowlist test compiled and never ran. It had been masked by a static initializer that called `std::abort()` on failure. Both are now called from `main()`. Lesson: a suite being in the CMake source list is not evidence it runs.*
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
