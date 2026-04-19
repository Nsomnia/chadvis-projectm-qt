# ⚙️ Pro-Tier Tweakage: Configuration Lore

ChadVis stores its soul in `~/.config/chadvis-projectm-qt/config.toml`. It's a TOML file because we respect your ability to read and write without needing 500 curly braces.

---

## 📂 The Core Structure

```toml
[audio]
device = "default"
bufferSize = 2048
sampleRate = 44100

[visualizer]
presetPath = "/usr/share/projectM/presets"
fps = 60
beatSensitivity = 1.5

[recording]
outputDirectory = "~/Videos/ChadVis"
videoCodec = "libx264"
crf = 18
```

---

## 🧪 Key Tweaks

### 🎸 Audio Buffer (`audio.bufferSize`)
*   **Low (512-1024):** Snappy visuals, might crackle on a potato.
*   **High (4096+):** Smooth as butter, but you might feel a slight delay in beat reactivity.
*   **The Chad Choice:** `2048`. It's the sweet spot for that perfect sync.

### 🎥 Recording Codecs (`recording.videoCodec`)
If you've got the hardware, use it.
*   **NVENC:** `h264_nvenc` or `hevc_nvenc`. (NVIDIA Chads only)
*   **VAAPI:** `h264_vaapi`. (AMD/Intel kings)
*   **The "I have a CPU from 2012":** `libx264`.

### 🎵 Suno Integration (`suno`)
The app handles the login, but you can manually tweak your `downloadPath` here if you want your AI bangers stored on a separate RAID array.

---

## 🗣️ The Bicker Board

**Senior Dev:** "I've implemented auto-save with dirty-tracking. You don't even need to hit a 'save' button in the UI most of the time. It just works."

**Richard Stallman:** "Why is the configuration in a hidden directory? `~/.config` is a modern convention that hides the user's data from them! It should be a plain text file in the home directory named `.chadvisrc`! Transparency is freedom!"

**Linus (LTT):** "Richard, nobody wants their home folder cluttered with 500 dotfiles. `~/.config` is clean. It's tidy. It's... **sponsored by Private Internet Access!** (Okay, I'll stop)."

---

## 🆘 Recovery

If you break your config so hard the app won't launch, just delete it. ChadVis will regenerate a fresh one on the next launch. It's fail-safe. Like a redundant power supply, but for your settings.
