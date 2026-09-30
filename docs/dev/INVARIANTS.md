# Engineering Invariants

Hard-won constraints that must not be violated. Not a tutorial and not a
changelog: a list of things that were measured, got fixed once, and would be
broken again by an agent who has not read this file.

## Why this file exists

The backlog carried dozens of completed items, and each closed entry recorded a
*reason* rather than just an outcome: the concrete mechanism that made the naive
implementation wrong, the number that made the tradeoff non-obvious, the negative
result that stops someone re-running the experiment. The backlog is being
reconciled and those entries are going away. The reasoning was the durable part,
so it lives here instead. Nothing below is aspirational — every rule was written
because the alternative shipped a bug, a hang, a silently upside-down video, or a
test that never ran.

Operating rules stay in [`AGENTS.md`](../../AGENTS.md); remaining work stays in
[`TODO.md`](../../TODO.md). This file owns neither, and it restates no rule that
another document already owns — it records only *why* the constraints exist.

## Threads and ownership

- **`Playlist` is not a `QObject` and must not become one.** That is the entire
  reason `vc::Signal` exists in this codebase. If a second thread ever needs to
  touch the playlist, either marshal to the owner thread with
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` *from the caller's side*
  (the house idiom) or add exactly one coarse mutex.
  **If you take the mutex, every emit must move outside it** — this is forced, not
  stylistic: `currentChanged`'s live subscribers re-enter the playlist
  (`AudioEngine::onPlaylistCurrentChanged` → `loadCurrentTrack` →
  `prepareNextTrack` → `itemAt`/`currentIndex`), so a lock held across that emit
  self-deadlocks on the first track change. This is the same doctrine written at
  `PresetManager.hpp:56-57`.
- **Do not add a mutex to `Playlist`.** That is today's decision; the procedure
  above is for the day a second thread appears. `Playlist` already carries
  `const std::thread::id ownerThread_` with `assertOwnerThread()` on every public
  method. A lock would *mask* an off-thread writer while `PlaylistBridge`'s row
  bookkeeping silently rotted; the assert turns a heisenbug into a log line plus
  an abort. Do not "fix" the assert by adding a mutex around it.
- **`assertOwnerThread` proves affinity, not re-entrancy safety.** A slot
  re-entering the playlist from inside an emit is same-thread and passes — that is
  the current design working, not a hole. Note also that the `assert` compiles out
  under `NDEBUG` and the distributable build is Release, so a Release binary logs
  the violation and then walks into the race. That is an accepted fail-loud
  choice; a hard Release fail would need `std::terminate()`, not `assert`. Do not
  "fix" the Release hole by removing the log line.
- **`DownloadQueue` has no threads at all.** It is a plain `QObject`;
  `setMaxConcurrent(3)` bounds in-flight HTTP replies multiplexed on one event
  loop. Nothing in its header says so, and `SunoDownloader::addAndPlay` looks
  dangerous until you check. Do not add locking to it on the assumption that
  `maxConcurrent` implies worker threads.
- **`QMediaPlayer` is GUI-thread-driven.** It is a `QObject` created in `init()`;
  the backend does its decode work on its own threads but delivers every signal
  to the object's thread. Do not reason about it as a worker, and do not treat its
  internal threading as licence to touch `AudioEngine` state from elsewhere.
- **`LyricsSync` is single-writer by design: `QTimer` owns the position.** A seed
  must *snap* (drain pre-sets `smoothedTime_` so the lerp degenerates) or `seek`
  stops being exact and a mid-playback `loadLyrics` ramps from 0 instead of
  snapping. `syncNow()` is public (not a test backdoor) because `seek` with a null
  `AudioEngine` never drains on its own. Do not add a second trigger —
  `AudioEngine::positionChanged` is deliberately still unconsumed, and a mutex is
  worse than the hazard (two writers plus an unguarded `lyrics_.vector`).

## Buffers and lifetime

- **`Signal::emitSignal` perfect-forwarding only pays off at N == 1.**
  `frameCaptured` has exactly one production subscriber, so the hot path steals
  the 6 MB buffer with zero copies. `Slot` is deliberately left alone:
  `std::function`'s by-value `operator()` parameter is a real copy boundary, and
  removing it would buy one 24-byte move while introducing a dangling-reference
  hazard across ~30 `connect` sites and breaking the reference payload types
  (`Signal<const std::vector<SunoClip>&`). Measure payload defects in **bytes**,
  never in construction counts — an earlier test asserted counts and made a
  24-byte move look equivalent to an 8 MB copy.
- **`is_convertible_v` in the `emitSignal` `static_assert` is load-bearing.**
  `is_same_v` would break 5 live sites, because `VideoRecorderCore::state_` is a
  `std::atomic<RecordingState>` emitted directly and only satisfies the check
  through its implicit `operator T()`. Do not tighten the assertion to `is_same_v`
  for type purity.
- **The `AVFormatContextPtr` alias is deliberately gone,** split into
  `AVFormatContextInPtr` and `AVFormatContextOutPtr` so any stale reference is a
  compile error rather than a silent write-side read. Do not "simplify" the two
  aliases back into one.
  **Never close `pb` before freeing the context**: `avformat_close_input` frees
  the context and *then* closes `pb` on a snapshot, so a deleter that closed `pb`
  first would double-close on every file read. The guard is `AVFMT_FLAG_CUSTOM_IO`
  on `c->flags`, not `oformat->flags` — the latter is null for a demuxer, which is
  the whole class of bug that used to segfault a read.
- **A test-side copy of a shared rule is how two copies drift.** When a rule
  changes, grep the tests for the rule, not just for the symbol.

## Preset scanning

- **`PresetManager` generations are shared, never mutated in place.** Every scan
  builds a fresh vector and *swaps it in*, so a published generation is never
  cleared/resized/refilled and a `const PresetInfo*` taken before a rescan stays
  valid. Do not "optimise" a scan into filling an existing vector. A failed scan
  retains the previous generation rather than emptying a working library.
- **The synchronous `scan()` is intentional.** `pm::Bridge` reads
  `empty()`/`selectByIndex(0)` immediately after calling it and never sets a
  publish context, so it never starts a worker. `Application.cpp` uses
  `scanAsync` plus a publish context instead. Converting `pm::Bridge` to async is
  a behaviour change requiring deferred first-preset selection, not a mechanical
  swap.
- **`PresetBridge` wiring is idempotent and lives in `attachManager()`.** It is
  called from the constructor, and it must not be moved behind an explicit call:
  `qmlEngine_->load()` runs *inside* `init()`, so QML's one-time `model:` binding
  evaluates before an async scan publishes, and a deferred connect means the
  startup list stays permanently empty. This is the `AGENTS.md` §3 failure class —
  a declaration that reads as a working feature and is not.
- **The preset worker is a parked `vc::JThread`, not a moved `QThread`.** The
  `QObject` move is unavailable because `PresetManager` is a *value member* of
  `pm::Bridge` and cannot be moved to another thread's event loop.

## Recording and timestamps

- **PTS derives from the frame's own capture time, never from a counter.** Three
  decisions are load-bearing: `av_rescale_q` rather than a hand-rolled division,
  because truncating biases every frame one tick early *and* accumulates while
  nearest-integer is bounded by half a tick with no drift; the origin is normalised
  to PTS 0, because a `steady_clock` reading is microseconds since boot and
  handing that to a muxer inflates the reported duration via the MP4 edit list;
  and monotonicity is guaranteed twice, by a `lastVideoPts_ + 1` floor plus a
  no-timestamp path that extrapolates from the last observed capture interval and
  re-anchors absolutely against `videoOriginUs_`, so extrapolation error cannot
  accumulate. With no interval observed it collapses to one tick and warns once —
  the only available answer when the producer supplied no timing. Do not
  reintroduce an incrementing frame counter as "just a sanity check"; it becomes
  a second source of truth for the timeline.
- **Do not revive a parallel frame counter.** `GrabbedFrame::frameNumber` has no
  producer: `FrameGrabber::frameNumber_` is only ever reset, never incremented,
  and `submitVideoFrame` never sets it, so the field is always 0. PTS derives from
  time; a second source of truth would be worse than none.
- **Stated limitation: duration tracks frame count, not wall time.** When the
  renderer outruns the nominal record fps, consecutive frames collapse to one tick.
  That is the mismatch `VisualizerRenderer::startRecording` already warns about;
  fixing it is frame-rate conversion, a different feature. Do not report it as a
  regression.
- **projectM v4 has exactly one render destination: framebuffer 0.** It binds
  `GL_DRAW_FRAMEBUFFER` to 0 for its closing texture copy, next to the upstream
  note "ToDo: Allow external apps to provide a custom target framebuffer".
  **GL compositing of Qt Quick overlays is therefore impossible** — framebuffer
  0 is precisely the framebuffer the Quick scene graph owns, so projectM and Quick
  would fight over the same draw target every frame. This is why overlay and
  karaoke burn-in is an FFmpeg post-process and not a compositing problem. A
  recording resolution that differs from the window's is a *readback scale*
  (`RenderTarget::blitFromDefault`) — the one remaining FBO use, and a copy target
  by construction. Do not add an FBO as a projectM render target.
- **`FrameGrabber::flipImage` is load-bearing.** `glReadPixels` returns rows
  bottom-up and the encoder uploads them as they arrive. The correct CPU
  implementation sat dead behind a `useGPUFlip_` branch nothing could reach.
  Deleting the single call fails the integration test with "captured rows are the
  raw bottom-up readback, i.e. upside down" while the non-emptiness test still
  passes — a mutation check worth preserving, so do not weaken that assertion to
  a check the pipeline cannot satisfy.

## Lyrics serialization and queries

- **There is exactly one owner per lyrics format.** `LyricsBridge` owns SRT, LRC
  and ASS. Two formatters exist to drift; a test pins the live bridge's exact
  output bytes so a second one reappearing fails. Do not re-add a formatter to
  `LyricsData` or to a free function.
- **The time unit is a parameter, and that is load-bearing.** It is not a shared
  constant. SRT is milliseconds; LRC and ASS are centiseconds. Routing SRT
  through a centisecond helper quantises twice — `1.4567s` is `1457ms` scaled
  directly but `1460ms` via `146cs` — silently changing shipped bytes. Durations
  are differences of two *absolute* centisecond stamps rather than
  independently-rounded spans, because a player accumulates preceding durations
  and per-word rounding compounds (200 words of 0.333 s sum to 6660cs where the
  naive form gives 6600cs).
- **Prove equivalence by differential test in C++, never by a reimplementation.** A
  Python reimplementation of both sides of the old/new expression had already let
  one bug through this cycle precisely because the wrong format string got
  transcribed into both sides. If you change one side, diff the two expressions as
  real code over the finite-float domain.
- **LRC has no escape syntax, so brackets are substituted, not escaped:**
  `[`→U+FF3B, `]`→U+FF3D, CR/LF→space. Substituting only `[` leaves
  the file unbalanced, with a surviving `]` a tag reader could still truncate at;
  substituting both makes "every remaining ASCII bracket is a real tag delimiter"
  a checkable invariant. SRT is deliberately untouched — it has no header, so
  there is no tag grammar to forge. Do not add an "escape" backslash to LRC.
- **The three context-line implementations diverge in OPPOSITE directions.**
  `LyricsBridge` clamps an index of −1 to 0 and always shows at least line 0,
  while `LyricsSync` returns empty — but for `getUpcomingLines` the divergence
  reverses: `LyricsSync` reads −1 as "line 0 is current" and starts after it,
  while the bridge reads it as "the whole song is still to come" and includes
  line 0.
  **Delegating `getUpcomingLines` to `LyricsSync` would drop line 0** from the
  pre-render buffer. Signatures also differ (`int` vs `size_t`), pinned by two
  `static_assert`s — the `size_t` form makes the negative case unrepresentable
  rather than clamped, which is why the pair is not interchangeable. Both
  agreements and both single disagreements are pinned by tests so neither can be
  "fixed" into the other by accident. Unifying them needs a per-function product
  decision and a new `LyricsSync` overload, not a refactor.
- **`checkedIndex` is the single bounds gate** across the whole lyrics query path.
  Every subscript routes through it; do not reintroduce ad-hoc `int`/`size_t`
  mixing at the QML bridge boundary, and do not wrap an index and then defend
  against the wrap — saturate against the real bounds.

## Testing and the build

- **A suite being in the CMake source list is not evidence it runs.** This has now
  bitten twice: `test_PresetScanner` was compiled into `unit_tests` with no
  runner, and `test_SunoEndpoints` defined `runTestSunoEndpoints` that
  `test_main.cpp` never declared or called — the fail-closed host-allowlist guard
  had never actually run, masked by a static initializer that called
  `std::abort()`. Both are now declared *and* called from `main()`. A static
  initializer that aborts to make a suite noticeable is a trap, not a safety net.
- **ctest must be pointed at `build/tests`.** `--test-dir build` discovers zero
  tests and still exits 0, because that directory has no `CTestTestfile.cmake`.
  Read the discovered count, not the exit code. The full suite inventory and the
  per-command detail live in [TESTING.md](TESTING.md).
- **`QOffscreenSurface` is unusable for anything larger than 1x1 on macOS** — Qt
  hands it a 1x1 drawable, so a larger `glReadPixels` is out of bounds and returns
  zeros with `GL_INVALID_FRAMEBUFFER_OPERATION` (0x506). The GL fixture is a real
  `QWindow` with `setSurfaceType(QWindow::OpenGLSurface)` and an event-loop spin
  until `isExposed()`. The macOS `offscreen` QPA plugin
  **cannot create an OpenGL context at all** ("This plugin does not support
  createPlatformOpenGLContext!"), so the headless/GL suite split is real and
  must not be collapsed. **A skip is not a pass** — the GL entry is the one that
  counts.
- **A test-function argument list is replayed against every suite in the binary.**
  `test_main.cpp` therefore runs the GL suite alone when arguments are present,
  or every other class fails with "Function not found". Do not "simplify" that
  dispatch into a per-suite filter.
- **Only these 7 `cmake/` modules are in active use, and they must not be deleted:**
  `CPM.cmake`, `Compiler.cmake`, `Dependencies.cmake`, `FindProjectM4.cmake`,
  `Install.cmake`, `Sources.cmake`, and `TargetSetup.cmake`. All are included by
  the top-level `CMakeLists.txt`.
- **The `clang-format` gate is diff-scoped, not whole-repo.** Violations fall from
  6526 to 4667 across `src/`, but 19 files indent with tabs, 5 use 2 spaces, 1
  uses Allman (`SunoExploreService.cpp`), 83 files carry trailing whitespace on
  blank lines (clang-format always strips it and no option preserves it), and
  ~1456 continuation lines wrap well before column 100. Use `git clang-format`. A
  one-time `src/` normalization pass is the prerequisite for a whole-repo gate. Do
  not restore `ColumnLimit: 0` — only ~184 of ~26 000 `src/` lines exceed 100
  chars, so the extra findings are real over-wrapping, and a `ColumnLimit: 0`
  formatter can never enforce width.
- **`CMAKE_OSX_SYSROOT` is not the fix for `clang-tidy`, and is actively unsafe.**
  Measured: adding the SDK the compiler already reports for itself *drops*
  `/usr/local/include` from `/usr/bin/c++`'s search list (6 entries become
  5), stripping the project's only route to fmt, spdlog, toml++, glm and taglib.
  The real cause is that `build/compile_commands.json` records Apple clang's
  *driver* command line, and a driver command does not contain what the driver
  itself injects — the SDK, libc++ and `/usr/local/include` appear only in the
  `cc1` line. The working invocation is documented in the `.clang-tidy` header;
  use it rather than inventing a new flag.
- **An anchor is only trustworthy if your slugger matches the real one.** Two
  link-checker reports disagreed in opposite directions: one collapsed whitespace
  where the slugger keeps one hyphen per space, the other assumed em-dash
  stripping left a single hyphen. The long-standing
  `#appendix-a--explicit-conflict-register` anchor is correct — do not "fix" it.
