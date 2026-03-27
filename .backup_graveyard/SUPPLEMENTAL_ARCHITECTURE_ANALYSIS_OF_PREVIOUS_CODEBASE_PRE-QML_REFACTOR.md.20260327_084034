# CHADVIS ARCHITECTURAL SUPPLEMENT & QML GAP ANALYSIS
@version 3.0.0-supplement
@date 2026-03-27 01:58:00 MDT
@author Autonomous Agent (Opencode)
@status SUPPLEMENTAL_ANALYSIS

### Introduction
Having consumed the absolute monolith of the previous Qt Widgets codebase, the architectural picture is now fully complete. The previous implementation is feature-rich but suffers from typical C++ GUI monolith symptoms: tightly coupled UI/business logic, blocking OpenGL calls, and a massive dependency footprint (QtWebEngine). 

Moving to QML is the correct path for a modern, fluid, 60fps+ interface. However, mapping this massive Widgets state-machine to declarative QML requires careful bridging. Below are new architectural revelations, missing QML features to implement, and severe bottlenecks discovered in the C++ backend that must be gutted during the transition.

---

## 1. QML Refactor: Missing Features & UI Gaps

The following critical user interfaces exist in the Widgets monolith but must be explicitly architected for the QML refactor.

### A. The Comprehensive Settings Window
The `SettingsDialog.cpp` manages over 40 distinct states across 6 tabs. In QML, doing this with individual `PROPERTY()` bindings will choke the QML engine.
*   **Solution**: Implement a `ConfigBridge` C++ singleton. Instead of individual properties, expose the TOML config sections as `QVariantMap` or lightweight `QObject*` wrappers (e.g., `AudioSettingsObj`). 
*   **QML Implementation**: Create a `SettingsWindow.qml` using a `SwipeView` or a custom `ListView` sidebar (mimicking the `SidebarWidget.cpp` style). Use Qt Quick Controls 2 `Switch`, `SpinBox`, and custom `CyanSlider.qml` components bound directly to the `ConfigBridge`.

### B. WYSIWYG Overlay Editor (`OverlayEditor.cpp`)
The previous app allowed users to inject animated, glowing text (watermarks, "Now Playing") directly into the OpenGL pipeline.
*   **QML Implementation**: Do **not** render this in C++ OpenGL anymore! QML is a hardware-accelerated scenegraph. Move the entire `OverlayEngine.cpp` logic into pure QML using `Text` items, `NumberAnimation`, and `MultiEffect` (for glow/shadows). The user edits them via a QML `DragHandler` and `PinchHandler`, saving their coordinates back to the TOML config. This deletes hundreds of lines of legacy C++ GL code.

### C. Suno Library Data Grid (`SunoBrowser.cpp`)
The `QTableWidget` used for the Suno library is a heavy memory hog.
*   **QML Implementation**: Implement a `QAbstractTableModel` in C++ (`SunoLibraryModel.hpp`). Expose this to QML and consume it using a Qt 6 `TableView`. This provides lazy-loading, zero-cost sorting, and buttery smooth scrolling even with 10,000+ downloaded clips.

### D. File Drag & Drop (`PlaylistView.cpp`)
*   **QML Implementation**: Wrap the main QML application window in a `DropArea`. When `onDropped` fires, pass the `drop.urls` array to the C++ `AudioControllerBridge` to parse the files and append them to the underlying `Playlist` model.

---

## 2. Deep Architecture Re-evaluations (The "Gutting" Phase)

### A. The OpenGL vs. Qt6 RHI Collision (CRITICAL)
*   **The Problem**: `VisualizerRenderer.cpp` and `OverlayRenderer.cpp` are making raw OpenGL 3.3 calls (`glGenFramebuffers`, `glDrawArrays`). Qt 6 QML defaults to the **RHI (Rendering Hardware Interface)**, which might be Vulkan, Metal, or Direct3D. If QML is running on Vulkan and your C++ tries to call `glClear`, the application will immediately segfault.
*   **The Fix**: You **must** wrap projectM in a `QQuickFramebufferObject` (QFBO). QFBO provides a safe `QQuickFramebufferObject::Renderer` thread where Qt guarantees an active OpenGL context, allowing projectM to render to an FBO, which QML then safely composites into the modern UI.

