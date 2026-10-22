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

### MT-001-D2 — Browser session import (T0189-adjacent, added with T0199)

A **headless run proves nothing about any of these** — the whole feature is a
consent prompt and a read of the user's own browser, so every row needs the
person whose machine it is.

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| D2.0 | **Run the fresh build only**: `build-fast/chadvis-projectm-qt.app` (or relaunch via `./build.sh --fast`). The `~/Documents/chadvis-projectm-qt-*` copies are stale binaries that predate the 2026-10-15 decrypt fixes — a report from one of them is a report about the bug, not about the tree. | | | |
| D2.1 | Signed in to suno.com in the default browser, open `Settings > Account`. The card shows **Already signed in? Import the session** below the Google button, enabled. | | | |
| D2.2 | Press it. macOS shows a consent dialog naming the browser's *Safe Storage* item (e.g. "Brave Safe Storage"). Allow it. | | | If no prompt appears and the import succeeds, the grant was remembered from an earlier run — note that, do not fail it. The read window is now 30 s; take your time. |
| D2.3 | The Account page transitions to the signed-in state: user name, plan, and credits appear, and the main-window account chip updates. A short **"Imported. Verifying your session"** line may appear on the card first. | | | This is the end-to-end proof: store → decrypt → header → `setCookie` → bearer fetch → account. |
| D2.4 | Quit and relaunch. Still signed in — the credential came from the browser's `__client` (≈1 year) and was persisted through `CredentialStore`. | | | |
| D2.5 | Read the log output for the run. **No cookie value, no bearer, and no filesystem path appears** — the import log line names neither the browser nor the profile. | | | A single `session imported from a browser` line with no identifying detail is the intended ceiling. |
| D2.6 | Decline the consent dialog. The Account page shows the reason ("keychain item not available"), and nothing is imported. The app stays usable. | | | |
| D2.7 | With **no** Suno session in any browser (e.g. signed out everywhere), press the import button. The error names the browsers that were searched and tells the user to sign in once at suno.com. It does not crash, and it does not open a browser unprompted. | | | |
| D2.8 | Multiple browsers installed, only one signed in. The import uses the signed-in one, and the error path does not read the others' Keychain items — only one consent prompt appears. | | | |
| D2.9 | Sign out of suno.com in the browser (the prepared state: `__client_uat = 0`, no `__client`), then press Import. The card must say **"<Browser> says you are signed out of suno.com. Sign in there first, then press Import once more."** — not "No Suno session was found". | | | d629fbf: the uat-only header is refused by the durability bar before it can be stored as a phantom success. |
| D2.10 | Press **Sign in with Google**, then click the card's **Never mind — back to sign-in** button (or close the browser and change your mind). The card returns to the sign-in page without restarting the app, and a fresh Sign in click opens the browser again. | | | Second user report of the session; `cancelGoogleSignIn()` is a pure state reset. |
| D2.11 | Press Import while a consent prompt is pending but unanswered for >10 s (let it sit ~20 s, then Allow). The import still succeeds — the wait was extended from 10 s to 30 s so `security` is not killed mid-prompt. | | | The likeliest cause of the signed-in "button did not work" report. |
| D2.12 | Signed in to suno.com in the default browser, press Import. Sign-in succeeds exactly as in D2.3 — the bearer now comes from `POST .../tokens` first (touch is the fallback), so a regression here reads as a total sign-in failure, not a subtle one. | | | If sign-in fails after this change, check the log for the "falling back to touch" line: its presence means tokens failed and touch was tried; its absence means tokens succeeded or the failure was a rejected credential. |
| D2.13 | Sign in via suno.ai instead of suno.com (type suno.ai in the browser; it redirects to suno.com), then press Import. The import finds the session — the harvest reads suno.ai host rows as well as suno.com ones. | | | The Clerk cookies themselves live under suno.com; this row proves the harvest does not miss a session just because the browser's first landing was the alias domain. |
| D2.14 | While signed in (via Import), press Import again with the same browser session. The card re-verifies and stays signed in — no hang, no duplicate sign-in, no "Imported. Verifying" left standing. | | | The second press used to no-op on an unchanged cookie header (`setCookie` early return); `reverifyStoredCredential()` forces a fresh exchange, and the in-flight guard keeps it to one. |
| D2.15 | With a signed-out browser store (D2.9 state), press Import and let it fail, then press **Sign in with Google** and cancel, then press Import again. Each attempt shows the current verdict — no stale "Imported. Verifying", no stale error from the previous attempt. | | | Sign-out resets the coordinator state; a failed verification converts the import wait into a named error instead of stranding the card. |

### MT-001-D3 — Encrypted playback and the local decrypt (T0317)

