<p align="center">
  <img src="resources/icons/chadvis-projectm-qt.svg" alt="chadvis-projectm-qt logo" width="150"/>
</p>

<h1 align="center">chadvis-projectm-qt: Qt6 projectM v4 Visualizer</h1>

<p align="center">
   <a href="https://github.com/Nsomnia/chadvis-projectm-qt/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Nsomnia/chadvis-projectm-qt/ci.yml?branch=main&label=Build%20Status&style=for-the-badge&logo=github" alt="Build Status"></a>
   <a href="https://github.com/Nsomnia/chadvis-projectm-qt/releases"><img src="https://img.shields.io/github/v/release/Nsomnia/chadvis-projectm-qt?style=for-the-badge&label=Latest%20Release&logo=github" alt="Latest Release"></a>
  <img src="https://img.shields.io/badge/Arch%20Linux-You%20know%20it-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux">
  <img src="https://img.shields.io/badge/C%2B%2B20-Modern%20C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++20">
</p>

<p align="center">
  <i>The ultimate tool for AI music creators who want to automate dope, milkdrop-style music videos without the hassle.</i>
</p>

---

## 🎵 What is chadvis-projectm-qt?

`chadvis-projectm-qt` is a modern Qt6-based visualizer for projectM v4, built with C++20. It provides a clean, efficient interface for rendering projectM presets with real-time audio visualization.

Key features:
*   **projectM v4 Integration:** Full support for the latest projectM API
*   **Qt6 Native:** Modern Qt6 widgets and OpenGL integration
*   **C++20:** Modern C++ with smart pointers, Result types, and clean architecture
*   **Video Recording:** Built-in FFmpeg-based video export
*   **Overlay Engine:** Text overlays and custom graphics
*   **Arch Linux Optimized:** Designed for the modern Linux desktop

### ✨ Key Features (Because we don't do "basic")

*   **Advanced ProjectM Integration:** Harness the full power of ProjectM for dynamic, psychedelic, and customizable visualizations. We didn't reinvent the wheel; we just put some sick rims on it.
*   **Intuitive Qt6 UI:** A user interface so clean, your grandma could probably use it. But she won't, because she's too busy enjoying her vinyl collection.
*   **Real-time Audio Analysis:** Analyzes your audio in real-time, feeding delicious data to ProjectM for visuals that actually react to your tunes, not just some pre-baked nonsense.
*   **Overlay Engine:** Integrate text overlays. Because sometimes, you just need to drop some wisdom or memes on your visual output.
*   **Built-in Video Recorder:** Capture your epic visual journeys directly within the app. Share your vibes with the less fortunate (i.e., non-Arch users).
*   **Configurable to the Max:** Tweak every knob, slide every slider. Because true Chads customize everything.

---

## 🛠️ Get That Vibe: Building from Source

You use Arch, BTW. So building from source is practically foreplay.

### Prerequisites (The non-negotiables)

Before you embark on this glorious journey, ensure you have these bad boys installed:

*   **CMake** (>= 3.20)
*   **Qt6** (Core, Gui, Widgets, Multimedia, OpenGLWidgets, Svg)
*   **spdlog**
*   **fmt**
*   **taglib**
*   **toml++**
*   **GLEW**
*   **GLM**
*   **FFmpeg** (libavcodec, libavformat, libavutil, libswscale, libswresample)
*   **ProjectM-4** (libprojectM-4, libprojectM-4-playlist)

On Arch Linux, you can probably snag most of these with pacman:
```bash
sudo pacman -S cmake qt6-base qt6-multimedia qt6-svg spdlog fmt taglib \
    tomlplusplus glew glm ffmpeg libprojectM
```

### The Sacred Ritual: Compilation

```bash
# Clone this repo (if you haven't already, peasant)
git clone https://github.com/Nsomnia/chadvis-projectm-qt.git
cd chadvis-projectm-qt

# Build the beast (use build.sh - it's Chad-approved)
./build.sh build  # Or: ./build.sh run (builds and runs)

# Or manually
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja -j1  # Use 1 core for potato-safe mode

# Run from build directory
./chadvis-projectm-qt
```

**Note**: The build takes ~5-10 minutes. It's compiling modern C++20 with all the bells and whistles. Be patient.

### 📦 Arch User Repository (AUR)

For the truly enlightened, `chadvis-projectm-qt` might just land in the AUR eventually. Keep an eye out. Until then, compiling is character building.