### B. The Audio Backend Hydra
*   **The Problem**: The codebase currently juggling `QMediaPlayer` (for standard playback), `SDLMixerAudioSource` (unused but present), and `FFmpegAudioSource` (for custom decoding). This is a maintenance nightmare.
*   **The Fix**: Standardize on **miniaudio** (via CPM) or stick *strictly* to `QMediaPlayer` with a `QAudioSink`. Miniaudio is a single-header library that can decode MP3/FLAC/WAV, handle gapless playlist transitions seamlessly, and natively output the raw float PCM data projectM needs, bypassing Qt Multimedia's clunky `QAudioBufferOutput` entirely.

### C. WebEngine & Stealth.js Bloat
*   **The Problem**: `SunoBrowserWidget.cpp` and `stealth.js` are trying to fight Cloudflare/Clerk bot-protection by injecting mock navigator properties. This requires shipping the 150MB Chromium binary (`QtWebEngine`).
*   **The Fix**: Nuke `QtWebEngine` from orbit. The codebase already has `SystemBrowserAuth.cpp` which opens the user's *actual* Chrome/Firefox and catches the callback via a local TCP socket (`QTcpServer`). This is 100% immune to bot-detection, zero bloat, and standard OAuth practice.

### D. PBO Frame Grabbing vs. Hardware Encoding
*   **The Problem**: `AsyncFrameGrabber.cpp` uses Pixel Buffer Objects (PBOs) to pull frames from the GPU to CPU RAM (`glMapBuffer`), only for `VideoRecorderFFmpeg.cpp` to push them *back* to the GPU if NVENC/VAAPI is selected.
*   **The Fix**: In the QML refactor, intercept the QFBO texture directly. Use OpenGL-to-CUDA/VAAPI interop to pass the texture handle directly to FFmpeg's hardware frames context. Zero-copy. 

---

## 3. SUPPLEMENTAL TODO.md ADDITIONS

*(Agent Directive: Append these items to the MASTER TODO.md from the previous loop, maintaining the strict ranking system).*

### [P0] QML / C++ Rendering Integration
- [ ] **Implement QQuickFramebufferObject:** Wrap the `VisualizerRenderer.cpp` logic inside a `QQuickFramebufferObject` to ensure QML/RHI compatibility and prevent context-collision segfaults.
- [ ] **Migrate Overlays to QML:** Delete `OverlayRenderer.cpp` and `OverlayEngine.cpp`. Reimplement watermarks, karaoke text, and animations using QtQuick `Text`, `MultiEffect`, and `NumberAnimation` layered *on top* of the QFBO.

### [P1] UI Feature Parity (QML)
- [ ] **Build QML Settings Window:** Create a comprehensive `Settings.qml` with a sidebar layout. Expose `ConfigData.hpp` to QML via a unified `Q_PROPERTY` wrapper.
- [ ] **Build Suno Library Table:** Implement `SunoLibraryModel : public QAbstractTableModel` in C++ to efficiently feed thousands of downloaded tracks to a QML `TableView`.
- [ ] **Implement Drag & Drop:** Add a global QML `DropArea` to catch incoming audio files and route them to the C++ playlist manager.

### [P1] Audio Engine Consolidation
- [ ] **Evaluate Miniaudio / Remove Dead Code:** Remove `SDLMixerAudioSource.cpp` and `FFmpegAudioSource.cpp`. Evaluate replacing `QMediaPlayer` entirely with `miniaudio` for native gapless playback and direct PCM float extraction without thread-locking.

### [P2] Auth & Dependency Diet
- [ ] **Purge QtWebEngine:** Delete `SunoBrowserWidget.cpp`, `SunoCookieDialog.cpp`, `stealth.js`, and `SunoPersistentAuth.cpp`. 
- [ ] **Promote SystemBrowserAuth:** Make `SystemBrowserAuth.cpp` the sole authentication mechanism, removing the massive `Qt6::WebEngineWidgets` dependency from `CMakeLists.txt`.

###[P3] FFmpeg Pipeline Optimization
- [ ] **Zero-Copy Pipeline Research:** Research passing the QFBO OpenGL Texture ID directly to FFmpeg `hw_frames_ctx` for NVENC/VAAPI, bypassing `AsyncFrameGrabber.cpp`'s CPU memory mapping.