- **Dependabot covers `github-actions` only, and the file says so.** It cannot
  see the 6 CPM pins or the 9 `find_package` lookups. Do not let it imply coverage
  it does not have, and do not add a fabricated ecosystem section to imply it.

## Suno evidence

These rules are about evidence discipline, not code. They exist because the Suno
surfaces in this client are capture-derived and unofficial; see
[`suno_api/README.md`](../suno_api/README.md) for which file owns which kind of
fact.

- **Naming a captcha provider adds no captcha host to any allowlist.** Clerk auth
  is Cloudflare Turnstile; the suno.com web sign-in UI is hCaptcha; the generation
  captcha is Turnstile v2 with `FORCE_ENABLE_CAPTCHA = false`. Confusing the two
  is how a host enters an allowlist that should not carry it.
- **`/v1/verify` is not `/v1/client/verify`, and is still unobserved.** The two are
  kept separate in four places. Do not merge them, and do not treat one capture
  of the Clerk heartbeat as evidence about the other.
- **`classifyStoredCredential` classifies shape, not sufficiency.** It now
  prefix-matches `__client_uat` / `__client` / `__session` against a longest-first
  table, and the ordering is for unambiguous *reporting* only — `__client` is a
  prefix of `__client_uat`, so the short name is never tested first. Whether a jar
  carrying **only** `__client_uat` *should* be sufficient is an open question: the
  master documents that cookie as non-secret session-presence metadata, "not a
  credential". **Do not resolve it without a capture.** Cookie names arrive from a
  user paste, so they are untrusted text: names are quoted, values never are, and a
  name failing a charset check or over 64 chars is counted, not reproduced.
