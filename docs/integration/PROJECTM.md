# 🎨 projectM v4: The Visualization Engine

projectM is the soul of the visualizer; the `vc::pm::Bridge` is the nervous system.

The project is C++23. The previous "waiting for C++20" line below was written before the language standard moved; treat any C++20 reference in older docs the same way.

---

## 🏗️ The Bridge (`vc::pm::Bridge`)

We wrap the projectM v4 C API in one RAII C++23 class. The class is `vc::pm::Bridge`, declared in `src/visualizer/projectm/Bridge.hpp:15-17`. There is no `vc::ProjectMBridge`; if you see that name in a doc or a commit message, it is stale.

It is non-copyable and holds the engine, the preset playlist, and a `PresetManager`.

- **Initialization**: `Bridge::init(const ProjectMConfig&)` brings up the projectM handle, loads shaders and textures, and populates the preset library. Failure is a `Result<void>`, not an exception and not a silent no-op.
- **Rendering**: driven per-frame by the renderer, not by the bridge. `VisualizerRenderer` pops interleaved float PCM from the visualizer side of `AudioQueue` and pushes it through `Engine::addPCMDataInterleaved()` into `projectm_pcm_add_float`.
- **Spectrum analysis**: projectM does not receive an `AudioSpectrum`. It receives raw float PCM and runs its own analysis internally. PFFFT is used in this repository by `AudioAnalyzer` inside `AudioEngine` to produce the QML-side spectrum and level values, and by nothing in the projectM feed path. If a document claims PFFFT bins drive the visualizer, it is describing a pipeline that does not exist.

---

## 📺 OpenGL Integration

- **FBO management**: the off-screen render target is `GL_RGBA8` — 8-bit unnormalized, `GL_UNSIGNED_BYTE` storage, with `GL_LINEAR` min/mag filtering and `GL_CLAMP_TO_EDGE` wrapping (`src/visualizer/RenderTarget.cpp:64-70`). It is **not** a 32-bit float framebuffer. Nothing in the capture path is HDR, and any copy claiming float FBO storage is wrong about the format.
- **Texture blitting**: `VisualizerRenderer` binds the render target, calls `projectM_.engine().renderToTarget(renderTarget_)`, and captures from the resulting texture through its two-PBO path. That FBO path is a frame-capture mechanism for recording. It is not how the visualizer is embedded in QML — see [../dev/ARCHITECTURE.md](../dev/ARCHITECTURE.md) for the window and GL-context ownership, which is the load-bearing part.
- **Preset management**: `PresetManager::scan()` delegates to `PresetScanner::scan()`, which walks the directory recursively, parses each preset's author, and sorts by name.

### ⚠️ Open issue: preset scanning is synchronous

`PresetScanner` and `PresetManager` contain no threading of any kind — no `QThread`, no `QtConcurrent`, no `std::thread`, no `async`. The scan runs synchronously on whichever thread calls it, and the callers are the GUI thread: `Application::init()` scans the configured preset directory during startup (`src/core/Application.cpp:390`), and `PresetBridge::rescan()` is a `Q_INVOKABLE` reachable straight from QML. `Bridge::scanPresets()` also scans from the render side before populating the native playlist.

Consequence: with a large preset library, startup and a manual rescan both stall the UI for the duration of the directory walk and regex parse. The earlier claim that scanning happens "without blocking the UI thread" described a design that was never built. Fixing it means moving the walk to a worker and publishing results back through a queued connection, which is tracked as an open issue in `AGENTS.md`/`TODO.md` rather than silently documented as done.

---

## 🗣️ The Render Room

**Senior Dev:** "We're using projectM v4, which is a massive leap over v3. Modern shader-based pipeline, and we've kept the buffer swaps tight so preset transitions don't stutter."

**Richard Stallman:** "Does projectM drag in any non-free blobs for its shader compilation? And the presets — are they licensed under Creative Commons Attribution-ShareAlike? The visuals must be as free as the code."

**Linus (The Real One):** "Qt OpenGL context management is a pain, but a raw `QWindow` beats `QOpenGLWidget` once you're doing PBO readbacks for recording. Don't let anyone 'simplify' that back."

**Linus (LTT):** "Milkdrop presets on a 4K OLED? Absolute peak aesthetic. And the beat sensitivity is so tunable — calm for chill, absolute chaos for death metal."
