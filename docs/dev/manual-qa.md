# Manual QA — ChadVis Current Shell (MT-001)

**Test ID:** MT-001 · **Category:** GUI · **Cadence:** per release candidate, and after any change to `src/qml/main.qml`, `src/qml/SettingsWindow.qml`, `src/qml/views/VideoView.qml`, or the credential surfaces.

The automated suite is documented in [TESTING.md](TESTING.md). This file is the part that cannot be automated: the real GUI session, the embedded native projectM window, and the settings round-trip through an actual window close.

## Environment Requirement

Every item below requires a **real GUI session**. None of it can be satisfied by the offscreen harness:

| Coverage | Where |
| --- | --- |
| QML module loads, root window instantiates, produces a non-null frame | `integration_tests` (offscreen) |
| Native projectM `WindowContainer` embedding and GL-context survival across navigation | **real GUI session only** |
| Separate top-level Settings window, page rail, and modal behavior | **real GUI session only** |
| Keyboard/mouse navigation and focus | **real GUI session only** |
| Credential field labels and masking | **real GUI session only** |
| Configuration-backed persistence across an app restart | **real GUI session only** |

The offscreen platform has no presentation path, so a green `integration_tests` run tells you nothing about the Video page. Run this checklist before believing that the shell works.

## Prerequisites

- Build the application with `./build.sh`.
- Run on a desktop with a working OpenGL implementation.
- Use a disposable configuration when checking persistence.
- Do not place a real Suno credential in screenshots, logs, or the test report.

## Checklist

Record `PASS` or `FAIL` per item. A `FAIL` needs a note describing the observed behavior, not a summary of intent.

### MT-001-A — Startup and primary shell

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| A1 | Start `./build/chadvis-projectm-qt`; the main window opens without a crash or hang. | | | |
| A2 | With default settings, the main window opens with **Library** selected. | | | |
| A3 | The persistent rail shows all **seven** destinations: Library, Notifications, Explore, Create, Listen, Video, Settings. | | | |
| A4 | The Library search field, refresh control, and either the clip grid or the unauthenticated empty state render without a QML error. | | | |

### MT-001-B — Navigation

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| B1 | Notifications opens and shows the unread badge and notification list, or a clean unauthenticated state. | | | |
| B2 | Explore opens and shows the feed labels and clip entries. | | | |
| B3 | Create opens the generation form, the runtime model catalog, and the local Create controls. | | | |
| B4 | Create does **not** show a `B-Side Chat` tab. Its Modal/Orpheus routes are still `[LEAD]`, so the tab is intentionally absent. A tab here is a defect, not a feature. | | | |
| B5 | Create does not submit generation; the UI refuses until a supported CAPTCHA token flow exists. | | | |
| B6 | Listen opens the queue, lyrics, and transport controls. | | | |
| B7 | Video opens the native projectM canvas and its FX, Presets, and Record dock. | | | |
| B8 | Navigate Video → Library → Video. The projectM canvas remains available and is not lost or replaced by a blank embedded window. | | | |
| B9 | Repeat B8 in the order Video → Explore → Create → Listen → Video. The canvas still survives. | | | |

B8/B9 are the GL-context lifetime rule: `VideoView` must stay instantiated for the process lifetime, because `WindowContainer` owns the embedded `QWindow` and its OpenGL context. There are no `Loader`s in the shell precisely so nothing unloads it.

### MT-001-C — Settings window

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| C1 | Selecting Settings opens a separate top-level window titled `ChadVis Settings`, not an in-window page swap. | | | |
| C2 | The window is application-modal with respect to the main window. | | | |
| C3 | The page rail lists exactly nine pages: Account, Audio, Visualizer, Recording, Karaoke, Appearance, Performance, Shortcuts, Profiles. | | | |
| C4 | Select every page and confirm each renders without a crash or QML warning. | | | |
| C5 | Closing the Settings window returns focus to the previously active content view. | | | |

### MT-001-D — Account page and manual credentials

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| D1 | The page shows the `Sign in with Google` action and does not launch an unverified browser callback flow. | | | |
| D2 | Collapsed, the disclosure reads `▸ Paste session cookie header instead`. Expanding it flips to `▾ Paste session cookie header instead`. | | | |
| D3 | The expanded section is headed `Session Cookie Header` and contains a field labelled `Complete Cookie request header` with placeholder `Cookie: __client…=…; __client_uat…=…`. | | | |
| D4 | The on-page warning is visible: the `__client…` cookies are the part that matters, and **a lone session JWT is not enough**. | | | |
| D5 | Pasting a bare JWT with no `=` raises the inline warning `This field wants a cookie header, not a bare token.` | | | |
| D6 | Once text is present, the field shows the `Hidden credential — paste remains intact` mask and the value is not readable on screen. | | | |
| D7 | The field accepts focus and editing. With no credential configured, the rest of the shell remains usable. | | | |
| D8 | The panel states the credential is stored in the OS keychain and never written to the config file or logs. Confirm no credential material appears in the log output or in the config file on disk. | | | |

D4 matters: any document or review note that tells a user to paste a bare session token is describing a flow the app cannot use. The panel text is the authority.

### MT-001-E — Settings persistence

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| E1 | In `Settings > Visualizer`, change `FPS Limit` to a disposable non-default value. | | | |
| E2 | Close Settings, then close the main window. | | | |
| E3 | Relaunch and confirm the FPS value was saved. | | | |
| E4 | Confirm the debounce path also persists: change the value, wait roughly two seconds without another change, and kill the process from the terminal. The value should already be on disk — the auto-save is not close-gated. | | | |

### MT-001-F — Process output

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| F1 | The app exits without a crash or hang. | | | |
| F2 | No QML binding, `WindowContainer`, or OpenGL-context error appears during navigation. | | | |
| F3 | No QML warning is emitted that names a property or theme token that does not exist. | | | |

## macOS Credential Note

macOS builds use Security.framework when available. The current `CredentialStore::load()` query sets `kSecUseAuthenticationUIFail`, so an ACL dialog is not part of the intended startup path. If a smoke run stalls before QML, verify that the binary contains the current non-interactive keychain code and check whether the login keychain is locked. With no stored Suno credential, startup should continue in the disconnected state.

## Expected Results

- Library is the default landing surface.
- All seven rail destinations are reachable.
- Settings is a separate paged, application-modal window.
- Browser sign-in does not bypass the desktop-callback gate.
- The credential field is a cookie header, not a bare token, and the UI says so.
- The projectM surface survives ordinary view navigation.
- Closing the main window persists configuration-backed `SettingsBridge` values.

## Report Format

```text
MT-001 Result: [PASS/FAIL]
Failing items: [IDs, or none]
Date: [date]
Configuration used: [default/disposable profile]
GPU/driver: [what the OpenGL implementation reported]
Notes: [any observations]
```

A `PASS` with an empty failing-item list and a recorded GPU/driver is the only form that counts. An untested item is a `FAIL`, not a `PASS` — an unchecked box is not a green build.
