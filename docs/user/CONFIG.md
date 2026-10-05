# ⚙️ Pro-Tier Tweakage: Configuration Reference

ChadVis keeps its settings in a single TOML file: `config.toml`, resolved per-platform (see [File Location](#-file-location)). TOML because we respect your ability to read and write settings without needing 500 curly braces.

This document is rebuilt from [`config/default.toml`](../../config/default.toml), [`src/core/ConfigData.hpp`](../../src/core/ConfigData.hpp), and [`src/core/ConfigParsers.cpp`](../../src/core/ConfigParsers.cpp). If the three ever disagree with this page, the source wins and this page is the bug.

---

## 📍 File Location

The path is **platform-dependent**, resolved by `QStandardPaths::GenericConfigLocation` with `chadvis-projectm-qt` appended (`src/util/FileUtils.cpp:107-110`):

| Platform | Resolved path |
| :--- | :--- |
| Linux | `~/.config/chadvis-projectm-qt/config.toml` (honors `XDG_CONFIG_HOME`) |
| macOS | `~/Library/Preferences/chadvis-projectm-qt/config.toml` |
| Windows | `%APPDATA%\chadvis-projectm-qt\config.toml` |

Verified on macOS: the running app writes `~/Library/Preferences/chadvis-projectm-qt/config.toml`.

The `Generic*` locations are used deliberately — they derive from the environment (`$HOME` / `XDG_*` on Unix, `%APPDATA%` on Windows) and do **not** consult the application name, so they resolve correctly before `QCoreApplication` exists.

**On first run** (`ConfigLoader::loadDefault`, `src/core/ConfigLoader.cpp:163-231`): the directory is created, the config is written with live defaults, and two paths are resolved to real values rather than the template's placeholders:

- `visualizer.preset_path` → the first existing system preset directory, else `<dataDir>/presets`
- `recording.output_directory` → `<dataDir>/recordings`

On Linux only, a packaged install at `/usr/share/chadvis-projectm-qt/config/default.toml` is copied as the template instead (see `cmake/Install.cmake:4`).

---

## 📂 Section Map

**Eleven tables ship in `config/default.toml`** — `[general]`, `[audio]`, `[keyboard]`, `[ui]`, `[visualizer]`, `[overlay]`, `[recording]` with its two nested encoder tables, `[karaoke]`, and `[suno]`. Every one of them is both parsed and serialized; there is no longer a table the template omits. `[karaoke]` in particular was missing from the shipped template for the life of the project before 2026-10-04 and is now present in full, so an earlier revision of this page that called it "template-omitted" is stale.

| Section | Keys | What it drives |
| :--- | :--- | :--- |
| `[general]` | 1 | Logging verbosity |
| `[audio]` | 3 | Output device, buffer size, sample rate |
| `[visualizer]` | 15 | projectM surface, beat reactivity, preset rotation |
| `[recording]` | 8 | Recorder enablement, container, output directory |
| `[recording.video]` | 9 | Video encoder settings (nested) |
| `[recording.audio]` | 4 | Audio encoder settings (nested) |
| `[overlay]` | 2 | Text overlay switch plus the element array |
| `[ui]` | 9 | Theme, panel visibility, sidebar geometry |
| `[keyboard]` | 7 | Global key bindings |
| `[suno]` | 8 written, 10 read | Device identity, download preferences, plus two parse-only legacy secret slots |
| `[karaoke]` | 8 | Lyric overlay typography and colors |

Key naming is strictly **snake_case** — `buffer_size`, `sample_rate`, `beat_sensitivity`, `output_directory`. A camelCase key such as `bufferSize` is not an alias; it is silently ignored and you get the default back.

Colors are strings parsed by `Color::parseHexColor` (`src/util/Color.hpp:99`), which returns a four-verdict result rather than throwing: `#RRGGBB` (alpha forced to opaque) or `#RRGGBBAA`. `Color::fromHex` remains as a documented compatibility shim over it. The app always writes the 8-digit form.

Paths beginning with `~/` are expanded against `QDir::homePath()`, so `USERPROFILE` works on Windows (`src/core/ConfigParsers.cpp:66-77`).

---

## 🔧 Per-Key Reference

### `[general]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `debug` | bool | `false` | Verbose logging. See the warning below — do not delete this key. |

Read by `ConfigLoader::load` (`src/core/ConfigLoader.cpp:115-127`). It uses the null-safe spelling — `(*gen)["debug"].as_boolean()` with a local `false` fallback — so a `[general]` table with `debug` missing reads as `false` rather than dereferencing a null node. Earlier revisions of this page claimed a missing key here was a null dereference and told you never to remove it; that is no longer true, and you can delete the key freely.

### `[audio]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `device` | string | `'default'` | Output device identifier passed to the multimedia backend |
| `buffer_size` | integer | `2048` | Conversion window in samples. The **parser** applies no clamp; the engine clamps it into 4096–16384 via `pcm::conversionWindow` (`src/audio/PcmFormat.hpp:43,49`), so a stored value outside that range is honoured by neither |
| `sample_rate` | integer | `44100` | Requested output rate in Hz. The parser applies no clamp; what the sink actually delivers is observed at runtime and shown in the audio settings page rather than assumed to equal this |

Qt 6 removed `QAudioOutput::setBufferSize` entirely, so `buffer_size` has no device-level home in the framework. It sizes the engine's own sample-conversion window instead. That is a real constraint, not a shortcut: an older revision of this page said "no clamping is applied" and implied the value reached the device, which was never true on Qt 6.

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

`beat_sensitivity` ships at `1.0`, not a beefier number. `preset_path` ships **empty on purpose**: an absent key or an explicit `''` both mean "auto-detect the system presets directory" through `file::presetsDir()` (`src/util/FileUtils.cpp:122-140`, called at `src/core/ConfigParsers.cpp:243-247`). Only a non-empty path is used literally. Auto-detect probes, in order:

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
| `gop_size` | integer | `0` | Keyframe interval. `0` means "let the encoder choose" — the muxer substitutes `fps * 2`. Values above 600 are pulled back to `0`; `0` itself must survive untouched, so it is not clamped against anything |
| `b_frames` | integer | `0` | B-frames allowed in the output, capped at 16. `0` disables them, which is what both codecs this project uses report regardless |

### `[recording.audio]` (nested)

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `codec` | string | `'aac'` | FFmpeg audio encoder |
| `bitrate` | integer | `320` | kbit/s, clamped 64–640 |
| `sample_rate` | integer | `48000` | **The encoder's output rate**, clamped 8000–192000. Not the source file's rate and not `[audio] sample_rate` |
| `channels` | integer | `2` | **The encoder's output channel count**, clamped 1–2. The cap is structural, not a taste decision: an `AudioFrame` is stereo-only, so a wider stream could not reach a consumer intact |

Video codec and CRF live **inside the nested `[recording.video]` table**, not at the top of `[recording]`. The parser only looks at `recording.video` / `recording.audio` (`src/core/ConfigParsers.cpp:294-337`).

`gop_size`, `b_frames`, `sample_rate` and `channels` **are** exposed as TOML keys. An earlier revision of this page said they were "struct fields with fixed values and are **not** exposed as TOML keys", which was true when written and stopped being true the moment the serializer was fixed — they are now in both the parse and serialize tables and in `config/default.toml`. The failure they share is worth naming, because it is the same bug twice: `ConfigParsers::serialize` rebuilds the whole root table from the structs, so **anything absent from a field table does not survive a save**. They were absent for the life of the project, which meant every save silently dropped them — indistinguishable from a default value that did round-trip. That is also why the earlier "not exposed" wording was doubly wrong: it described the intent rather than the effect, and the effect was data loss.

### `[overlay]`

| Key | Type | Default | Meaning |
| :--- | :--- | :--- | :--- |
| `enabled` | bool | `true` | Parsed and round-tripped, but **no consumer reads it** — the visualizer renders overlay elements from its own sources. Setting it `false` will not hide anything. See the caveat below |
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
- `enabled` **is** read on load (`src/core/ConfigParsers.cpp:346`) and survives a save. An earlier revision of this page said it was "written on every save, never read on load. It is not a real switch" — the parsing half of that is no longer true. It is still not a switch that does anything, so the practical advice is unchanged, but the reason is a missing consumer rather than a missing read.

### `[karaoke]`

Parsed, serialized, and shipped. The table is present in full in `config/default.toml`, so nothing needs adding by hand and every value below is already in your file.

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
| `token` | string | *not written* | **Legacy secret, parse-only.** See below — the serializer physically cannot emit it |
| `cookie` | string | *not written* | **Legacy secret, parse-only.** See below — the serializer physically cannot emit it |

All eight keys above it are parsed, serialized, and honored, and all eight are now in the shipped template. `download_path` is additionally settable from the Settings UI and from the `--suno-download-path` CLI flag. An earlier revision of this page said the template "shows only `device_id`"; it has shown the full set for a while, and there is no longer a Suno key missing from `config/default.toml`.

---

## 🔐 Legacy and Migration-Only Fields

Two `[suno]` keys are **not** normal settings:

- `token`
- `cookie`

They exist only so that a pre-keychain config can be upgraded in place, and they are **read-only by construction, not by convention**. `ConfigParsers` keeps them in a separate table, `CHADVIS_SUNO_LEGACY_SECRET_FIELDS`, which `parseSuno` expands and `serialize` does not — so there is no expansion anywhere that reaches `token` or `cookie`, and `Config::save()` cannot write a raw `__client=…` cookie to disk even if a caller asks it to (`src/core/ConfigParsers.cpp:186-203`, `:532-538`). If you find this page and the source disagreeing about whether a secret can be written here, the source is right and this page is the bug.

On startup, `SunoClient` reads a non-empty value from either key — `cookie` wins if both are present (`src/suno/SunoClient.cpp:348-352`) — and migrates it into the OS credential store (`:125-136`). Current credentials live there, not in this file.

**What removal from your file looks like, precisely.** Because `serialize()` rebuilds the entire document, the *first settings save after the upgrade deletes both keys from `config.toml`* whether or not the migration succeeded. `parseSuno` logs a warning whenever it finds one, so the removal is never silent (`src/core/ConfigParsers.cpp:414-418`). Two consequences worth planning for:

- **Migration succeeded** — the credential is in the credential store and you are signed in. The keys disappear and you never notice.
- **Migration failed** (no keychain available — which is still the case on every platform except macOS unless a backend is built) — the credential stays **in memory for this session only** and does **not** survive a restart, so you are signed out after a quit and have to paste it again. The app says so explicitly in an error log rather than pretending the value is still on disk (`src/suno/SunoClient.cpp:138-154`). An earlier revision of this page claimed that a failed migration "keeps the value in memory and logs the failure — it does not silently drop your session" and that "the next successful start empties them anyway". The first half was only ever true until the next save; treat "re-paste your credential" as the expected outcome on a keychain-less platform, not an edge case.

Do not hand-edit these keys expecting them to authenticate anything. The credential in question is a `token_type: "refresh"` Clerk cookie with `Max-Age=31536000` — a **one-year** secret — which is exactly why it does not belong in a world-readable file.

The credential fields in `src/core/ConfigData.hpp:192-199` sit under a comment that describes this parse-only status. `download_path` is a normal setting: it is not migrated, not blanked, and not secret.

---

## ✅ Recorder Settings Round-Trip

**`[recording.video]` and `[recording.audio]` survive a save/load cycle.** The nested form is the *only* supported and *only* written form, and the Settings window round-trips correctly.

The serializer builds one `recordingOut` table and, inside it, shadows the name `out` per group — the `VC_SER_*` macros expand to `out.insert(...)`, so the destination table has to actually be called `out` or the macro writes into something nobody reads. Each of the video and audio groups therefore fills its own `out`, inserts it as a nested `video` / `audio` table, and only then are the plain `[recording]` fields flattened into the same parent (`src/core/ConfigParsers.cpp:462-493`). A config written by a running app therefore looks like the shipped template:

```toml
[recording]
auto_record = false
container = 'mp4'
default_filename = 'chadvis-projectm-qt_{date}_{time}'
enabled = true
output_directory = '~/Videos/ChadVis'
record_entire_song = false
restart_track_on_record = false
stop_at_track_end = false

    [recording.audio]
    bitrate = 320
    channels = 2
    codec = 'aac'
    sample_rate = 48000

    [recording.video]
    b_frames = 0
    codec = 'libx264'
    crf = 18
    fps = 60
    gop_size = 0
    height = 1080
    pixel_format = 'yuv420p'
    preset = 'medium'
    width = 1920
```

Historical note, and it is worth reading because the shape recurred: this used to be a live source bug. The serializer built its two encoder sub-tables in locals named `videoOut`/`audioOut`, which the macros never touched — so both sub-tables were emitted **empty** while every encoder key was flattened into `[recording]`, where `video.codec` and `audio.codec` collided on one key and toml++ kept the first, silently losing the audio codec. On the way back in, the parser read an empty `[recording.video]` and reset every encoder setting to its struct default. **No encoder setting survived a save, ever.** An earlier revision of this page described the fix as "builds a local `videoOut` / `audioOut` table", which repeated the bug's mechanism as if it were the design; if you see that phrasing elsewhere it is wrong. The same class of bug then cost the project `gop_size`, `b_frames`, `sample_rate` and `channels` a second time, for the same reason — a field absent from the table is a field the serializer drops. Both are pinned by `TestConfigLoader::roundTripPreservesEveryField`.

---

## ✅ Verify Your Config

The parser falls back to the compiled-in default for **any** key it cannot read. A typo therefore does not fail loudly — it silently does nothing. Check your work:

1. **Restart the app.** Config is read once at startup; there is no live reload.
2. **Start from the log.** The loader logs the resolved path on success — `Config loaded from: <path>` — and `Config saved to: <path>` after a write. Both come out at `LOG_INFO`/`LOG_DEBUG`, so you need `debug = true` in `[general]` to see the second one.
3. **Diff against the template.** The app rewrites the whole file on save, sorted, with its own defaults. Compare your file with [`config/default.toml`](../../config/default.toml) and ask why a key you never touched changed.
4. **Check for flattened recorder keys.** If you see `crf` or `codec` sitting directly under `[recording]` instead of under `[recording.video]`, that is a file written by a build from before the serializer fix, or a hand-edit. The current writer emits the nested form. See [Recorder Settings Round-Trip](#-recorder-settings-round-trip) above.
5. **Beware the save on close.** `SettingsBridge.save()` runs on window close (`src/qml/main.qml:174`) and `Application` saves on shutdown (`src/core/Application.cpp:537`). Hand edits survive only if they are in a form the app can also write, or if you edit while the app is not running.

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
- **Explicit `--config <file>`**: the error is fatal. `Application::init` logs `Failed to load config` and returns the error, so the app refuses to start (`src/core/Application.cpp:279-283`).

Deleting the file sidesteps both paths: with no file present, `loadDefault` writes a fresh one and moves on.
