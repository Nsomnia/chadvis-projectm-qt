# File Structure: chadvis-projectm-qt

This document provides a high-level overview of the project structure, optimized for agentic navigation.

## 📂 Root Directory
- `src/`: Source code (C++20).
- `docs/`: Documentation and guides.
- `tests/`: Unit and integration tests.
- `resources/`: Qt resources (icons, shaders, etc.).
- `.agent/`: Agent-specific context, plans, and status.
- `build.sh`: Main build/test wrapper.

## 📂 `src/` - Source Code
### `audio/`
- `AudioEngine.hpp/cpp`: Main audio playback and analysis coordinator.
- `AudioAnalyzer.hpp/cpp`: FFT analysis and beat detection logic.
- `FFmpegAudioSource.hpp/cpp`: FFmpeg-based audio decoding.
- `Playlist.hpp/cpp`: Audio playlist management.

### `core/`
- `Application.hpp/cpp`: App singleton and component lifecycle.
- `Config.hpp/cpp`: Public API for configuration.
- `ConfigData.hpp`: Configuration data structures.
- `ConfigLoader.hpp/cpp`: File I/O for config.
- `ConfigParsers.hpp/cpp`: TOML parsing logic.
- `Logger.hpp/cpp`: spdlog wrapper.

### `overlay/`
- `OverlayEngine.hpp/cpp`: Text overlay management.
- `OverlayRenderer.hpp/cpp`: OpenGL rendering for overlays.
- `TextElement.hpp/cpp`: Individual text element logic.

### `recorder/`
- `VideoRecorder.hpp`: Forwarding header.
- `VideoRecorderCore.hpp/cpp`: Recording state and public API.
- `VideoRecorderThread.hpp/cpp`: Encoding thread loop.
- `VideoRecorderFFmpeg.hpp/cpp`: Low-level FFmpeg integration.
- `FrameGrabber.hpp/cpp`: OpenGL frame capture logic.

### `suno/`
- `SunoClient.hpp/cpp`: Suno AI API client.
- `SunoDatabase.hpp/cpp`: Local SQLite storage for Suno clips.

### `ui/`
- `MainWindow.hpp/cpp`: Main Qt window and layout.
- `controllers/`: Logic bridging UI and engines.
- `widgets/`: Custom Qt widgets (PlaylistView, PresetBrowser, etc.).

### `visualizer/`
- `VisualizerWindow.hpp/cpp`: Qt window management.
- `VisualizerRenderer.hpp/cpp`: OpenGL rendering and projectM bridge.
- `PresetManager.hpp/cpp`: Public API for presets.
- `PresetScanner.hpp/cpp`: Directory scanning logic.
- `PresetPersistence.hpp/cpp`: Save/load preset state.
- `RenderTarget.hpp/cpp`: FBO management.

## 📂 `docs/` - Documentation
- `AGENT_QUICKSTART.md`: Rapid context for agents.
- `ARCHITECTURE.md`: Detailed system design.
- `AGENT_CONTEXT.md`: High-level project identity and rules.
