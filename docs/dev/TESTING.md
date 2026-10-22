# Testing ChadVis

The test tree is wired through CMake and registers **fourteen** CTest entries: one aggregate `unit_tests` binary, **eleven** standalone unit executables, and two entries over a *single* `integration_tests` binary — an offscreen QML startup check, and a native-OpenGL framebuffer suite that selects only the GL test functions. A green `ctest` run is 14 tests, not 11 or 12.

This file documents the **automated** tests and how to run them. The interactive GUI checklist lives in [manual-qa.md](manual-qa.md) as MT-001.

## Build and Test Commands

```bash
./build.sh
ctest --test-dir build/tests --output-on-failure
```

### Gotcha: a build directory remembers its Qt, and a half-upgraded Homebrew lies about it

`CMAKE_PREFIX_PATH` is cached. Changing `CHADVIS_QT_PATH` or passing `--qt` to an **existing** build directory does not take effect — the cache still holds the old prefix, and the failure names files from the *old* Qt, not the one you just pointed at. AGENTS.md warns about this in general; the concrete symptom on this machine was:

```
The imported target "Qt6::harmonydeployqt" references the file
"/usr/local/opt/qt/bin/harmonydeployqt" but this file does not exist
```

which reads like a broken project and is a broken package manager. `brew upgrade` had left the `qt` umbrella at 6.11.1 while qtbase, qtdeclarative, qtmultimedia and qttools all moved to 6.12.0, so the umbrella's cmake files described a `bin/` that no longer had those tools in it.

Two distinct failures live here and they are worth telling apart:

* **Components split across prefixes.** `Qt6Config.cmake` only looks for its components *beside itself*, so `CMAKE_PREFIX_PATH="/usr/local/opt/qtbase;/usr/local/opt/qtdeclarative"` does not find `Qt6Multimedia` — it is not next to qtbase's config. Homebrew ships the umbrella broken, so `scripts/qt-stitch.sh` builds `.qt-stitch/`: one prefix of symlinks uniting the 6.12.0 formulae. Nothing is copied and Homebrew is not touched; delete the directory to undo it.

```bash
scripts/qt-stitch.sh
mv build-fast .backup_graveyard/        # the cache still names the old Qt
./build.sh --fast --tests --qt "$PWD/.qt-stitch"
```

The stitch is a workaround, not a fix. `brew upgrade qt` completing removes the need for it. `scripts/qt-stitch.sh` reports which components it found, and writes nothing into `/usr/local` — an early draft did, creating `*.d` directories inside qtbase, which is why the merge order in that script is load-bearing and commented as such.

### Gotcha: `--test-dir build` is a false green

`ctest --test-dir build` reports **SUCCESS while discovering zero tests**, and it will keep doing so until someone notices there is no `build/CTestTestfile.cmake`. The test tree is configured by `tests/CMakeLists.txt`, so the generated `CTestTestfile.cmake` lands in `build/tests/`, not `build/`. Confirm it before you trust a green run:

```bash
ls build/tests/CTestTestfile.cmake   # exists
ls build/CTestTestfile.cmake         # does not exist
```

CTest treats "no tests found" as a success exit code, so the wrong directory is indistinguishable from a real pass unless you read the test count. Use `build/tests`. A correct run prints 14 tests and ends with `100% tests passed, 0 tests failed out of 14`.

`build.sh` performs an incremental build by default and preserves the existing CMake configuration. Use `./build.sh --rebuild` (or `--clean`) for a clean rebuild; `-d`/`--debug` and `-r`/`--release` explicitly select a configuration. The script does not accept a `run` argument.

## Automated Test Inventory

### `unit_tests` (aggregate)

`tests/unit/CMakeLists.txt:1-33` builds a single `unit_tests` executable from **39** sources:

