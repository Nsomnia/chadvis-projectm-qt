# AGENTS.md - ChadVis projectM Qt Visualizer
**"I use Arch, BTW" - This is a Chad developer's codebase.**

## 🎯 Current Status: v1.0-beta Release

### ✅ All Critical Bugs Fixed

#### Preset Selection System (COMPLETE)
- **Problem**: Presets wouldn't change via CLI or GUI
- **Root Causes**: 
  - Timing: `--preset` processed before presets scanned
  - Signal order: Emitted before listeners connected
  - Duplicates: Same name from package + manual install
- **Solutions**:
  - Pending preset queue in `PresetManager`
  - Signal connection before scanning
  - Duplicate name skipping in next/prev
  - Partial name matching
- **Files**: `PresetManager.cpp`, `ProjectMBridge.cpp`, `VisualizerWindow.cpp`, `VisualizerPanel.cpp`

#### Default Preset Mode (FIXED)
- **Problem**: `--default-preset` flag ignored
- **Root Cause**: `useDefaultPreset` not passed to `ProjectMConfig`
- **Fix**: Added `pmConfig.useDefaultPreset = vizConfig.useDefaultPreset;`
- **Files**: `VisualizerWindow.cpp`

#### Critical Bug Fixes (C1-C4 from Claude Analysis)
1. **C1 - VideoRecorder Race**: Added `ffmpegMutex_` lock in `processAudioBuffer()`
2. **C2 - FrameGrabber Leak**: Error handling for `glMapBuffer()` failures
3. **C3 - PresetBrowser Use-After-Free**: Copy item data before modifications
4. **C4 - RenderTarget Context**: Added `QOpenGLContext::currentContext()` checks

#### Logging Improvements
- Reduced `LOG_INFO` spam (audio buffer logs → `LOG_DEBUG`)
- Preset selection logs now appropriate levels
- Frame render logs removed (too verbose)

## 🧪 Testing & Verification

### Test Commands
```bash
# Build
./build.sh build

# Test preset selection
./build.sh run --preset "Aderrasi - Airhandler"

# Test default preset (no preset, just projectM idle)
./build.sh run --default-preset

# Test with audio
./build.sh run --preset "Aderrasi - Airhandler" ~/Music/song.mp3

# Test next/prev buttons
# Launch app, click ▶ button repeatedly
```

### Expected Behavior
- **CLI preset**: Loads specified preset immediately
- **Default preset**: Shows projectM idle/blank visualizer
- **Next/Prev**: One click = new preset (skips duplicates)
- **Audio**: Visualizer reacts to music in real-time

## 📊 Code Statistics
- **Files**: 6 modified, 158 insertions, 33 deletions
- **Build time**: ~5-10 minutes (C++20 optimized)
- **Presets scanned**: 8980 from `/usr/share/projectM/presets`
- **Binary size**: 1.8M (debug build)

## 🏗️ Architecture Overview

### Component Stack
```
MainWindow (UI)
├── VisualizerPanel (Central widget)
│   └── VisualizerWindow (QWindow, GL context)
│       └── ProjectMBridge (projectM v4 adapter)
│           └── PresetManager (Scanning/Selection)
├── AudioEngine (FFmpeg + Qt Multimedia)
├── VideoRecorder (FFmpeg encoding)
└── Config (TOML-based)
```

### Key Design Decisions
1. **QWindow over QOpenGLWidget**: Fixes black canvas with projectM v4
2. **Manual GL context management**: Required for projectM v4 C API
3. **Signal-based preset loading**: Decouples selection from loading
4. **Pending preset queue**: Handles timing issues gracefully
5. **Duplicate name skipping**: Works with package + manual presets

## 📝 Commit Summary (v1.0-beta)

### Main Branch: `314f08b`
```
fix: Preset selection timing and duplicate name handling
fix: Default preset mode (--default-preset)
fix: Race condition in VideoRecorder
fix: Memory leak in FrameGrabber
fix: Use-after-free in PresetBrowser
fix: Missing OpenGL context checks
fix: Verbose logging reduction
docs: Updated README.md with v1.0-beta status
docs: Updated AGENT.md with architecture
```

## 🎯 Next Priorities (M1-M4 from Claude)

### M1. Blocking UI Thread
**Problem**: `onUpdateLoop()` runs at 60fps on main thread
**Files**: `src/ui/MainWindow.cpp`
**Fix**: Move to separate thread or use timer

### M2. FFmpeg Error Recovery
**Problem**: Recording stops permanently on first error
**Files**: `src/recorder/VideoRecorder.cpp`
**Fix**: Add retry logic or user notification

### M3. Signal/Deadlock Risk
**Problem**: Callbacks can lock same mutex
**Files**: `src/util/Signal.hpp`
**Fix**: Copy slots before calling

### M4. Resource Leaks
**Problem**: FFmpeg resources not freed on early returns
**Files**: `src/recorder/VideoRecorder.cpp`
**Fix**: Use RAII wrappers

## 🔧 Build & Development

### Quick Commands
```bash
# Check deps
./scripts/check_deps.sh

# Build
./build.sh build

# Run
./build.sh run --preset "test"

# Clean
./build.sh clean

# Test
./build.sh test
```

### Git Workflow
```bash
# Create branch
git checkout -b fix/issue-name

# Work and commit
git add -A
git commit -m "fix: Description"
git push -u origin fix/issue-name

# Merge to main
git checkout main
git merge fix/issue-name
git push
```

## 📚 References
- **projectM v4 API**: `/usr/include/projectM-4/`
- **Qt6 Docs**: https://doc.qt.io/qt-6/
- **Repository**: https://github.com/Nsomnia/chadvis-projectm-qt
- **Presets**: `/usr/share/projectM/presets`

## 🎓 Agent Notes

### Session Handoff
- **Status**: v1.0-beta ready for user testing
- **All critical bugs fixed**: C1-C4, preset selection, default mode
- **Next**: User feedback, then M1-M4 improvements

### Important Files
- `.agent/TASKS.md`: Completed task tracking
- `README.md`: User-facing documentation
- `AGENT.md`: This file (agent/developer notes)
- `src/`: All source code

### Testing Checklist
- [x] Preset selection via CLI
- [x] Default preset mode
- [x] Next/Prev buttons (one click)
- [x] Duplicate name handling
- [x] Audio visualization
- [x] Video recording
- [x] Config file loading
- [x] All critical bugs fixed

### User Testing Commands
```bash
# Test 1: Basic preset
./build.sh run --preset "Aderrasi - Airhandler"

# Test 2: Default preset (should be blank/idle)
./build.sh run --default-preset

# Test 3: With audio
./build.sh run --preset "Aderrasi - Airhandler" ~/Music/test.mp3

# Test 4: GUI navigation
# Launch app, click preset browser, double-click presets
# Click next/prev buttons
```

---

**Status**: ✅ v1.0-beta ready for user testing  
**All critical bugs**: ✅ FIXED  
**Audio reactive**: ✅ CONFIRMED WORKING  
**Default preset**: ✅ FIXED  
**Ready for**: 🧪 User shake-down testing
