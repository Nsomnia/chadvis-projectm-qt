# 🏛️ Senior Dev Lore: The Architecture of ChadVis

Welcome to the internal sanctum. If you're reading this, you're either looking to contribute or you're trying to figure out why we use a `VisualizerWindow` instead of a simple `QOpenGLWidget`.

---

## 🏗️ High-Level Design: Singleton-Engine-Controller

We follow a strict pattern to ensure that the "Arch BTW" levels of cleanliness are maintained.

### 1. The Supreme Leader: `vc::Application`
Located in `src/core/Application.hpp`.
- Owns all core engines (`AudioEngine`, `OverlayEngine`, `VideoRecorder`).
- Manages the lifecycle. If `Application` dies, everything dies gracefully.
- Accessible via the `APP` macro. No global variables, just a well-managed singleton.

### 2. The Heavy Lifters: Engines
- **`AudioEngine`**: Coordinates playback and analysis. Feeds PCM data to projectM.
- **`OverlayEngine`**: Manages text quads. It's essentially a mini-rendering engine on top of the visualizer.
- **`VideoRecorder`**: An asynchronous beast. It lives in its own thread because blocking the UI for a frame write is a sin.

### 3. The Puppeteers: Controllers
- Bridge the gap between the UI (Qt Widgets) and the Engines.
- They handle the signals and slots so the engines don't have to know about the UI.

---

## 📺 Rendering Pipeline (The 1337 Pass)

We don't just "draw" to the screen. We have a pipeline:

1.  **projectM Render**: Renders the preset to a Framebuffer Object (FBO).
2.  **Capture (Optional)**: If recording, we use **Pixel Buffer Objects (PBOs)** for a double-buffered, zero-copy readback. This keeps the FPS high even while encoding 4K video.
3.  **Overlay Pass**: The `OverlayRenderer` draws text and graphics over the FBO.
4.  **Blit**: The final result is blitted to the `VisualizerWindow`.

---

## 🧵 Threading Model

- **Main Thread**: Qt Event Loop + OpenGL Rendering.
- **Audio Thread**: Managed by FFmpeg/QtMultimedia.
- **Recorder Thread**: A dedicated `std::jthread` for the FFmpeg encoder loop.
- **Network Thread**: `QNetworkAccessManager` handles Suno API calls without stuttering the visuals.

---

## 🗣️ The Engineering Huddle

**Senior Dev:** "I've banned the `new` keyword. If I see a raw pointer owning a resource, I'm rejecting the PR. Use `std::unique_ptr`. Also, `vc::Result<T>` is not a suggestion, it's the law."

**Richard Stallman:** "Why are you using OpenGL? You should be using a purely software-based renderer to ensure that no proprietary microcode in the GPU is being executed! And these 'threads'... they are a chaos that defies the deterministic beauty of a single-threaded Lisp environment!"

**Linus (LTT):** "Richard, if we used a software renderer, we'd be getting 2 frames per *minute*. This architecture is great because it lets me record at 60fps while my CPU is barely breaking 10% usage. That's what I call **performance per watt!**"

---

> "Code like the person who has to maintain it is a violent psychopath who knows where you live. Or worse, someone who uses Windows." — *The Senior Dev*
