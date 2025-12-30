# ChadVis TODO - Current Issues

## Critical Problems (Fix Immediately)

### 1. Preset Selection Broken
- **Issue**: Visualizer stays on default preset no matter what
- **Impact**: Core functionality broken
- **Files**: `src/visualizer/ProjectMBridge.cpp`, `src/visualizer/VisualizerWindow.cpp`

### 2. Flickering/Artifacts on Preset Change
- **Issue**: Visual corruption when attempting preset changes
- **Impact**: Poor user experience
- **Files**: `src/visualizer/RenderTarget.cpp`, `src/visualizer/VisualizerWindow.cpp`

### 3. Excessive Debug Logging
- **Issue**: Frame logs every frame (30-60x/sec), memory risk
- **Impact**: Performance degradation, log spam
- **Files**: `src/visualizer/VisualizerWindow.cpp`, `src/audio/AudioEngine.cpp`

---

## Detailed Tracking
See `.agent/TASKS.md` for full details, debug steps, and test commands.

## Quick Commands
```bash
./build.sh build      # Build
./build.sh run        # Run and test
./build.sh test       # Run tests
```

---

**Last Updated**: 2025-12-30
