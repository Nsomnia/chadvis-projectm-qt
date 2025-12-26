# projectM Qt Visualizer
**Chad's Audio Visualizer - "I use Arch, BTW"**

A Qt6-based audio visualizer frontend for [projectM](https://github.com/projectm-visualizer/projectm) v4.

**Status: ✅ WORKING** - Renders continuously, captures system audio via PulseAudio/PipeWire

## Features
- ✅ **Continuous Rendering** - QWindow-based OpenGL with manual context management
- ✅ **System Audio Capture** - PulseAudio/PipeWire monitor capture
- ✅ **Preset Support** - 4000+ projectM presets via v4 C API
- ✅ **Modern Qt6** - 60 FPS, double-buffered, Core Profile OpenGL
- ✅ **Keyboard Controls** - N/P (presets), F11 (fullscreen), Ctrl+A (audio), ESC (quit)

## Quick Start
### Dependencies (Arch Linux)
```bash
sudo pacman -S qt6-base qt6-multimedia libprojectm pulseaudio cmake gcc
```

### Build & Run
```bash
./scripts/build.sh      # Ninja build, 1 core (potato-safe)
./scripts/run_app.sh    # Launch visualizer
```

### Manual Build
```bash
mkdir -p build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -- -j1
./projectm-qt-visualizer
```

## Architecture
- **Main Entry**: `src/main_new.cpp` - QWindow-based entry point
- **Visualizer**: `src/ProjectMWindow.cpp` - Manual GL context + requestUpdate() loop
- **projectM Wrapper**: `src/projectm/ProjectMWrapper.cpp` - v4 C API wrapper
- **Audio**: `src/platform/linux/PulseAudioSource.cpp` - Threaded capture

## Key Design Decisions
1. **QWindow over QOpenGLWidget** - Avoids Qt's FBO conflicts with projectM
2. **Manual GL Context** - Full control over OpenGL state
3. **requestUpdate()** - Continuous rendering without throttling
4. **Separate Audio Thread** - Non-blocking capture feeding projectM

## Documentation
- [Architecture](docs/ARCHITECTURE.md)
- [Build Instructions](docs/BUILD_INSTRUCTIONS.md)
- [Coding Standards](docs/CODING_STANDARDS.md)
- [ProjectM v4 API Notes](docs/PROJECTM_V4_API_NOTES.md)

## For AI Agents
See `.agent/` directory for:
- [Prime Directive](.agent/AGENT_PRIME_DIRECTIVE.md)
- [Current State](.agent/CURRENT_STATE.md)
- [Next Tasks](.agent/NEXT_TASKS.md)

## License
MIT - Because Chad developers share their code.
