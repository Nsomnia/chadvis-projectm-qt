# ⚙️ Pro-Tier Tweakage: Configuration Reference

ChadVis keeps its settings in a single TOML file: `config.toml`, resolved per-platform (see [File Location](#-file-location)). TOML because we respect your ability to read and write settings without needing 500 curly braces.

This document is rebuilt from [`config/default.toml`](../../config/default.toml), [`src/core/ConfigData.hpp`](../../src/core/ConfigData.hpp), and [`src/core/ConfigParsers.cpp`](../../src/core/ConfigParsers.cpp). If the three ever disagree with this page, the source wins and this page is the bug.

---

## 📍 File Location

The path is **platform-dependent**, resolved by `QStandardPaths::GenericConfigLocation` with `chadvis-projectm-qt` appended (`src/util/FileUtils.cpp:69-72`):

| Platform | Resolved path |
| :--- | :--- |
| Linux | `~/.config/chadvis-projectm-qt/config.toml` (honors `XDG_CONFIG_HOME`) |
| macOS | `~/Library/Preferences/chadvis-projectm-qt/config.toml` |
| Windows | `%APPDATA%\chadvis-projectm-qt\config.toml` |

Verified on macOS: the running app writes `~/Library/Preferences/chadvis-projectm-qt/config.toml`.

The `Generic*` locations are used deliberately — they derive from the environment (`$HOME` / `XDG_*` on Unix, `%APPDATA%` on Windows) and do **not** consult the application name, so they resolve correctly before `QCoreApplication` exists.

**On first run** (`ConfigLoader::loadDefault`, `src/core/ConfigLoader.cpp:36-93`): the directory is created, the config is written with live defaults, and two paths are resolved to real values rather than the template's placeholders:

- `visualizer.preset_path` → the first existing system preset directory, else `<dataDir>/presets`
- `recording.output_directory` → `<dataDir>/recordings`

On Linux only, a packaged install at `/usr/share/chadvis-projectm-qt/config/default.toml` is copied as the template instead (see `cmake/Install.cmake:4`).

---

## 📂 Section Map

Ten sections ship in `config/default.toml`. The parser also handles an eleventh, `[karaoke]`, which the template omits but the app writes on every save.

| Section | Keys | What it drives |
| :--- | :--- | :--- |
| `[general]` | 1 | Logging verbosity |
| `[audio]` | 3 | Output device, buffer size, sample rate |
| `[visualizer]` | 15 | projectM surface, beat reactivity, preset rotation |
| `[recording]` | 8 | Recorder enablement, container, output directory |
| `[recording.video]` | 7 | Video encoder settings (nested) |
| `[recording.audio]` | 2 | Audio encoder settings (nested) |
| `[overlay]` | 2 | Text overlay switch plus the element array |
| `[ui]` | 9 | Theme, panel visibility, sidebar geometry |
| `[keyboard]` | 7 | Global key bindings |
| `[suno]` | 10 | Device identity, download preferences, legacy secret slots |
| `[karaoke]` | 8 | Lyric overlay typography and colors (template-omitted) |

Key naming is strictly **snake_case** — `buffer_size`, `sample_rate`, `beat_sensitivity`, `output_directory`. A camelCase key such as `bufferSize` is not an alias; it is silently ignored and you get the default back.

Colors are strings parsed by `Color::fromHex` (`src/util/FileUtils.cpp:425`): `#RRGGBB` (alpha forced to opaque) or `#RRGGBBAA`. The app always writes the 8-digit form.

Paths beginning with `~/` are expanded against `QDir::homePath()`, so `USERPROFILE` works on Windows (`src/core/ConfigParsers.cpp:44-55`).

---

## 🔧 Per-Key Reference

### `[general]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `debug` | bool | `false` | Verbose logging. See the warning below — do not delete this key. |

Read by `ConfigLoader::load` (`src/core/ConfigLoader.cpp:14-16`), which dereferences the node without a null check. If `[general]` exists but `debug` is missing, that is a null dereference, not a fallback. Leave the key in place and change the value.

### `[audio]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `device` | string | `'default'` | Output device identifier passed to the multimedia backend |
| `buffer_size` | integer | `2048` | Output buffer size in frames. No clamping is applied |
| `sample_rate` | integer | `44100` | Output sample rate in Hz. No clamping is applied |

### `[keyboard]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `play_pause` | string | `'Space'` | Toggle playback |
| `next_track` | string | `'N'` | Next playlist item |
| `prev_track` | string | `'P'` | Previous playlist item |
| `toggle_record` | string | `'R'` | Start/stop recording |
| `toggle_fullscreen` | string | `'F'` | Toggle fullscreen |
| `next_preset` | string | `'Right'` | Next projectM preset |
| `prev_preset` | string | `'Left'` | Previous projectM preset |

These are exposed read-only to the UI (notify signals, no write setters in `SettingsBridge`), so they are hand-edited keys.

### `[ui]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `theme` | string | `'dark'` | Theme name |
| `show_playlist` | bool | `true` | Playlist panel visible |
| `show_presets` | bool | `true` | Preset panel visible |
| `show_debug_panel` | bool | `false` | Debug panel visible |
| `visualizer_background` | color | `'#000000FF'` | Visualizer background (maps to `UIConfig::backgroundColor`) |
| `accent_color` | color | `'#00FF88FF'` | Accent color |
| `expanded_panel` | string | `'playback'` | Which sidebar accordion panel is open |
| `sidebar_width` | integer | `280` | Sidebar width in px, clamped to 200–400 |
| `drawer_open` | bool | `false` | Drawer state for narrow layouts |

### `[visualizer]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `width` | integer | `1920` | Render width, clamped 160–7680 |
| `height` | integer | `1080` | Render height, clamped 120–4320 |
| `fps` | integer | `60` | Frame rate cap, clamped 10–240 |
| `beat_sensitivity` | float | `1.0` | Beat reactivity multiplier, clamped 0.1–10.0 |
| `preset_duration` | integer | `30` | Seconds per preset |
| `smooth_preset_duration` | integer | `5` | Blend seconds between presets, clamped 0–30 |
| `hard_cut_sensitivity` | float | `1.0` | Hard-cut detection sensitivity, clamped 0.1–10.0 |
| `aspect_correction` | bool | `true` | Aspect-ratio correction |
| `shuffle_presets` | bool | `true` | Randomize preset order |
| `force_preset` | string | `''` | Pin a specific preset name; empty means no pin |
| `use_default_preset` | bool | `false` | Start from projectM's default preset |
| `mesh_x` | integer | `32` | Mesh grid width, clamped 8–512 |
| `mesh_y` | integer | `24` | Mesh grid height, clamped 8–512 |
| `preset_path` | path | `''` | Preset directory. Empty means auto-detect |
| `texture_paths` | array of strings | `[]` | Extra texture search paths |

`beat_sensitivity` ships at `1.0`, not a beefier number. `preset_path` ships **empty on purpose**: an absent key or an explicit `''` both mean "auto-detect the system presets directory" through `file::presetsDir()` (`src/core/ConfigParsers.cpp:193-200`). Only a non-empty path is used literally. Auto-detect probes, in order:

1. `/usr/share/projectM/presets`
2. `/usr/local/share/projectM/presets`
3. `/opt/homebrew/share/projectM/presets`
4. `%LOCALAPPDATA%\projectM\presets` (Windows only)
5. `/usr/share/projectm-presets`
6. `<dataDir>/presets` — and this is also the final fallback if none of the above exist

### `[recording]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `enabled` | bool | `true` | Master recorder enable |
| `auto_record` | bool | `false` | Start recording automatically |
| `record_entire_song` | bool | `false` | Keep recording past the track end |
| `restart_track_on_record` | bool | `false` | Restart the track when recording starts |
| `stop_at_track_end` | bool | `false` | Stop exactly at track end |
| `default_filename` | string | `'chadvis-projectm-qt_{date}_{time}'` | Filename template; `{date}` and `{time}` are substituted |
| `container` | string | `'mp4'` | Output container |
| `output_directory` | path | `'~/Videos/ChadVis'` | Fallback only. First run rewrites it to `<dataDir>/recordings` |

### `[recording.video]` (nested)

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `codec` | string | `'libx264'` | FFmpeg video encoder |
| `crf` | integer | `18` | Constant rate factor, clamped 0–51 |
| `preset` | string | `'medium'` | x264/x265 speed preset |
| `pixel_format` | string | `'yuv420p'` | Pixel format |
| `width` | integer | `1920` | Clamped 160–7680, then rounded up to an even number |
| `height` | integer | `1080` | Clamped 120–4320, then rounded up to an even number |
| `fps` | integer | `60` | Clamped 10–120 |

### `[recording.audio]` (nested)

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `codec` | string | `'aac'` | FFmpeg audio encoder |
| `bitrate` | integer | `320` | kbit/s, clamped 64–640 |

Video codec and CRF live **inside the nested `[recording.video]` table**, not at the top of `[recording]`. The parser only looks at `recording.video` / `recording.audio` (`src/core/ConfigParsers.cpp:245-267`).

`VideoEncoderConfig::gopSize` and `bFrames`, plus `AudioEncoderConfig::sampleRate` and `channels`, are struct fields with fixed values and are **not** exposed as TOML keys.

### `[overlay]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `enabled` | bool | `true` | Written on every save, never read on load. It is not a real switch |
| `elements` | array of tables | 2 elements in the template | Overlay text elements |

Per-element keys:

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `id` | string | `'element'` | Element identifier |
| `text` | string | `''` | Display text; supports `{artist}` / `{title}` placeholders |
| `anchor` | string | `'left'` | `left`, `center`, or `right` |
| `font_size` | integer | `32` | Font size |
| `color` | color | `'#FFFFFF'` | Text color |
| `opacity` | float | `1.0` | Opacity 0.0–1.0 |
| `animation` | string | `'none'` | Animation name. See the caveat below |
| `animation_speed` | float | `1.0` | Animation rate multiplier |
| `visible` | bool | `true` | Element visibility |
| `position.x` | float | `0.5` | Normalized horizontal position |
| `position.y` | float | `0.5` | Normalized vertical position |

Omitting the whole `[overlay.elements.position]` sub-table leaves the position at `{0.5, 0.5}`. Supplying `[overlay.elements.position]` but omitting `x` or `y` inside it falls back to `0.0` for the missing component, because `Vec2` itself defaults to `{0, 0}` (`src/util/Types.hpp:56-63`). That asymmetry is real; it is not a typo here.

Two caveats worth knowing:

- `animation` is stored as a string, but the QML overlay renderer selects animations by **numeric index** (`src/qml_bridge/OverlayBridge.cpp:57`, `src/qml/components/VisualizerOverlay.qml:59-88`). The string in this file is not currently what drives rendering.
- Overlay edits made in the UI are **not** saved to `config.toml`. `OverlayBridge` persists to a separate `overlays.json` under `QStandardPaths::AppConfigLocation` (`src/qml_bridge/OverlayBridge.cpp:130-135`). Treat `[overlay.elements]` in `config.toml` as the startup seed.

### `[karaoke]`

Parsed and serialized by the app, but **absent from `config/default.toml`**. Add the table by hand if you want to pin these; every value already has a working default.

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `enabled` | bool | `true` | Karaoke lyric overlay enable |
| `font_family` | string | `'Arial'` | Font family |
| `font_size` | integer | `32` | Font size |
| `bold` | bool | `true` | Bold text |
| `y_position` | float | `0.5` | Vertical position, 0.0–1.0 |
| `active_color` | color | `'#FFFF00FF'` | Color of the sung word |
| `inactive_color` | color | `'#FFFFFFFF'` | Color of upcoming words |
| `shadow_color` | color | `'#000000FF'` | Text shadow color |

### `[suno]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `device_id` | string | `''` | Non-secret install identity sent as the `Device-Id` header. A UUID is generated and persisted on first use if left empty (`src/ui/controllers/SunoController.cpp:31-41`) |
| `download_path` | path | `''` | Download directory. Consumed by `SunoDownloader` |
| `download_format` | string | `'mp3'` | `mp3` or `wav`; anything else falls back to `mp3` |
| `auto_download` | bool | `false` | Download automatically |
| `save_lyrics` | bool | `true` | Write lyrics alongside downloads |
| `embed_metadata` | bool | `true` | Embed tags in downloaded files |
| `debug_lyrics` | bool | `false` | Dump lyrics to `debug_lyrics_file` |
| `debug_lyrics_file` | path | `''` | Target path for the lyrics dump |
| `token` | string | `''` | **Legacy secret.** Migration-only — see below |
| `cookie` | string | `''` | **Legacy secret.** Migration-only — see below |

`download_path`, `download_format`, `auto_download`, `save_lyrics`, `embed_metadata`, `debug_lyrics`, and `debug_lyrics_file` are all parsed, serialized, and honored, even though the template omits them. `download_path` is also settable from the Settings UI and from the `--suno-download-path` CLI flag. The template's `[suno]` section shows only `device_id` because that is the only Suno key with a non-trivial default worth printing.

---

## 🔐 Legacy and Migration-Only Fields

Two `[suno]` keys are **not** normal settings. They exist so that a pre-keychain config can be upgraded in place:

- `token`
- `cookie`

On startup, `SunoClient` reads a non-empty value, migrates it into the OS credential store, and then **blanks both fields** (`src/suno/SunoClient.cpp:63-74`, `:309-310`). New secrets are never written to `config.toml`; they go to the OS keychain via `CredentialStore`. Current credentials live there, not in this file.

If migration fails, the app keeps the value in memory and logs the failure — it does not silently drop your session. Do not hand-edit these keys expecting them to authenticate anything; the next successful start empties them anyway.

The credential fields in `src/core/ConfigData.hpp:152-157` sit under a comment that also covers `download_path`, but that comment describes the secret migration only. `download_path` is not migrated and is not blanked.

---

## ⚠️ Known Round-Trip Caveat

Verified against a config written by a running app: **`[recording.video]` and `[recording.audio]` keys do not survive a save/load cycle.**

The serializer expands the video and audio field tables into the wrong table. It builds a local `videoOut` / `audioOut` table, but the expansion macro writes to the identifier `out` (`src/core/ConfigParsers.cpp:168-176`), which in that scope is the enclosing `[recording]` table. The result on disk is:

```toml
[recording]
bitrate = 320
codec = 'libx264'
crf = 18
fps = 60
height = 1080
pixel_format = 'yuv420p'
preset = 'medium'
width = 1920

    [recording.audio]
    [recording.video]
```

The nested tables are written **empty**, and the real values are flattened into `[recording]`, where the parser never looks for them. Meanwhile the parser reads the nested form correctly, and the shipped template uses the nested form.

Practical consequences:

1. Hand-write `[recording.video]` and `[recording.audio]` in the **nested** form. That is the only form the loader reads.
2. If you edit those settings in the Settings window and let the app save, the values will not come back on the next launch.

This is a source bug in `ConfigParsers::serialize`, not a documentation bug, and it is recorded here so this page does not lie to you. As of this writing it is **not** tracked in [`TODO.md`](../../TODO.md) or [`AGENTS.md`](../../AGENTS.md) — if you fix it, that is a genuinely welcome first contribution.

---

## ✅ Verify Your Config

The parser falls back to the compiled-in default for **any** key it cannot read. A typo therefore does not fail loudly — it silently does nothing. Check your work:

1. **Restart the app.** Config is read once at startup; there is no live reload.
2. **Start from the log.** The loader logs the resolved path on success — `Config loaded from: <path>` — and `Config saved to: <path>` after a write. Both come out at `LOG_INFO`/`LOG_DEBUG`, so you need `debug = true` in `[general]` to see the second one.
3. **Diff against the template.** The app rewrites the whole file on save, sorted, with its own defaults. Compare your file with [`config/default.toml`](../../config/default.toml) and ask why a key you never touched changed.
4. **Check for flattened recorder keys.** If you see `crf` or `codec` sitting directly under `[recording]`, you are looking at a broken round-trip, not your edit. See the caveat above.
5. **Beware the save on close.** `SettingsBridge.save()` runs on window close (`src/qml/main.qml:89`) and `Application` saves on shutdown (`src/core/Application.cpp:523`). Hand edits survive only if they are in a form the app can also write, or if you edit while the app is not running.

---

## 🧪 Key Tweaks

### 🎸 Audio Buffer (`audio.buffer_size`)
*   **Low (512-1024):** Snappy visuals, might crackle on a potato.
*   **High (4096+):** Smooth as butter, but you might feel a slight delay in beat reactivity.
*   **The Chad Choice:** `2048`. It's the sweet spot for that perfect sync.

### 🎥 Recording Codecs (`recording.video.codec`)
If you've got the hardware, use it.
*   **NVENC:** `h264_nvenc` or `hevc_nvenc`. (NVIDIA Chads only)
*   **VAAPI:** `h264_vaapi`. (AMD/Intel kings)
*   **The "I have a CPU from 2012":** `libx264`, which is the default.

### 🎵 Beat Reactivity (`visualizer.beat_sensitivity`)
Above `1.0` the visuals hit harder and earlier. Below it, they chill out. The parser clamps the value to 0.1–10.0, so `100` is not a power-up, it is a clamp.

### ☀️ Suno Downloads (`suno.download_path`)
`download_path` is read and honored, and it is also a Settings-window field and a `--suno-download-path` CLI flag. Point it at a RAID array if your AI bangers deserve better storage.

---

## 🗣️ The Bicker Board

**Senior Dev:** "I've implemented auto-save with dirty-tracking. You don't even need to hit a 'save' button in the UI most of the time. It just works."

**Richard Stallman:** "Why is the configuration in a hidden directory? `~/.config` is a modern convention that hides the user's data from them! It should be a plain text file in the home directory named `.chadvisrc`! Transparency is freedom!"

**Linus (LTT):** "Richard, nobody wants their home folder cluttered with 500 dotfiles. `~/.config` is clean. It's tidy. It's... **sponsored by Private Internet Access!** (Okay, I'll stop)."

**Senior Dev:** "Also, on macOS, Qt puts it in `~/Library/Preferences`, so your `~/.config` speech was wrong before it was outdated."

---

## 🆘 Recovery

If you break your config so hard the app won't launch, just delete it. ChadVis will regenerate a fresh one on the next launch. It's fail-safe. Like a redundant power supply, but for your settings.

A malformed TOML file is handled differently depending on how it got loaded:

- **Default path** (`loadDefault`): the parse error is caught and returned as a `Result` (`src/core/ConfigLoader.cpp:30-33`). The app logs the complaint and continues on in-memory defaults, so your settings quietly reset to defaults. Read the log before you assume your tweaks worked.
- **Explicit `--config <file>`**: the error is fatal. `Application::init` logs `Failed to load config` and returns the error, so the app refuses to start (`src/core/Application.cpp:282-286`).

Deleting the file sidesteps both paths: with no file present, `loadDefault` writes a fresh one and moves on.
