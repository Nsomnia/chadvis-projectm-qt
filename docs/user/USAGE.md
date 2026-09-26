# Using ChadVis

ChadVis is a Suno-first desktop client with a separate projectM video workspace. The main window opens on Library by default and keeps a navigation rail for Library, Explore, Notifications, Create, Listen, Video, and Settings.

## Main Window

### Library

Library is the default landing surface for the Suno collection. It provides:

- A searchable clip grid backed by `SunoBridge`.
- Refresh and cursor-based loading for additional pages.
- Clip details with artwork, model, style, prompt, lyrics, and a media-backed play/download action. Captured playable media is preferred; forbidden sentinels and guessed CDN URLs are never used.

When no Suno session is available, the empty state directs you to Settings. The refresh control requests the library again after credentials are supplied.

### Explore and Notifications

Explore reads the captured `POST /api/unified/explore` feed and presents its feed labels and clip entries without constructing media URLs. Notifications reads the captured notification envelope, shows the unread badge, and offers the captured mark-all-read operation. Both surfaces fail closed when no authenticated session is available.

### Create

Create contains the runtime model-catalog generation form, the captured `.m4a` upload flow, and the local Create controls. Generation is intentionally disabled until the captured CAPTCHA token request and durable queued/processing/failed contract are implemented. The former B-Side Chat tab is not available because its Modal/Orpheus routes remain `[LEAD]`.

### Listen

Listen is the playback surface. It contains the current-track summary, queue, lyrics panel, and transport bar. Use the transport bar or queue controls to play, pause, stop, seek, change tracks, adjust volume, or open local audio files. Files can also be added to the queue from the queue toolbar.

The projectM canvas is not part of Listen; it lives in Video.

### Video

Video hosts the native projectM surface. The side dock has three tool tabs:

- **FX**: text overlays managed by `OverlayBridge`.
- **Presets**: projectM preset search, selection, favorites, blacklist, ratings, and random selection through `PresetBridge`.
- **Record**: recording output selection, start/stop, and live recorder statistics through `RecordingBridge`.

Karaoke is also composed into this view. When `Settings > Karaoke` enables it, `KaraokeMaster` displays the synchronized metadata and lyrics supplied by `LyricsBridge` over the Video surface.

The Video view remains alive while the main window is open. Navigating away only hides it, which avoids tearing down and recreating the native visualizer window and its OpenGL context.

## Settings Window

Selecting Settings opens a separate application-modal window. Its page rail contains Account, Audio, Visualizer, Recording, Karaoke, Appearance, Performance, Shortcuts, and Profiles pages.

Configuration-backed `SettingsBridge` values are saved after two seconds of inactivity. The Save action writes the configuration immediately, the window's close action saves again, and closing the main window also calls `SettingsBridge.save()`.

## Suno Account and Sign-In

Open `Settings > Account` to manage the Suno session. The page includes a `Sign in with Google` card, but browser sign-in is not enabled in the current build. The captured Suno/Clerk flow has not demonstrated a desktop callback that ChadVis can safely receive, so the Google action must not start an unverified native sign-in handshake.

Use the manual path instead:

1. Expand `▾ Paste session cookie header instead`.
2. Paste the **complete `Cookie:` request header** — copied straight from a signed-in browser's request to `auth.suno.com/v1/client` — into the **Complete Cookie request header** field.
3. Return to Library and refresh, or perform another authenticated action, so `SunoClient` reloads the stored credential.

A lone session JWT is **not** enough. The `__client…` cookies are the part that actually authenticates the Studio session; supplying only a bearer/session JWT produces a non-working session, and the Account page shows the cookie-header hint when the pasted value contains no `=`. Extra cookies are ignored.

`SettingsBridge` sends this value to `CredentialStore`; it is stored in the OS keychain and is never written to `config.toml` or to logs. When the session becomes valid, the Account page and main-window account chip can display the user name, plan, and credit balance from `SunoAccountManager`.

## Shell Keyboard Controls

Seven bindings come from `config/default.toml` and are read through `SettingsBridge`. Two are hardcoded in QML and are not configurable.

| Action | Default key | Source |
| :--- | :--- | :--- |
| Play/pause | `Space` | `keyboard.play_pause` |
| Next track | `N` | `keyboard.next_track` |
| Previous track | `P` | `keyboard.prev_track` |
| Start/stop recording and reveal Video | `R` | `keyboard.toggle_record` |
| Reveal Video — fullscreen action not wired yet | `F` | `keyboard.toggle_fullscreen` |
| Next projectM preset and reveal Video | `Right` | `keyboard.next_preset` |
| Previous projectM preset and reveal Video | `Left` | `keyboard.prev_preset` |
| Expand/collapse the navigation rail | `M` | Hardcoded in `src/qml/main.qml` |
| Close the Settings window | `Esc` | Hardcoded in `src/qml/SettingsWindow.qml` |

The configured values can be reviewed in `Settings > Shortcuts`. Note that `F` is bound but is still a QML placeholder: it reveals the Video surface and logs a TODO instead of toggling fullscreen, so treat that binding as reserved rather than functional. `M` and `Esc` have no `config.toml` key — editing the config will not change them.
