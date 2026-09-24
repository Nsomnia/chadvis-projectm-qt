# Testing ChadVis

The test tree is wired through CMake. The GUI smoke checklist below targets the current Suno-first shell; there is no Qt Widgets menu bar or full-window projectM stage in this interface.

## Build and Test Commands

```bash
./build.sh
ctest --test-dir build --output-on-failure
```

`build.sh` performs an incremental build by default and preserves the existing CMake configuration. Use `./build.sh --rebuild` for a clean rebuild; `--debug` and `--release` explicitly select a configuration. The script does not accept a `run` argument.

## Automated Test Inventory

### `unit_tests`

`tests/unit/CMakeLists.txt` builds one aggregate `unit_tests` executable from these current sources:

- `tests/unit/test_main.cpp` — aggregate runner.
- `tests/unit/core/test_Logger.cpp` — logger lifecycle coverage.
- `tests/unit/core/test_ConfigParsers.cpp` — TOML parsing and serialization coverage.
- `tests/unit/core/test_FileUtils.cpp` — filename sanitization coverage.
- `tests/unit/visualizer/test_PresetScanner.cpp` — preset scanning source compiled into the target.
- `tests/unit/suno/test_AuthModule.cpp` — JWT, auth headers, and file-backed credential-store coverage.
- `tests/unit/suno/test_ClipParser.cpp` — captured clip and `feed/v3` envelope parsing coverage.
- `tests/unit/suno/test_DownloadQueue.cpp` — failure classification, backoff, resume, FIFO concurrency, cancellation, and atomic file replacement using injected fake replies.
- `tests/unit/suno/test_SunoEndpoints.cpp` — endpoint-map invariants, including the single-`/api` composition rule and allowed URL shapes. This suite runs during static initialization and aborts the aggregate test process on failure.

The aggregate `tests/unit/test_main.cpp` explicitly invokes the logger, config, file-utils, auth, clip-parser, and download-queue suites. `test_PresetScanner.cpp` is compiled but has no invocation in that aggregate runner.

### `integration_tests`

`tests/integration/test_main.cpp` is currently only a `QCoreApplication` harness and contains no test cases. The CMake target exists and is registered with CTest, but it is not evidence of projectM-render or other integration coverage.

## Manual Test: Current Shell (MT-001)

**Test ID:** MT-001 · **Category:** GUI

### Prerequisites

- Build the application with `./build.sh`.
- Run on a desktop with a working OpenGL implementation.
- Use a disposable configuration when checking persistence.
- Do not place a real Suno credential in screenshots, logs, or the test report.

### Steps

1. Start the binary:

   ```bash
   ./build/chadvis-projectm-qt
   ```

2. Check the primary shell:

   - [ ] With default settings, the main window opens with Library selected.
   - [ ] The persistent rail shows Library, Create, Listen, Video, and Settings.
   - [ ] The Library search, refresh, clip grid or unauthenticated empty state render without a QML error.

3. Check navigation:

   - [ ] Create opens the generation form and its `B-Side Chat` tab.
   - [ ] Listen opens the queue, lyrics, and transport controls.
   - [ ] Video opens the native projectM canvas and its FX, Presets, and Record dock.
   - [ ] Navigate Video → Library → Video. The projectM canvas remains available and is not lost or replaced by a blank embedded window. This is the GL-context lifetime rule: `VideoView` must stay instantiated.

4. Check the standalone Settings window:

   - [ ] Selecting Settings opens a separate top-level window titled `ChadVis Settings`.
   - [ ] The page rail lists Account, Audio, Visualizer, Recording, Karaoke, Appearance, Performance, Shortcuts, and Profiles.
   - [ ] Select every page and confirm that it renders without a crash or QML warning.

5. Check the Account page:

   - [ ] The page shows the `Sign in with Google` action and does not launch an unverified browser callback flow.
   - [ ] `Paste session cookie or token instead` reveals the masked Session Token field.
   - [ ] The manual field accepts focus and editing. With no credential configured, the rest of the shell remains usable.

6. Check settings persistence:

   - [ ] In `Settings > Visualizer`, change `FPS Limit` to a disposable non-default value.
   - [ ] Close Settings, then close the main window.
   - [ ] Relaunch and confirm that the FPS value was saved.

7. Inspect the process output:

   - [ ] The app exits without a crash or hang.
   - [ ] No QML binding, `WindowContainer`, or OpenGL-context error appears during navigation.

### macOS Credential Note

macOS builds use Security.framework when available. The current `CredentialStore::load()` query sets `kSecUseAuthenticationUIFail`, so an ACL dialog is not part of the intended startup path. If a smoke run stalls before QML, verify that the binary contains the current non-interactive keychain code and check whether the login keychain is locked. With no stored Suno credential, startup should continue in the disconnected state.

### Expected Results

- Library is the default landing surface.
- All five rail destinations are reachable.
- Settings is a separate paged window.
- Browser sign-in does not bypass the desktop-callback gate.
- The projectM surface survives ordinary view navigation.
- Closing the main window persists configuration-backed SettingsBridge values.

### Report Format

```text
MT-001 Result: [PASS/FAIL]
Date: [date]
Configuration used: [default/disposable profile]
Notes: [any observations]
```