Every layer under this gesture is verified in code and against the live server; the
gesture itself is not, because it needs a signed-in account and a screen. ctest
proves nothing here.

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| D3.1 | Signed in, press Play on a clip whose media is `m4a-opus`/`progressive` (every synced clip). The clip downloads, the saved file is an `.m4a`, and audio starts. | | | The captured form is ciphertext until the content key arrives. |
| D3.2 | Play a clip Suno marks as locked for its download product (the library's own clips are mostly in this state). It still plays. The log contains one line beginning `SunoDownloader: clip … is locked in Suno's download product` and the transfer proceeds. | | | This is the T0317 regression guard: the lock is reported, never enforced. |
| D3.3 | Open the saved file outside the app (`afplay <file>`, or `afinfo <file>`). It plays, and reports Opus 48 kHz stereo. | | | macOS AudioToolbox reads Opus-in-MP4; a player that refuses it is a separate, named limitation. |
| D3.4 | Press Play on the same clip again within the hour (delete or keep the file — both paths are valid). No second license request is made: the log says `reusing the content key`. | | | `CONTENT_KEY_CACHE_TIME` parity with the site: one `/api/mango/rights` call per clip per hour. |
| D3.5 | Read the log for the run. No cookie, no bearer, and no complete media URL appears. | | | Neither the downloader nor the queue logs a URL at all, and the rights response is never logged. A host or path appearing here would be a new leak. |
| D3.6 | Play a clip whose media this client will not fetch, or let the server refuse one (403/401). The Library shows the refusal sentence; the card does not sit on "Saving" forever. | | | Fail-closed paths: `noUsableMediaMessage`, `clipSaveRefused`, `finalizeFailed`. |

### MT-001-E — Settings persistence

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| E1 | In `Settings > Visualizer`, change `FPS Limit` to a disposable non-default value. | | | |
| E2 | Close Settings, then close the main window. | | | |
| E3 | Relaunch and confirm the FPS value was saved. | | | |
| E4 | Confirm the debounce path also persists: change the value, wait roughly two seconds without another change, and kill the process from the terminal. The value should already be on disk — the auto-save is not close-gated. |

### MT-001-G — Themes and colour

**Evidence so far (2026-10-15, not a human pass).** G1–G4 have partial
evidence: the binary was launched with `--theme gruvbox` and the window was
read from a screen capture — gruvbox's warm ground, cream text and `#83A598`
accent render across the whole window, so CLI → session override → engine →
pixels works. `config.toml`'s md5 was unchanged across that launch, which is the
T0192 guarantee, and `TestQmlStartup::settingsWindowOpensOnNavigate` passes,
which means the panel, the picker and the `Popup` inside it all instantiate.
**Everything below G5 is untested by anyone or anything.** In particular no
screenshot yet exists of the picker open, of the theme selector, or of a colour
being dragged — and the four palettes were contrast-*measured*, never seen.

Added with the theme engine. **A headless run is not evidence for anything on
this list** — these all depend on what the eye sees, so `PASS` here has to come
from a person looking at the window.

| ID | Step | Pass | Fail | Notes |
| --- | --- | --- | --- | --- |
| G1 | Open `Settings > Appearance`. Four themes are listed in the dropdown. | | | |
| G2 | Select **Gruvbox** without pressing Apply. The **whole window** changes, not only the Settings panel — rail, transport, status bar, buttons, borders, and the glow on the focused control. | | | |
| G3 | Select **Nord**, then **ChadVis Dark**, then **Material**. Each repaints completely. No token is left over from the previous theme — look especially at borders, hover states, and the sidebar's selected-item fill. | | | |
| G4 | Confirm the same for **Material → Gruvbox**: the glass borders and the focus glow follow the accent. They used to be hardcoded cyan and were the reason a colour change looked half-applied. | | | |
| G5 | Press **Apply**, close Settings, relaunch. The window opens on the applied theme. | | | |
| G6 | Select Gruvbox, do **not** press Apply, then select **ChadVis Dark** instead. Quit Settings via the window close button rather than applying. The persisted theme is unchanged — a preview is not a choice. | | | |
| G7 | Click the **Accent** swatch. A picker opens with a saturation/value field, a hue strip, and an alpha strip over a checkerboard. | | | |
| G8 | Drag in the SV field and along the hue strip. The window previews the colour live as you drag. Drag the alpha strip to about half; the background of the strip shows through the checkerboard and the window itself becomes translucent. | | | |
| G9 | Press **Cancel**. The colour returns to what it was before the drag. Cancel is the reason colour edits need no Apply button. | | | |
| G10 | Pick a colour and **Done**. The dropdown now reads **Custom**. The hex field beside the swatch shows the value. | | | |
| G11 | Type a hex directly into the Accent field (`#FFAA00`), press Enter. The window updates and the field shows a normalised `#FFAA00FF`. | | | |
| G12 | Type `#12345` — an invalid length — and press Enter. The field snaps back to the previous valid value rather than turning the window into an undefined colour. | | | |
| G13 | Under **Gruvbox**, pick a new accent. Gruvbox's text colours and borders stay gruvbox's; only the accent family changes. | | | |
| G14 | Read `config.toml`: `theme` is `custom`, and `accent_color` / `visualizer_background` hold the picked values as `#RRGGBBAA`. | | | |
| G15 | Relaunch. The window opens on the custom theme — this is the exact regression that produced the uniform transparent-blue window on 12 Oct. | | | |
| G16 | `Settings > Appearance > Reset to Default`, then **Apply**. The window returns to Material and `config.toml` says `theme = 'material'`. | | | |
| G17 | Launch once with `./build/chadvis-projectm-qt --theme nord`. The window opens on Nord. Quit and relaunch **without** the flag: the window opens on the previously saved theme, not Nord. | | | |
| G18 | Set an unknown id by hand (`theme = 'dracula'`) in `config.toml` and launch. The app starts on a valid theme, logs what it fell back to, and **leaves `dracula` in the file**. | | | |
| G19 | Read every secondary text label, the disabled labels, and the status bar against their backgrounds. Nothing should be unreadable grey-on-grey. `textDisabled` shipped at 1.95–2.77:1 on the old theme and the four palettes here are new colour guesses — T0191 tracks the measurement this depends on. | | | |
| G20 | Resize the window from ~400 px to full width. Nav rail collapses and expands, and every threshold crossing re-reads the theme rather than reverting to the built-in defaults. | | | |

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