---

## 🧠 Configuration (For the Control Freaks)

ChadVis uses `toml++` for configuration. Config files are stored in:

### Config Locations (in order of priority):
1. **User config**: `~/.config/chadvis-projectm-qt/config.toml` (or `$XDG_CONFIG_HOME/chadvis-projectm-qt/config.toml`)
2. **System default**: `/usr/share/chadvis-projectm-qt/config/default.toml` (installed by `make install`)
3. **Built-in defaults**: If no file exists, the app uses hardcoded defaults

### Creating Your Config:
```bash
# Copy the default config to your user directory
mkdir -p ~/.config/chadvis-projectm-qt
cp /usr/share/chadvis-projectm-qt/config/default.toml ~/.config/chadvis-projectm-qt/config.toml

# Edit it
nano ~/.config/chadvis-projectm-qt/config.toml
```

### Config Sections:
- `[general]` - Debug mode, logging
- `[audio]` - Audio device, sample rate
- `[visualizer]` - FPS, preset path, beat sensitivity
- `[recording]` - Video format, bitrate, output path
- `[overlay]` - Text overlays, karaoke settings
- `[ui]` - Window size, theme
- `[keyboard]` - Custom key bindings

**Note**: The app will automatically create the config directory if it doesn't exist. Changes are saved when you exit the application.

### Presets Location:
ProjectM presets are searched in this order:
1. `/usr/share/projectM/presets`
2. `/usr/local/share/projectM/presets`
3. `/usr/share/projectm-presets`
4. `~/.local/share/chadvis-projectm-qt/presets` (fallback)

To add custom presets, place them in `~/.local/share/chadvis-projectm-qt/presets/`.

---

## 🤝 Contributing (Show Us Your Code, Chad)

Think you can make ChadVis even more Chad-tier? Prove it. We welcome contributions, but only if they're up to snuff. No junior-dev-level pull requests, please. Read our `CONTRIBUTING.md` (once I write it, give me a minute) for the lowdown.

---

## 📜 License

This project is licensed under the MIT License. See the `LICENSE` file for details.
(Yes, I will add a `LICENSE` file. Don't rush me.)

---

## 🚀 The Future (Because we're always elevating)

We've got big plans, bigger vibes. Expect more features, more optimizations, and probably more "I use Arch, BTW" jokes. Stay tuned, stay Chad.

```
 ░░█░█░▀█▀░█▀▄░█▀▀░█▀▀░█░█░█▀█░█▀▄░░░░░█░█░▀█▀░█▀▄░▀▀█░░
 ░░▀▄▀░░█░░█▀▄░█▀▀░█░░░█▀█░█▀█░█░█░▄▄▄░▀▄▀░░█░░█░█░▄▀░░░
 ░░░▀░░▀▀▀░▀▀░░▀▀▀░▀▀▀░▀░▀░▀░▀░▀▀░░░░░░░▀░░▀▀▀░▀▀░░▀▀▀░░
 ```

## 🔧 Technical Fix: Black Canvas Issue

**Problem**: QOpenGLWidget causes black canvas with projectM v4  
**Solution**: Use QWindow with manual OpenGL context management

The `VisualizerWindow` class (in `src/visualizer/VisualizerWindow.cpp`) replaces `VisualizerWidget`:
- Creates its own `QOpenGLContext`
- Uses `requestUpdate()` for continuous rendering
- Calls `makeCurrent()` before each render
- Calls `swapBuffers()` explicitly

This fixes the issue where projectM would initialize but only render 1 frame.

## ✅ Current Status

**Build Status**: ✅ WORKING - Compiles successfully  
**Runtime Status**: ✅ WORKING - Application runs, initializes OpenGL, loads 8000+ presets  
**Audio**: ⏳ TODO - Needs testing with actual audio playback  
**Video Recording**: ⏳ TODO - Feature present but untested  

**Test Output**:
```
[info] OpenGL: 4.6 (Core Profile) Mesa 25.3.1-arch1.3
[info] Scanned 8386 presets from /usr/share/projectM/presets
[info] ProjectM initialized: 402x248 @ 60 fps, 8386 presets
[info] Visualizer window initialized
```

**Known Issues**:
- Build takes ~5-10 minutes (C++20 with optimizations)
- VDPAU warning if no NVIDIA GPU (harmless)
- Audio playback needs testing
- Video recording needs testing