- **Do not widen the downloader's `content_type == "mp3"` filter on inference.** A
  captured `m4a-opus`/`progressive` item is therefore unplayable. Widening it
  needs a capture showing whether Suno serves Opus on `audiopipe.suno.ai` without
  the web-side Mango decryptor the recon documented. The download layer reports
  which fail-closed predicate fired, so this is a visible error rather than a dead
  button, and a test pins the current shape so a future widening fails
  deliberately.
- **Keep "do not assume array position" for the session selector.** The `sid`-claim
  re-derivation is corroborated by one capture with a single session —
  `last_active_session_id` == `sessions[0].id` == the JWT `sid` claim == the
  `{sid}` path segment. That is corroboration of the substitution, not proof for
  multi-session accounts.
- **A 401 proves route existence and nothing else** — no request, no response
  body. `x-matched-path` plus probe verdicts reach the `[LEAD]` definition by a
  much better oracle, but they must not be promoted to `[T1]`, and nothing may be
  wired on them alone. Existence, contract and entitlement are three separate
  axes.
- **The `Implemented` column is a useful periodic re-audit.** `SunoEndpoints.hpp`
  has 77 route constants of which exactly 12 are referenced; the 65 dead ones are
  the real measure of how much captured surface is unwired.
- **Recording a secret-shaped string in prose is not a substitute for a commit.**
  Names and values from a user paste are untrusted input; see the classifier rule
  above. Treat any prose note about a real account, clip or URL as a leak the same
  way you would treat the literal itself.
