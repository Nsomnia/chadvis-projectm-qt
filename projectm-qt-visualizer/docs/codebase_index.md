# Codebase Index: chadvis-projectm-qt-vidz

## Overview
chadvis-projectm-qt-vidz is a music visualization application using Qt and libprojectM.

## Directory Structure
-   **src/**
    -   **core/**: Core application logic.
        -   `Application`: Main entry point wrapper, lifecycle management.
        -   `Config`: Settings management (`json` based likely).
    -   **visualizer/**: Rendering engine.
        -   `VisualizerWidget`: The Qt OpenGL widget. Handles the render loop and display.
        -   `ProjectMBridge`: Wrapper around `libprojectM`. Handles initialization and preset management.
        -   `RenderTarget`: FBO wrapper for off-screen rendering (used for recording and overlays).
    -   **audio/**: Audio processing.
        -   `AudioEngine`: Connects `AudioAnalyzer` to input sources (PulseAudio/WASAPI/etc).
        -   `AudioAnalyzer`: FFT/Beat detection logic (likely feeds into ProjectM).
        -   `Playlist`: Music library/playlist management.
    -   **util/**: Utility classes.
    -   **ui/**: Qt UI components (Panels, Windows).

## Key Components

### Rendering Pipeline
1.  **Input**: Audio data comes from `AudioEngine` -> `VisualizerWidget::feedAudio` -> `ProjectMBridge`.
2.  **Render Loop**: `VisualizerWidget::onTimer` triggers `paintGL`.
3.  **Frame**: `VisualizerWidget::renderFrame` calls `ProjectMBridge::renderToTarget`.
4.  **Display**: `VisualizerWidget` blits the FBO to the screen (or overlay FBO then screen).

### ProjectM Integration
-   Uses `libprojectM` v4 API.
-   Renders to an FBO (`RenderTarget`) rather than directly to the default framebuffer.
-   Supports Presets, Transitions, and specific config (FPS, Mesh size).
