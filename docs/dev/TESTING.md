# Testing ChadVis

The test tree is wired through CMake and registers eight CTest suites: one aggregate `unit_tests` binary, six standalone unit executables, and one `integration_tests` binary that renders the real QML shell offscreen.

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

CTest treats "no tests found" as a success exit code, so the wrong directory is indistinguishable from a real pass unless you read the test count. Use `build/tests`. A correct run prints 8 tests and ends with `100% tests passed, 0 tests failed out of 8`.

`build.sh` performs an incremental build by default and preserves the existing CMake configuration. Use `./build.sh --rebuild` (or `--clean`) for a clean rebuild; `-d`/`--debug` and `-r`/`--release` explicitly select a configuration. The script does not accept a `run` argument.

## Automated Test Inventory

### `unit_tests` (aggregate)

`tests/unit/CMakeLists.txt:1-19` builds a single `unit_tests` executable from **17** sources:

| Source | Coverage |
| --- | --- |
| `tests/unit/test_main.cpp` | Aggregate runner; also runs the endpoint suite at static-initialization time. |
| `tests/unit/core/test_Logger.cpp` | Logger lifecycle coverage. |
| `tests/unit/core/test_ConfigParsers.cpp` | TOML parsing and serialization coverage. |
| `tests/unit/core/test_FileUtils.cpp` | Filename sanitization coverage. |
| `tests/unit/recorder/test_RecordingPipeline.cpp` | Frame submissions reach the encoder, recorder audio queue drained before and after the worker starts, sanitized container path from config. |
| `tests/unit/visualizer/test_PresetScanner.cpp` | Preset scanning source compiled into the target. |
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

The aggregate runner invokes **14** suites — logger, config parsers, file utils, recorder, auth module, stored-credential classification, credential restore worker, library manager, auth coordinator, loopback listener, OAuth login service, clip parser, download queue, and lyrics pipeline (`tests/unit/test_main.cpp:23-36`).

Two compiled sources are not invoked from `main()`:

- `test_PresetScanner.cpp` has no `runTest*` entry point in the aggregate runner.
- `test_SunoEndpoints.cpp` runs during static initialization instead and calls `std::abort()` on failure, because the aggregate target predates that lane and pre-dates its return-status plumbing. A failure there kills the process instead of being masked by the aggregate exit code.

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

### `integration_tests`

`tests/integration/test_main.cpp` is a **real test case**, not an empty harness. It is a `QGuiApplication` offscreen startup-and-render check, `mainQmlLoadsAndRenders()` (`tests/integration/test_main.cpp:12-40`):

1. Constructs a real `vc::AudioEngine` and requires `init()` to succeed.
2. Registers the real QML bridges into a fresh `QQmlApplicationEngine`.
3. Loads `qrc:/qt/qml/ChadVis/src/qml/main.qml` — the same document the app loads.
4. Requires a non-empty root-object list, a `QQuickWindow` root, `isVisible()`, and `isExposed()` within a 3-second retry window.
5. Grabs the window and requires a non-null frame with non-zero width and height.

CTest registers it with `QT_QPA_PLATFORM=offscreen` (`tests/integration/CMakeLists.txt:19-21`), so it needs no display. What it does **not** cover: the native projectM `WindowContainer` surface. The offscreen platform has no real GL presentation path, so a passing run says the QML module loads, instantiates, and produces a frame — it is not evidence about the visualizer's OpenGL context. Video-page behavior, window embedding, and persistence still need the real GUI session in [manual-qa.md](manual-qa.md).

### Registered suites at a glance

| Suite | Kind | Environment |
| --- | --- | --- |
| `unit_tests` | Aggregate, 14 invoked + 2 static-init | console |
| `test_SunoAudioUploadService` | Standalone unit | console |
| `test_SunoRequestEpoch` | Standalone unit | console |
| `test_SunoExploreService` | Standalone unit | console |
| `test_SunoNotificationService` | Standalone unit | console |
| `test_ClerkAuthClient` | Standalone unit | console |
| `test_AudioAnalyzer` | Standalone unit | console |
| `integration_tests` | Offscreen QML startup and render | `QT_QPA_PLATFORM=offscreen` |

## Running One Suite

```bash
./build.sh
ctest --test-dir build/tests -R unit_tests --output-on-failure
./build/tests/unit/unit_tests                                   # aggregate, all invoked suites
./build/tests/unit/test_AudioAnalyzer                            # single standalone target
QT_QPA_PLATFORM=offscreen ./build/tests/integration/integration_tests
```

## Scope Notes

- There is no Qt Widgets menu bar and no separate full-window projectM stage in this interface. The projectM surface is the native window embedded in the Video page; see [ARCHITECTURE.md](ARCHITECTURE.md) and [../integration/PROJECTM.md](../integration/PROJECTM.md).
- **Create has no `B-Side Chat` tab.** `src/qml/views/CreateView.qml` declares no tab strip. The B-Side surface is unavailable because its Modal/Orpheus routes remain `[LEAD]` and unverified, matching [../user/USAGE.md](../user/USAGE.md). Earlier revisions of this document claimed otherwise; that was wrong. Do not reintroduce the claim, and do not ship the tab, until a direct capture promotes those routes.
- Generation is intentionally disabled in the Create UI until a supported CAPTCHA token flow exists, so no automated test may assert a successful generation round-trip.
- Any new test must be added to `tests/unit/CMakeLists.txt` with an `add_test()` call. Adding a source file alone produces a binary that CTest never runs, which is the same class of false green as the wrong `--test-dir`.
