# 🎨 projectM v4: The Visualization Engine

If projectM is the soul of the visualizer, the `ProjectMBridge` is the nervous system.

---

## 🏗️ The Bridge (`vc::ProjectMBridge`)

We wrap the projectM v4 C API in a clean, modern C++ class.

- **Initialization**: Handles the loading of shaders, textures, and the preset library.
- **Rendering**: Called every frame. It feeds the `AudioSpectrum` data to projectM.
- **Spectrum Analysis**: We use **KissFFT** (via CPM) to turn PCM data into frequency bins. We then normalize these so the visuals don't just "flicker" but actually dance.

---

## 📺 OpenGL Integration

- **FBO Management**: We render to a 32-bit float Framebuffer Object.
- **Texture Blitting**: We use a custom shader pass to blit the FBO to the screen, ensuring that alpha transparency and high-dynamic-range colors are preserved across different Linux compositors (KDE, GNOME, Sway).
- **Preset Management**: The `PresetManager` handles the scanning of thousands of `.milk` files without blocking the UI thread.

---

## 🗣️ The Render Room

**Senior Dev:** "We're using projectM v4, which is a massive leap over v3. It uses a modern shader-based pipeline. I've optimized the buffer swaps so we get zero-stutter transitions between presets."

**Richard Stallman:** "Does projectM use any non-free blobs for its shader compilation? And what about the presets? Are they licensed under a Creative Commons Attribution-ShareAlike license? We must ensure the visuals are as free as the code!"

**Linus (The Real One):** "The OpenGL context management in Qt can be a pain, but we've nailed it by using a raw `QWindow`. It's much more stable than `QOpenGLWidget` when you're doing heavy PBO readbacks for recording."

**Linus (LTT):** "Milkdrop presets on a 4K OLED monitor? Absolute peak aesthetic. I could watch this for hours. And the beat sensitivity is so tunable, I can make it look calm for chill beats or absolute chaos for death metal."

---

> "Milkdrop isn't dead. It's just been waiting for C++20." — *Some Guy on Reddit*
