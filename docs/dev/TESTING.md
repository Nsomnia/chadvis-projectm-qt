# Testing ChadVis

The test tree is wired through CMake and registers **nine** CTest suites: one aggregate `unit_tests` binary, six standalone unit executables, and two entries over a *single* `integration_tests` binary — an offscreen QML startup check, and a native-OpenGL framebuffer suite that selects only the GL test functions. A green `ctest` run is 9 tests, not 8.

This file documents the **automated** tests and how to run them. The interactive GUI checklist lives in [manual-qa.md](manual-qa.md) as MT-001.

## Build and Test Commands

```bash
./build.sh
ctest --test-dir build/tests --output-on-failure
```

### Gotcha: `--test-dir build` is a false green

`ctest --test-dir build` reports **SUCCESS while discovering zero tests**, and it will keep doing so until someone notices there is no `build/CTestTestfile.cmake`. The test tree is configured by `tests/CMakeLists.txt`, so the generated `CTestTestfile.cmake` lands in `build/tests/`, not `build/`. Confirm it before you trust a green run:

```bash
ls build/tests/CTestTestfile.cmake   # exists
ls build/CTestTestfile.cmake         # does not exist
```

CTest treats "no tests found" as a success exit code, so the wrong directory is indistinguishable from a real pass unless you read the test count. Use `build/tests`. A correct run prints 9 tests and ends with `100% tests passed, 0 tests failed out of 9`.

`build.sh` performs an incremental build by default and preserves the existing CMake configuration. Use `./build.sh --rebuild` (or `--clean`) for a clean rebuild; `-d`/`--debug` and `-r`/`--release` explicitly select a configuration. The script does not accept a `run` argument.

## Automated Test Inventory

### `unit_tests` (aggregate)

`tests/unit/CMakeLists.txt:1-21` builds a single `unit_tests` executable from **19** sources:

| Source | Coverage |
| --- | --- |
| `tests/unit/test_main.cpp` | Aggregate runner; declares and calls all 18 `runTest*` entry points. |
| `tests/unit/core/test_Logger.cpp` | Logger lifecycle coverage. |
| `tests/unit/core/test_ConfigParsers.cpp` | TOML parsing and serialization coverage. |
| `tests/unit/core/test_FileUtils.cpp` | Filename sanitization coverage. |
| `tests/unit/core/test_Version.cpp` | `--version` banner pinned to `version.txt`: the injected `CHADVIS_VERSION`, the exact banner text, no stale `1.0.0` literal, and the real binary's `--version` output. |
| `tests/unit/recorder/test_RecordingPipeline.cpp` | Frame submissions reach the encoder, recorder audio queue drained before and after the worker starts, sanitized container path from config. |
| `tests/unit/audio/test_Playlist.cpp` | Selection notification, snapshot lifetime, re-entrant reads, the thread contract, shuffle/repeat traversal, skip-while-paused, end-of-track advance, and destruction flushing the session playlist. |
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
| `tests/unit/suno/test_SunoEndpoints.cpp` | Endpoint-map invariants, including the single-`/api` composition rule and allowed URL shapes. |
| `tests/unit/lyrics/test_LyricsPipeline.cpp` | Captured aligned-payload fields and metadata lyrics, invalid-payload rejection, sync line selection and clamped progress, empty load as terminal, downloader signal after an existing file jump, search and SRT/LRC export, lazy bridge updates. |

The aggregate runner invokes **18** suites (`tests/unit/test_main.cpp:27-44`): logger, config parsers, file utils, version, recorder, playlist, preset scanner, Suno endpoints, auth module, stored-credential classification, credential restore worker, library manager, auth coordinator, loopback listener, OAuth login service, clip parser, download queue, and lyrics pipeline.

**Every one of the 19 sources above is invoked.** There are no compiled-but-unrun translation units in the aggregate target, and there are no static-initialization test hooks. That was not always true, and the failure mode is the one this repository keeps re-learning:

- `test_PresetScanner.cpp` was in `CMakeLists.txt` for a long time with no `runTest*` entry point in the runner, so its assertions never executed. `runTestPresetScanner` is now declared at `tests/unit/test_main.cpp:10` and called at `:33`.
- `test_SunoEndpoints.cpp` ran from a static initializer and `std::abort()`ed on failure, which masked the failure behind a process kill instead of the aggregate exit code. `runTestSunoEndpoints` is now declared at `tests/unit/test_main.cpp:11` and called at `:34` like every other suite.

Treat both as closed, and do not reintroduce either shape. A new aggregate source needs **two** edits: the `add_executable` source list *and* a `runTest*` declaration plus a `status |=` call in [test_main.cpp](../../tests/unit/test_main.cpp).

### Standalone unit executables

Six suites build as their own targets and register with CTest individually, because each needs a narrower link line than the aggregate provides:

| Target | Source | Focus |
| --- | --- | --- |
| `test_SunoAudioUploadService` | `tests/unit/suno/test_SunoAudioUploadService.cpp` | Exact captured initialize/finish contract, captured initializer field preservation, malformed-initializer rejection, temporary-URL policy. |
| `test_SunoRequestEpoch` | `tests/unit/suno/test_SunoRequestEpoch.cpp` | Sign-out clears queued and in-flight work, credential replacement aborts in-flight work, invalidated restore wakes readiness waiters, empty reload clears the active credential. |
| `test_SunoExploreService` | `tests/unit/suno/test_SunoExploreService.cpp` | Explore request/cursor contract, nested clip parsing with feed labels preserved, malformed and exhausted responses. |
| `test_SunoNotificationService` | `tests/unit/suno/test_SunoNotificationService.cpp` | Captured notification envelope, malformed entries and nested content, missing-`notifications` rejection, badge and mark-all-read contract. |
| `test_ClerkAuthClient` | `tests/unit/suno/test_ClerkAuthClient.cpp` | `getStyle` envelope parsing, `touch`-style response and client token parsing, cookie normalization with captured headers, exact-selector session preference. |
| `test_AudioAnalyzer` | `tests/unit/audio/test_AudioAnalyzer.cpp` | Interior tone produces the expected bin and level, zero input and reset are deterministic, independent analyzers do not share FFT scratch, concurrent reset/analyze/copy, deterministic silence on invalid arguments. |

### `integration_tests` and `integration_gl_tests`

Both entries run the **same** `integration_tests` binary, built from `tests/integration/CMakeLists.txt:1-4` with two sources: `test_main.cpp` and `test_ProjectMFramebuffer.cpp`. They differ only in environment and in which test functions are selected. When a test function is named on the command line, the runner runs **only** the GL suite (`tests/integration/test_main.cpp:54-57`) — replaying those names against the QML class would fail the run with "Function not found".

#### `integration_tests` — offscreen QML startup and render

CTest registers it with `QT_QPA_PLATFORM=offscreen` (`tests/integration/CMakeLists.txt:31-34`), so it needs no display. `tests/integration/test_main.cpp` is a **real test case**, not an empty harness: `mainQmlLoadsAndRenders()` (`tests/integration/test_main.cpp:18-43`)

1. Constructs a real `vc::AudioEngine` and requires `init()` to succeed.
2. Registers the real QML bridges into a fresh `QQmlApplicationEngine`.
3. Loads `qrc:/qt/qml/ChadVis/src/qml/main.qml` — the same document the app loads.
4. Requires a non-empty root-object list, a `QQuickWindow` root, `isVisible()`, and `isExposed()` within a 3-second retry window.
5. Grabs the window and requires a non-null frame with non-zero width and height.

This entry also runs the GL suite, and on macOS **every one of the 7 GL tests skips with a stated reason** rather than passing: the `offscreen` QPA plugin cannot create an OpenGL context at all (`This plugin does not support createPlatformOpenGLContext!`), which the probe at `tests/integration/test_ProjectMFramebuffer.cpp:53-64` detects. So what a green `integration_tests` proves is that the QML module loads, instantiates, and produces a frame. It is **not** evidence about the visualizer's OpenGL context, and it is not evidence about the recorder.

#### `integration_gl_tests` — native-OpenGL framebuffer suite

The 9th registered entry, added at `tests/integration/CMakeLists.txt:36-48`, runs the same binary under the platform's **native** QPA plugin — `cocoa` on macOS, whatever the session has elsewhere — with no `QT_QPA_PLATFORM` override and a 120-second timeout, selecting exactly the 7 GL test functions:

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

Nine entries: seven in `tests/unit/CMakeLists.txt`, two in `tests/integration/CMakeLists.txt`.

| Suite | Kind | Environment |
| --- | --- | --- |
| `unit_tests` | Aggregate, 18 invoked suites | console |
| `test_SunoAudioUploadService` | Standalone unit | console |
| `test_SunoRequestEpoch` | Standalone unit | console |
| `test_SunoExploreService` | Standalone unit | console |
| `test_SunoNotificationService` | Standalone unit | console |
| `test_ClerkAuthClient` | Standalone unit | console |
| `test_AudioAnalyzer` | Standalone unit | console |
| `integration_tests` | Offscreen QML startup and render; GL tests skip on macOS | `QT_QPA_PLATFORM=offscreen` |
| `integration_gl_tests` | 7 native-OpenGL framebuffer tests, same binary | native plugin (no override) |

## Running One Suite

```bash
./build.sh
ctest --test-dir build/tests -R unit_tests --output-on-failure
./build/tests/unit/unit_tests                                   # aggregate, all 18 suites
./build/tests/unit/test_AudioAnalyzer                            # single standalone target
QT_QPA_PLATFORM=offscreen ./build/tests/integration/integration_tests
ctest --test-dir build/tests -R integration_gl_tests --output-on-failure   # the GL proofs
```

## Scope Notes

- There is no Qt Widgets menu bar and no separate full-window projectM stage in this interface. The projectM surface is the native window embedded in the Video page; see [ARCHITECTURE.md](ARCHITECTURE.md) and [../integration/PROJECTM.md](../integration/PROJECTM.md).
- **Create has no `B-Side Chat` tab.** `src/qml/views/CreateView.qml` declares no tab strip. The B-Side surface is unavailable because its Modal/Orpheus routes remain `[LEAD]` and unverified, matching [../user/USAGE.md](../user/USAGE.md). Earlier revisions of this document claimed otherwise; that was wrong. Do not reintroduce the claim, and do not ship the tab, until a direct capture promotes those routes.
- Generation is intentionally disabled in the Create UI until a supported CAPTCHA token flow exists, so no automated test may assert a successful generation round-trip.
- Any new test must be added to `tests/unit/CMakeLists.txt` with an `add_test()` call. Adding a source file alone produces a binary that CTest never runs, which is the same class of false green as the wrong `--test-dir`. The aggregate target has the sharper version of the same trap: a source in `add_executable` with no `runTest*` declaration and `status |=` call in [test_main.cpp](../../tests/unit/test_main.cpp) compiles, links, and never asserts anything.