| Source | Coverage |
| --- | --- |
| `tests/unit/test_main.cpp` | Aggregate runner; declares and calls all 38 `runTest*` entry points. |
| `tests/unit/core/test_Logger.cpp` | Logger lifecycle coverage. |
| `tests/unit/core/test_ConfigParsers.cpp` | TOML parsing and serialization coverage. |
| `tests/unit/core/test_ConfigLoader.cpp` | Every serialized field surviving a round trip; the `[recording.video]`/`[recording.audio]` sub-tables written as sub-tables rather than collapsed onto one key; **no credential reaching either the serialized document or the saved file, across three successive saves**, while the legacy values stay readable for migration; `overlay.enabled` round-tripping in both states; unknown keys dropped; and corrupt, malformed or missing input diagnosed rather than thrown on. Also pins that the shipped `config/default.toml` declares every key the serializer emits. |
| `tests/unit/core/test_FileUtils.cpp` | Filename sanitization coverage. |
| `tests/unit/core/test_Signal.cpp` | The corrected cost model: an rvalue emit moves and never copies, an lvalue emit copies exactly once per subscriber, a `const&` payload is not copied at all; plus the emit-chain guards — disconnecting from inside a slot takes effect on the *next* emit, a nested emit does not take the outer emit's chain cleanup, and a throwing subscriber does not strand the chain. |
| `tests/unit/core/test_Version.cpp` | `--version` banner pinned to `version.txt`: the injected `CHADVIS_VERSION`, the exact banner text, no stale `1.0.0` literal, and the real binary's `--version` output. |
| `tests/unit/recorder/test_RecordingPipeline.cpp` | Frame submissions reach the encoder, recorder audio queue drained before and after the worker starts, sanitized container path from config. |
| `tests/unit/recorder/test_AudioFileDecoder.cpp` | Exact-value PCM survival from synthesis through `avcodec`/`swresample`; a mono source arriving in stereo at unity rather than at `swr`'s default −3 dB rematrix; the drained-resample frame count that makes the final flush load-bearing; a truncated WAV reported as indistinguishable from a short one; bounded-queue drop accounting and a worker draining every chunk; a real AAC track in an MP4 decoded to completion; and soxr selected only when the library actually accepted it, with a stated reason when it did not. |
| `tests/unit/recorder/test_RenderQueue.cpp` | The job model and scheduler with no worker, GL, FFmpeg or real I/O — which is what lets the concurrency ceiling be *proved*: failure classification (a full disk permanent, errno overriding a worker's guess), every state/slug round-trip, validation naming the field it rejected, the versioned render receipt (exact round trip, corrupt receipts refused distinctly, **a receipt refusing to certify a short render or a non-terminal outcome**), frame-weighted batch progress, FIFO handing-out with a retry jumping the queue, and a claim of completion after too few frames settling as partial. |
| `tests/unit/audio/test_Playlist.cpp` | Selection notification, snapshot lifetime, re-entrant reads, the thread contract, shuffle/repeat traversal, skip-while-paused, end-of-track advance, and destruction flushing the session playlist. |
| `tests/unit/audio/test_PcmFormat.cpp` | Bit-exact `float` copy including NaN; `int16` scaling exact for *every* possible input, not a sample of them; interleaved stereo ordering; and every rejection named rather than partially applied — unsupported format, sample ceiling boundary, destination too small, oversized by one, truncated source. |
| `tests/unit/audio/test_LoudnessAnalysis.cpp` | K-weighted loudness against exact references on synthesized sines (full-scale 1 kHz is not zero, a −20 dB sine measures −20 LUFS), digital silence gated out rather than averaged in, and gain application exact with 0 dB touching nothing. |
| `tests/unit/visualizer/test_PresetScanner.cpp` | Preset scanning, the hoisted author regex across filename shapes and repeated scans, and async rescan: pinned generations stay valid, a missing publish context falls back to sync, a failed scan keeps the published generation. |
| `tests/unit/suno/test_AuthModule.cpp` | JWT, auth headers, and file-backed credential-store coverage. |
| `tests/unit/suno/test_StoredCredentialClassification.cpp` | Credential-shape classification and confirmation that diagnostics leak no credential material. |
| `tests/unit/suno/test_CredentialRestoreWorker.cpp` | Restore worker never blocks on the credential backend from its constructor thread, concurrent restores coalesce, empty restore reopens the gate, and a discarded restore still notifies every waiter. |
| `tests/unit/suno/test_SunoLibraryManager.cpp` | Library refresh waits for an in-flight credential restore instead of treating it as signed out. |
| `tests/unit/suno/test_AuthCoordinator.cpp` | Insecure backend refuses from signed out, secure backend without capture backing refuses, one validated callback installed then local sign-out, distinguishable keychain read-status mapping. |
| `tests/unit/suno/test_LoopbackListener.cpp` | Ephemeral loopback port binding, valid callback acceptance with a fixed page, silent rejection of malformed requests, pipelined extra request ignored. |
| `tests/unit/suno/test_OAuthLoginService.cpp` | Transaction entropy/uniqueness, known S256 challenge vector, state-string QML contract stability, empty launch staying capture-gated without opening a browser, single-flight `begin`, mismatched-state rejection, deadline expiry. |
| `tests/unit/suno/test_ClipParser.cpp` | Captured clip and `feed/v3` envelope parsing coverage. |
| `tests/unit/suno/test_DownloadQueue.cpp` | Failure classification, backoff, resume, FIFO concurrency, cancellation, and atomic file replacement using injected fake replies. |
| `tests/unit/suno/test_CredentialStorePolicy.cpp` | The platform-free half of `CredentialStore`: each platform selecting its own backend **only when that backend is compiled**, no backend compiled failing closed rather than writing plaintext, `CHADVIS_NO_KEYCHAIN` outranking a working backend (on Windows too), every Secret Service failure mapped to its own reason and reported in the order it is checked, a locked collection winning over an absent one, and no refusal message ever claiming success or naming a file. Plus UTF-8 secret encoding (lone and mismatched surrogates refused, a valid pair accepted, the secret never echoed) and the byte ceilings. |
| `tests/unit/suno/test_SunoDownloader.cpp` | The save/play seam: a save-only batch never touches the playlist, save-and-play still selects the downloaded clip, an existing file is reused without playing, **same-title clips get distinct files**, a live request is escalated rather than discarded, a failed download saves nothing and leaves playback alone, and both refusals — no captured `mp3`, and WAV — happen before anything is resolved and say why. |
| `tests/unit/suno/test_SunoEndpoints.cpp` | Endpoint-map invariants, including the single-`/api` composition rule and allowed URL shapes. |
| `tests/unit/suno/test_BoundedBody.cpp` | The bounded reader as the *mandatory* accessor for a pumped reply: `readAll()` reads empty, the accessor returns every byte and not a prefix, a body exactly at the cap is accepted while the overflowing byte is detected but never stored, a hostile origin is cut off one byte past the cap and aborts mid-stream, and each migrated call site names the limit instead of reporting invalid JSON — while still parsing valid pages through it. Also one reader per live reply and none after sign-out, and a repo-level check that no call site reads a body directly off a pumped reply. |
| `tests/unit/lyrics/test_LyricsPipeline.cpp` | Captured aligned-payload fields and metadata lyrics, invalid-payload rejection, sync line selection and clamped progress, empty load as terminal, downloader signal after an existing file jump, search and SRT/LRC export, lazy bridge updates. |
| `tests/unit/lyrics/test_LyricsExport.cpp` | ASS header and lyric golden output including `\kf` word timing, a timestamp with non-zero centiseconds surviving, braces escaped and newlines neutralised so **a lyric or title cannot forge a header key**, a degenerate remote payload still producing a valid file, and the SRT/LRC golden bytes surviving non-finite times. |
| `tests/unit/lyrics/test_LyricTiming.cpp` | The reflow model exact on a worked example, undo restoring a bitwise-identical document after a multi-step drag, plausibility refusing the named violations while accepting a good document, and an exact serialisation round trip on every field. |
| `tests/unit/util/test_PathSafety.cpp` | `sanitizeFilename` as a single component and traversal-proof: the byte budget never splits a UTF-8 sequence, composed and decomposed spellings produce one name, titles differing only by a combining mark do not collide, Win32 reserved names are refused in every spelling **and truncation cannot manufacture one**, traversal yields a bare filename, an embedded NUL cannot truncate silently. Plus `parseHexColor`'s verdicts, a remote `.m3u` line never becoming a track, and a session file written by `saveM3U` round-tripping its titles and durations. |
| `test_SunoBridge.cpp` | `tests/unit/qml_bridge/test_SunoBridge.cpp` | Playlist mutation refusal paths (`T0010`), unrecognised `updateType` refused rather than guessed into an add, recognised types reaching the service unrefused. |
| `test_CliArgs.cpp` | `tests/unit/core/test_CliArgs.cpp` | Every CLI flag parsing alone, `--headless --record` refused in both orders and via `-r`. |
| `test_SunoAuthFailure.cpp` | `tests/unit/suno/test_SunoAuthFailure.cpp` | Auth-failure classification on the status axis only. |
| `test_SunoDatabase.cpp` | `tests/unit/suno/test_SunoDatabase.cpp` | Registration lifecycle, pragmas observed through a second connection, clip round trips. |
| `test_LyricsFetchQueue.cpp` | `tests/unit/suno/test_LyricsFetchQueue.cpp` | Deduplication, FIFO fairness when over cap, "still processing" re-entry. |
| `test_DownloadEntitlement.cpp` | `tests/unit/suno/test_DownloadEntitlement.cpp` | The download-product lock is reported, never enforced: a locked clip still transfers, the notice claims no quota, and the format/status/on-disk branches are unchanged. |
| `test_SunoWorkspace.cpp` | `tests/unit/suno/test_SunoWorkspace.cpp` | Workspace refusal kinds and the honest-state suite. |
| `test_PresetBridge.cpp` | `tests/unit/visualizer/test_PresetBridge.cpp` | A filtered row selects and favourites *itself*, never slot 0 (the `T0002`/`T0003` regression guard). |

The aggregate runner invokes **38** suites (`tests/unit/test_main.cpp`): logger, config parsers, config loader, file utils, version, signal, CLI args, recorder, audio file decoder, render queue, playlist, preset scanner, preset bridge, Suno endpoints, Suno auth failure, auth module, stored-credential classification, credential restore worker, library manager, lyrics fetch queue, Suno database, auth coordinator, loopback listener, OAuth login service, clip parser, download queue, credential store policy, Suno downloader, download entitlement, bounded body, Suno workspace, lyrics pipeline, lyrics export, lyric timing, PCM format, loudness analysis, path safety, and Suno bridge.

**Every one of the 38 sources above is invoked.** There are no compiled-but-unrun translation units in the aggregate target, and there are no static-initialization test hooks. Verified by set difference on this session: the 38 aggregate sources plus the 11 standalone sources account for **50 of the 51** `.cpp` files under `tests/unit/`. The one exception, `visualizer/test_PresetManager.cpp`, is declared nowhere and never builds (T0306). That was not always true, and the failure mode is the one this repository keeps re-learning:

- `test_PresetScanner.cpp` was in `CMakeLists.txt` for a long time with no `runTest*` entry point in the runner, so its assertions never executed. `runTestPresetScanner` is now declared at `tests/unit/test_main.cpp:14` and called at `:49`.
- `test_SunoEndpoints.cpp` ran from a static initializer and `std::abort()`ed on failure, which masked the failure behind a process kill instead of the aggregate exit code. `runTestSunoEndpoints` is now declared at `tests/unit/test_main.cpp:15` and called at `:50` like every other suite.

Treat both as closed, and do not reintroduce either shape. A new aggregate source needs **two** edits: the `add_executable` source list *and* a `runTest*` declaration plus a `status |=` call in [test_main.cpp](../../tests/unit/test_main.cpp).

Two directories under `tests/` are **empty and named by no CMakeLists**: `tests/unit/projectm/` and `tests/manual/`. Nothing is lost by ignoring them, but nothing runs from them either, so do not read either as a home for a test you are about to add without declaring the source.

### Standalone unit executables

Eight suites build as their own targets and register with CTest individually, because each carries its own `main()` or needs a different application object than the aggregate's `QCoreApplication`:

| Target | Source | Focus |
| --- | --- | --- |

### `integration_tests` and `integration_gl_tests`

Both entries run the **same** `integration_tests` binary, built from `tests/integration/CMakeLists.txt:1-4` with two sources: `test_main.cpp` and `test_ProjectMFramebuffer.cpp`. They differ only in environment and in which test functions are selected. When a test function is named on the command line, the runner runs **only** the GL suite (`tests/integration/test_main.cpp:55-58`) — replaying those names against the QML class would fail the run with "Function not found".

#### `integration_tests` — offscreen QML startup and render

CTest registers it with `QT_QPA_PLATFORM=offscreen` (`tests/integration/CMakeLists.txt:31-34`), so it needs no display. `tests/integration/test_main.cpp` is a **real test case**, not an empty harness: `mainQmlLoadsAndRenders()` (`tests/integration/test_main.cpp:18-43`)

1. Constructs a real `vc::AudioEngine` and requires `init()` to succeed.
2. Registers the real QML bridges into a fresh `QQmlApplicationEngine`.
3. Loads `qrc:/qt/qml/ChadVis/src/qml/main.qml` — the same document the app loads.
4. Requires a non-empty root-object list, a `QQuickWindow` root, `isVisible()`, and `isExposed()` within a 3-second retry window.
5. Grabs the window and requires a non-null frame with non-zero width and height.

This entry also runs the GL suite, and on macOS **every one of the 7 GL tests skips with a stated reason** rather than passing: the `offscreen` QPA plugin cannot create an OpenGL context at all (`This plugin does not support createPlatformOpenGLContext!`), which the probe at `tests/integration/test_ProjectMFramebuffer.cpp:53-64` detects. So what a green `integration_tests` proves is that the QML module loads, instantiates, and produces a frame. It is **not** evidence about the visualizer's OpenGL context, and it is not evidence about the recorder.

#### `integration_gl_tests` — native-OpenGL framebuffer suite

The last registered entry, added at `tests/integration/CMakeLists.txt:36-48`, runs the same binary under the platform's **native** QPA plugin — `cocoa` on macOS, whatever the session has elsewhere — with no `QT_QPA_PLATFORM` override and a 120-second timeout, selecting exactly the 7 GL test functions:

| Test | What it pins |
| --- | --- |
| `engineDrawsIntoDefaultFramebuffer` | After 30 rendered frames the default framebuffer holds a picture — a non-black byte count, not a `!frame.isNull()` check that solid black would pass. |
| `engineIgnoresAnFboBoundAsDrawTarget` | With an FBO bound as the draw target every frame, the FBO reads back **0** non-zero bytes and the default framebuffer reads back a picture, so an offscreen FBO would have stayed empty. |
| `rendererRecordingPathDeliversPixels` | The renderer's own capture path emits a full-size frame with non-zero bytes, not the FBO's own `glClearColor`. |
| `capturedFramesAreFlippedIntoTopDownRows` | The capture's row 0 is the raw `glReadPixels` bottom row — the frame is top-down, not upside down. |
| `encodedFileContainsTheVisualizerPicture` | 45 frames through the real recorder produce an H.264 MP4 that decodes to a 256x256 stream whose brightest frame has lit pixels. Every test above stops at the readback, so this is the one that would catch an encoder fed an empty buffer. |
| `recordingAtADifferentResolutionIsScaled` | A recording resolution that differs from the window's is scaled on the readback, so the encoder is never fed a wrongly sized buffer. |
| `liveWindowCapturesFrames` | The real `VisualizerWindow` — own GL context, expose event, render timer, buffer swap — captures a full-size non-black frame. The other six drive the renderer directly, so nothing here would catch a capture that only works while the window is not presenting. |

**A skip is not a pass.** These 7 are the only automated proof that the recorder's framebuffer path works; a run that reports them as skipped has proven nothing about it. When you touch the recorder, the visualizer renderer, or projectM, run this entry:

```bash
ctest --test-dir build/tests -R integration_gl_tests --output-on-failure
```

If it reports skips, read the reason. "QOpenGLContext failed to create; this QPA plugin cannot provide a GL 3.3 core context" means you are on a headless session with no usable platform plugin; the tests need a real drawable, because `QOffscreenSurface` is unusable here — on macOS Qt hands it a 1x1 drawable, so any larger readback is out of bounds and returns zeros (`tests/integration/test_ProjectMFramebuffer.cpp:66-70`). Video-page behavior, window embedding, and persistence still need the real GUI session in [manual-qa.md](manual-qa.md).

### Registered suites at a glance

Fourteen entries: twelve in `tests/unit/CMakeLists.txt`, two in `tests/integration/CMakeLists.txt`.

| Suite | Kind | Environment |
| --- | --- | --- |
| `unit_tests` | Aggregate, 38 invoked suites from 39 sources | console |
| `test_HttpPolicy` | Standalone unit (own `main()`) | console |
| `test_CapturedHosts` | Standalone unit, host registry | console |
| `test_FeatureFlags` | Standalone unit | console |
| `test_OffscreenRenderSpike` | Standalone unit (`QGuiApplication`, real GL drawable) | console, 180 s timeout |
| `test_RenderExecutor` | Standalone unit (`QGuiApplication`) | `QT_QPA_PLATFORM=offscreen`, 180 s timeout |
| `test_SunoAudioUploadService` | Standalone unit | console |
| `test_SunoRequestEpoch` | Standalone unit | console |
| `test_SunoExploreService` | Standalone unit | console |
| `test_SunoNotificationService` | Standalone unit | console |
| `test_ClerkAuthClient` | Standalone unit | console |
| `integration_tests` | Offscreen QML startup and render; GL tests skip under `offscreen` | `QT_QPA_PLATFORM=offscreen` |
| `integration_gl_tests` | 7 native-OpenGL framebuffer tests, same binary | native plugin (no override), 120 s timeout |
| `test_ThemeQml` | Standalone unit; loads the `ChadVis` QML module and the `Theme` singleton | `QT_QPA_PLATFORM=offscreen` |

### Outside ctest: two checks that are not CTest entries

Both are run by hand and both need to be run by hand, because ctest is not a
place either of them belongs.

| Check | Command | Why it is not a CTest entry |
| --- | --- | --- |
| QML lint | `./scripts/lint-qml.sh` | Runs the build's own `project_lib_qmllint` target, so it needs a configured build directory. Invoking `qmllint` directly cannot resolve this tree — `main.qml` uses relative imports. |
| Theme contrast | `python3 scripts/check-theme-contrast.py` | Needs neither Qt nor a build: it re-implements `Theme.qml`'s ramp and WCAG relative luminance in Python and reads the tokens straight out of `src/qml/styles/ThemeDefinitions.qml`. Exits non-zero when a gated pair fails, so it can gate CI; `--suggest` prints measured replacements instead of guesses. It answers "is this text legible", **not** "does this look right" — the latter is MT-001-G. |

## Running One Suite

```bash
./build.sh
ctest --test-dir build/tests -R unit_tests --output-on-failure
./build/tests/unit/unit_tests                                   # aggregate, all 38 suites
./build/tests/unit/test_HttpPolicy                              # single standalone target
./build/tests/unit/test_RenderExecutor                           # needs QT_QPA_PLATFORM=offscreen
QT_QPA_PLATFORM=offscreen ./build/tests/integration/integration_tests
ctest --test-dir build/tests -R integration_gl_tests --output-on-failure   # the GL proofs
```

## Scope Notes

### The sanitizer lanes

`AGENTS.md` says the lanes exist and that they run before merging recorder or session work. This section says how to actually run them.

```bash
./build.sh --tsan --tests  && ./build.sh --tsan --safe   # data races
./build.sh --asan --tests                                # memory errors; --safe misses the gl surfaces
./build.sh --ubsan --tests
```

Each profile has its own build directory, so `--fast` and `--asan` builds coexist. Two traps worth knowing before you spend a day on one:

**`detect_leaks` is not supported on macOS.** Setting `ASAN_OPTIONS=detect_leaks=1` on this box makes ASan abort at initialization with a single "detect_leaks is not supported on this platform" line and kills every test in the suite, which reads as N failures and is one. Omit the flag to run ASan here. LeakSanitizer does run on Linux, so `.github/workflows/asan.yml` still carries it and CI keeps that coverage.

**`--safe` does not reach the surfaces that matter for ASan.** `SAFE_TESTS` in `build.sh` lists the ten headless-safe entries; `integration_tests` and `integration_gl_tests` are deliberately excluded per `AGENTS.md` ("a green run is not evidence for GL, burn-in, or audio"). The ASan lane needs them regardless: `ctest --test-dir build-asan/tests --output-on-failure -j2` runs all thirteen.

Three-outcome classification, per the workflow. `exit 0` is clean. `exit 66` (TSan, the configured exitcode) and `exit 67` (ASan) mean findings worth reading. **Anything else means the lane itself broke** — a sanitizer runtime init failure, or SIGABRT — and must never be read as "clean", which is why the workflows carry the summary line saying which happened.

- There is no Qt Widgets menu bar and no separate full-window projectM stage in this interface. The projectM surface is the native window embedded in the Video page; see [ARCHITECTURE.md](ARCHITECTURE.md) and [../integration/PROJECTM.md](../integration/PROJECTM.md).
- **Create has no `B-Side Chat` tab.** `src/qml/views/CreateView.qml` declares no tab strip. The B-Side surface is unavailable because its Modal/Orpheus routes remain `[LEAD]` and unverified, matching [../user/USAGE.md](../user/USAGE.md). Earlier revisions of this document claimed otherwise; that was wrong. Do not reintroduce the claim, and do not ship the tab, until a direct capture promotes those routes.
- Generation is intentionally disabled in the Create UI until a supported CAPTCHA token flow exists, so no automated test may assert a successful generation round-trip.
- Any new test must be added to `tests/unit/CMakeLists.txt` with an `add_test()` call. Adding a source file alone produces a binary that CTest never runs, which is the same class of false green as the wrong `--test-dir`. The aggregate target has the sharper version of the same trap: a source in `add_executable` with no `runTest*` declaration and `status |=` call in [test_main.cpp](../../tests/unit/test_main.cpp) compiles, links, and never asserts anything.
