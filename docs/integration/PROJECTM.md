# 🎨 projectM v4: The Visualization Engine

projectM is the soul of the visualizer; the `vc::pm::Bridge` is the nervous system.

The project is C++23. The previous "waiting for C++20" line below was written before the language standard moved; treat any C++20 reference in older docs the same way.

---

## 🏗️ The Bridge (`vc::pm::Bridge`)

We wrap the projectM v4 C API in one RAII C++23 class. The class is `vc::pm::Bridge`, declared in `src/visualizer/projectm/Bridge.hpp:15-17`. There is no `vc::ProjectMBridge`; if you see that name in a doc or a commit message, it is stale.

It is non-copyable and holds the engine, the preset playlist, and a `PresetManager`.

- **Initialization**: `Bridge::init(const ProjectMConfig&)` brings up the projectM handle, loads shaders and textures, and populates the preset library. Failure is a `Result<void>`, not an exception and not a silent no-op.
- **Rendering**: driven per-frame by the renderer, not by the bridge. `VisualizerRenderer` pops interleaved float PCM from the visualizer side of `AudioQueue` and pushes it through `Engine::addPCMDataInterleaved()` into `projectm_pcm_add_float`.
- **Spectrum analysis**: projectM does not receive an `AudioSpectrum`. It receives raw float PCM and runs its own analysis internally, so there is **no client-side bin or level pipeline anywhere in the tree** — not a hidden one, not a disabled one. `src/audio/AudioAnalyzer.{hpp,cpp}`, the class that used to produce an FFT bin array and per-channel levels for the QML side, had no production caller and was deleted on 2026-10-04 (`e058eab`); its only consumer in the whole tree was `tests/unit/audio/test_AudioAnalyzer.cpp`. Its FFT dependency went with it: `cmake/Dependencies.cmake` no longer fetches pffft, `cmake/TargetSetup.cmake` no longer links `PFFFT::PFFFT`, and `-DPFFFT_STATIC_DEFINE` plus the pffft include directory no longer appear on any compile line. `src/audio/LoudnessAnalysis.{hpp,cpp}` (`vc::loudness`, ITU-R BS.1770 LUFS) is measured against exact references and is **also** callerless — it is a loudness measurement, not an FFT, and it feeds nothing yet. So if a document claims PFFFT bins drive the visualizer, it is describing a pipeline that never existed; and if one claims there is a client-side spectrum feeding a UI meter, it is describing a class that was just removed.

---

## 📺 OpenGL Integration

- **FBO management**: the off-screen render target is `GL_RGBA8` — 8-bit unnormalized, `GL_UNSIGNED_BYTE` storage (`src/visualizer/RenderTarget.cpp:64-72`), with `GL_LINEAR` min/mag filtering and `GL_CLAMP_TO_EDGE` wrapping (`:74-76`). It is **not** a 32-bit float framebuffer. Nothing in the capture path is HDR, and any copy claiming float FBO storage is wrong about the format.
- **Texture blitting**: `VisualizerRenderer` binds the render target, calls `projectM_.engine().renderToTarget(renderTarget_)`, and captures from the resulting texture through its two-PBO path. That FBO path is a frame-capture mechanism for recording. It is not how the visualizer is embedded in QML — see [../dev/ARCHITECTURE.md](../dev/ARCHITECTURE.md) for the window and GL-context ownership, which is the load-bearing part.
- **Preset management**: `PresetManager::scan()` delegates to `PresetScanner::scan()`, which walks the directory recursively, parses each preset's author, and sorts by name.

### ⚠️ Open issue: one synchronous preset scan survives, and it is not the one this section used to name

Two of the three call sites named by earlier revisions of this section have been fixed, and the fixes are load-bearing enough that keeping the old text was actively misleading — it described a startup stall that no longer happens. Corrected against source on 2026-10-05:

- **`Application::init()` is async.** It calls `presetManager_->scanAsync(presetDir, true)` (`src/core/Application.cpp:406`) after `setPublishContext(qapp_.get())` (`:402`), so the startup directory walk runs on the manager's worker and publishes back through a queued connection. An earlier revision of this section cited `src/core/Application.cpp:390` as a synchronous scan on the GUI thread; that is no longer what the code does.
- **`PresetBridge::rescan()` is async.** It calls `s_manager->rescanAsync()` (`src/qml_bridge/PresetBridge.cpp:254`), which coalesces an in-flight request into a single follow-up walk and publishes one `listChanged`. An earlier revision called this "a `Q_INVOKABLE` reachable straight from QML" that scans synchronously — the reachability is true, the synchronicity is not.
- **`PresetManager` is no longer thread-free.** It holds a `vc::JThread` scan worker parked on a condition variable, publishes on the thread given to `setPublishContext()`, and never lets the worker touch published state (`src/visualizer/PresetManager.hpp:22-29,254`). `PresetScanner` itself remains synchronous by design: it is a pure walk-and-parse with no threading at all, which is what makes it testable, and it is *called from* the worker rather than being threaded itself. An earlier revision claimed "`PresetScanner` and `PresetManager` contain no threading of any kind — no `QThread`, no `QtConcurrent`, no `std::thread`, no `async`". That is now true of the first and false of the second.

What genuinely remains is `pm::Bridge`: `Bridge::init()` calls `scanPresets(config.presetPath)` (`src/visualizer/projectm/Bridge.cpp:60`), which runs a plain synchronous `presetManager_.scan(path)` (`:98`) to populate the native playlist, and `VisualizerRenderer::initialize()` calls `projectM_.init(pmConfig)` (`src/visualizer/VisualizerRenderer.cpp:35`) — so this one *is* on the GUI thread. It cannot simply be made async without a behaviour change, because `init()` reads `presetManager_.empty()` (`:69`) and selects by name or index (`:71`, `:75`) before returning, and `scanPresets` re-reads the same emptiness to decide whether to walk at all (`:88`). Deferring first-preset selection to the publish callback is the fix; that is a behaviour change, which is why it is a separate item rather than a drive-by. Tracked as an open item in [`TODO.md`](../../TODO.md) rather than silently documented as done.

---

## 🗣️ The Render Room

**Senior Dev:** "We're using projectM v4, which is a massive leap over v3. Modern shader-based pipeline, and we've kept the buffer swaps tight so preset transitions don't stutter."

**Richard Stallman:** "Does projectM drag in any non-free blobs for its shader compilation? And the presets — are they licensed under Creative Commons Attribution-ShareAlike? The visuals must be as free as the code."

**Linus (The Real One):** "Qt OpenGL context management is a pain, but a raw `QWindow` beats `QOpenGLWidget` once you're doing PBO readbacks for recording. Don't let anyone 'simplify' that back."

**Linus (LTT):** "Milkdrop presets on a 4K OLED? Absolute peak aesthetic. And the beat sensitivity is so tunable — calm for chill, absolute chaos for death metal."
