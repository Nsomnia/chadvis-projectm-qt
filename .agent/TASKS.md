# ChadVis Active Task List
**Last Updated**: 2025-12-30  
**Session**: Active Development

## 🔴 CRITICAL - P0

### 1. Preset Stuck on Default Visualizer
**Problem**: No matter what settings user picks, system stays on default projectM visualizer

**Symptoms**:
- `--preset "Name"` ignored
- GUI preset selection doesn't work
- Config file preset ignored

**Files to Check**:
- `src/visualizer/ProjectMBridge.cpp` - preset loading
- `src/visualizer/VisualizerWindow.cpp` - preset change handling
- `src/core/Application.cpp` - command line handling

**Test**:
```bash
./build.sh run --preset "Aderrasi - Airhandler"
# Should show that preset, but shows default
```

---

### 2. Visual Artifacts on Rapid Preset Changes
**Problem**: Rapid preset changes cause 1-2 frames of artifacts/ghosting

**Symptoms**:
- Ghosting when switching presets quickly
- Lasts 1-2 frames
- Race condition suspected

**Files**:
- `src/visualizer/ProjectMBridge.cpp`
- `src/visualizer/VisualizerWindow.cpp`
- `src/visualizer/RenderTarget.cpp`

**Root Cause Theories**:
1. FBO not cleared between loads
2. Audio feeding during transition
3. Race condition in render thread

---

## 🟡 HIGH - P1

### 3. Audio-Visual Sync Issues
**Current**: `framesToFeed = (sampleRate + fps - 1) / fps`  
**Issue**: Timer-based, causes jitter

**Fix**: Feed all available audio, let projectM handle it

**Files**:
- `src/visualizer/VisualizerWindow.cpp` - renderFrame()
- `src/audio/AudioEngine.cpp` - processAudioBuffer()

---

### 4. Video Recording Threading
**Problem**: `submitVideoFrame()` blocks render thread

**Current**: `processVideoFrame(frame)` on render thread  
**Need**: PBO async + FrameGrabber queue

**Files**:
- `src/recorder/VideoRecorder.cpp`
- `src/recorder/FrameGrabber.cpp`

---

## ✅ COMPLETED

- Zero-allocation audio hot path (scratch buffer)
- Atomic config saving
- Wayland/Hyprland hang fix (RenderGuard + DontUseNativeDialog)
- Diagnostic timer fix (only warn during playback)
- Unified build system (single build.sh)
- Repository rename (chadvis-projectm-qt)

---

## 📋 NEXT ACTIONS

1. **Fix preset selection bug** (P0)
2. **Fix rapid change artifacts** (P0)
3. **Improve audio sync** (P1)
4. **Implement PBO recording** (P1)

---

## 🎯 AGENT REMINDERS

**Before starting work**:
- Check this file
- Run `./build.sh check-deps`
- Read the specific issue details

**While working**:
- ONE change at a time
- Compile after each change
- Test with `./build.sh run`
- Update this file

**After finishing**:
- Update this file
- Run `./build.sh build`
- Commit with proper message
