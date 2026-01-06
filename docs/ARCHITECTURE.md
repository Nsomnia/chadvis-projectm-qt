# ChadVis Architecture: The 1337 Guide

This document outlines the internal architecture of `chadvis-projectm-qt`. If you're reading this, you're either a contributor, a curious Chad, or an AI agent trying not to segfault.

## 🏗 High-Level Design

The application follows a **Singleton-Engine-Controller** pattern. This ensures clear ownership, prevents resource contention (especially with OpenGL), and makes the codebase "Arch btw" levels of clean.

### 1. The Conductor: `vc::Application`
Located in `src/core/Application.hpp`, this is the entry point and the supreme owner of all core engines.
- **Ownership:** Owns `AudioEngine`, `OverlayEngine`, and `VideoRecorder`.
- **Lifecycle:** Manages initialization and graceful shutdown. No engine is left behind.
- **Singleton:** Accessible via the `APP` macro.

### 2. The Powerhouse: Engines
Engines are low-level components that handle heavy lifting. They are owned by `Application`.
- **`AudioEngine`:** Manages Qt Multimedia and FFmpeg audio playback. Feeds PCM data to ProjectM.
- **`OverlayEngine`:** Renders text and graphics on top of the visualizer using `OverlayRenderer` (OpenGL).
- **`VideoRecorder`:** Asynchronous FFmpeg-based encoder. Captures frames from the OpenGL PBOs.

### 3. The Vision: `vc::VisualizerWindow`
A specialized `QWindow` that manages its own OpenGL context.
- **Why QWindow?** We use `QWindow` instead of `QOpenGLWidget` to gain manual control over the swap chain and context, which is required for stable projectM v4 rendering.
- **PBO Capture:** Uses Pixel Buffer Objects (PBOs) for zero-copy frame capturing during recording.

### 4. The Logic: Controllers
Controllers bridge the gap between the UI and the Engines. They live in `src/ui/controllers/`.
- **`AudioController`:** Binds `PlayerControls` and `PlaylistView` to the `AudioEngine`.
- **`VisualizerController`:** Coordinates between `PresetBrowser` and the ProjectM bridge.
- **`RecordingController`:** Manages the recording state and UI updates.
- **`SunoController`:** Handles API calls to Suno AI, database sync, and lyrics processing.

## 🧵 Threading Model

ChadVis is multi-threaded because we don't like stuttering.
- **Main Thread:** Qt Event Loop and UI rendering.
- **Audio Thread:** Managed by Qt Multimedia/FFmpeg.
- **Recorder Thread:** Dedicated thread for FFmpeg encoding to prevent UI hangs during capture.
- **Network Thread:** `QNetworkAccessManager` handles API calls asynchronously.

## 🎨 Rendering Pipeline

1. **ProjectM:** Renders the psychedelic visuals to a Framebuffer Object (FBO).
2. **Overlay Engine:** Renders text/metadata onto the same FBO or a separate layer.
3. **Blit:** The final FBO is blitted to the screen.
4. **Capture (Optional):** If recording, the FBO is copied to PBOs and sent to the `VideoRecorder`.

## 📂 Directory Structure

- `src/audio`: Audio playback and analysis.
- `src/core`: Application lifecycle and config.
- `src/overlay`: Text and graphics rendering.
- `src/recorder`: Video encoding logic.
- `src/suno`: Suno AI integration.
- `src/ui`: Qt widgets and controllers.
- `src/visualizer`: ProjectM bridge and OpenGL window.

## 🛠 Stability Mandates

- **No Shared Ownership:** Use `std::unique_ptr` for exclusive ownership. Use raw pointers only for non-owning access.
- **No Exceptions:** Use `vc::Result<T>` for error handling.
- **Context is King:** Always ensure the OpenGL context is current before calling any `gl*` functions.
- **Arch BTW:** Always log with pride.
