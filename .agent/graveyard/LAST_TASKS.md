# ChadVis Active Task List
**Last Updated**: 2025-12-30  
**Session**: Active Development

## 🚀 WORKFLOW RULES (NEW)

### Branch Strategy
```bash
# Create feature branch for each fix
git checkout -b fix/preset-selection
git checkout -b fix/excessive-logging
git checkout -b fix/flickering-artifacts

# Work on branch, commit frequently
git add .
git commit -m "fix: description"
git push origin fix/branch-name

# When ready, create PR or merge
git checkout main
git merge fix/branch-name
git push origin main
```

### Commit Frequency
- **Commit after EVERY logical change**
- **Push when compilation succeeds**
- **Don't wait for "perfect" solution**
- **Use descriptive commit messages**

### Undo Strategy
- **Use git revert** instead of chat history
- **Branch isolation** prevents main corruption
- **Frequent commits** = easy rollback

---

## 🔴 CRITICAL - P0

### 1. Preset Selection Not Working
**Status**: ✅ FIXED
**Problem**: Visualizer preset does NOT change regardless of settings

**Symptoms**:
- CLI `--preset` flag ignored
- GUI preset selection ignored  
- Config file preset ignored
- Always shows default projectM visualizer

**Files to Investigate**:
- `src/visualizer/ProjectMBridge.cpp` - preset loading logic
- `src/visualizer/VisualizerWindow.cpp` - preset change handler
- `src/core/Application.cpp` - command line preset handling
- `src/ui/MainWindow.cpp` - GUI preset selection

**Test Commands**:
```bash
# Test 1: CLI
./build.sh run --preset "Aderrasi - Airhandler"

# Test 2: GUI
# Launch app → Visualizer menu → select any preset

# Test 3: Check logs
./build.sh run 2>&1 | grep -i preset
```

**Debug Steps**:
1. Add LOG_INFO in `ProjectMBridge::selectPreset()`
2. Verify `projectm_load_preset_file()` is called
3. Check if GL context is current during load
4. Verify preset name matches available presets

**Branch**: `fix/preset-selection`  
**Fix**: Added `projectm_load_preset_file()` call in `onPresetManagerChanged()`

---

### 3. Excessive Debug Logging (Memory Risk)
**Status**: ✅ FIXED
**Problem**: Frame render debug output is too verbose and frequent

**Symptoms**:
- Logs every frame with "RENDERED FRAME X"
- Called 30-60 times per second
- Memory risk from excessive I/O

**Files to Fix**:
- `src/visualizer/VisualizerWindow.cpp` - render() method
- `src/audio/AudioEngine.cpp` - processAudioBuffer()

**Current Code**:
```cpp
LOG_INFO("RENDERED FRAME {}", frameCount_);  // Too frequent!
```

**Fix**:
- Change to `LOG_TRACE` or remove entirely
- Only log every 10-30 frames if needed
- Or use conditional logging based on debug flag

**Branch**: `fix/excessive-logging`  
**Fix**: Commented out LOG_INFO/LOG_DEBUG in render loop

---

### 2. Flickering/Artifacts on Preset Change Attempts
**Status**: ✅ FIXED
**Problem**: Redraw artifacts/flickering when trying to change preset

**Symptoms**:
- Visual corruption for 1-2 frames
- Happens during preset change attempts
- Looks like FBO not cleared or race condition

**Files to Check**:
- `src/visualizer/ProjectMBridge.cpp` - preset change method
- `src/visualizer/VisualizerWindow.cpp` - render loop
- `src/visualizer/RenderTarget.cpp` - FBO management
- `src/audio/AudioEngine.cpp` - audio feeding during transitions

**Root Cause Theories**:
1. **FBO not cleared** on preset change
2. **Audio continues feeding** during transition
3. **Race condition** between render and preset load
4. **projectM state** not reset properly

**Fix Requirements**:
- Clear FBO immediately on preset change
- Pause audio feeding during transition
- Ensure atomic preset operations

**Branch**: `fix/flickering-artifacts`  
**Fix**: Added presetLoading_ flag, clear FBO during transitions, pause audio feeding

---

## 🟡 HIGH - P1

### 4. Audio-Visual Sync Issues
**Status**: Identified but not fixed
**Priority**: P1

**Current**: `framesToFeed = (sampleRate + fps - 1) / fps`  
**Issue**: Timer-based calculation causes jitter

**Files**:
- `src/visualizer/VisualizerWindow.cpp` - renderFrame()
- `src/audio/AudioEngine.cpp` - processAudioBuffer()

---

### 5. Video Recording Threading
**Status**: Identified but not fixed
**Priority**: P1

**Problem**: `submitVideoFrame()` blocks render thread

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

## 📋 FIX ORDER: 1 → 3 → 2

**Current Focus**: Issue #1 - Preset Selection Not Working

**Next**: Issue #3 - Excessive Logging  
**Then**: Issue #2 - Flickering Artifacts

---

## 🎯 AGENT CHECKLIST

**Before starting**:
- [ ] Create branch: `git checkout -b fix/<issue-name>`
- [ ] Run `./build.sh check-deps`
- [ ] Read issue details

**While working**:
- [ ] ONE change at a time
- [ ] Compile after each change
- [ ] Test with `./build.sh run`
- [ ] Commit frequently with descriptive messages
- [ ] Push branch to remote

**After finishing**:
- [ ] Update this file
- [ ] Run `./build.sh build` (full verification)
- [ ] Commit final changes
- [ ] Push branch
- [ ] Request user review/merge

**Merge to main**:
- [ ] User confirms satisfactory
- [ ] `git checkout main`
- [ ] `git merge fix/<branch-name>`
- [ ] `git push origin main`


---

## 🟢 HIGH PRIORITY - From CLAUDE_NOTES.md

These are critical issues identified by Claude 4.5 Sonnet that need attention:

### 🔴 CRITICAL - Should Fix Immediately

#### C1. Race Condition in VideoRecorder
**Problem**: `processAudioBuffer()` doesn't lock `ffmpegMutex_` but accesses shared state
**Files**: `src/recorder/VideoRecorder.cpp`
**Fix**: Add `std::lock_guard lock(ffmpegMutex_);` to `processAudioBuffer()`

#### C2. Memory Leak in AsyncFrameGrabber
**Problem**: PBO not unbound in error paths
**Files**: `src/recorder/FrameGrabber.cpp`
**Fix**: Ensure `glBindBuffer(GL_PIXEL_PACK_BUFFER, 0)` in all return paths

#### C3. Use-After-Free in PresetBrowser
**Problem**: `itemData(index)` can become invalid during updates
**Files**: `src/ui/PresetBrowser.cpp`
**Fix**: Copy data before potential updates

#### C4. Missing OpenGL Context Check
**Problem**: GL operations without `makeCurrent()`
**Files**: `src/visualizer/VisualizerWidget.cpp`
**Fix**: Add context management

### 🟡 MAJOR - Should Fix Soon

#### M1. Blocking UI Thread
**Problem**: `onUpdateLoop()` runs at 60fps on main thread
**Files**: `src/ui/MainWindow.cpp`
**Fix**: Move to separate thread or use timer

#### M2. No FFmpeg Error Recovery
**Problem**: Recording stops permanently on first error
**Files**: `src/recorder/VideoRecorder.cpp`
**Fix**: Add retry logic or user notification

#### M3. Signal/Slot Deadlock Risk
**Problem**: Callbacks can lock same mutex
**Files**: `src/util/Signal.hpp`
**Fix**: Copy slots before calling

#### M4. Resource Leaks in Error Paths
**Problem**: FFmpeg resources not freed on early returns
**Files**: `src/recorder/VideoRecorder.cpp`
**Fix**: Use RAII wrappers

### 🟢 IMPROVEMENTS - Nice to Have

#### I1. RAII for FFmpeg Resources
**Action**: Create wrapper classes for AVFrame, AVCodecContext, etc.

#### I2. Configuration Validation
**Action**: Add bounds checking for config values

#### I3. Better Error Messages
**Action**: Include context in error strings

#### I4. Telemetry/Metrics
**Action**: Add frame counters, FPS tracking

### 🔧 REFACTORING

#### R1. Extract FFmpeg Wrapper
**Action**: Create `FFmpegEncoder` class

#### R2. Split MainWindow
**Action**: Extract AudioController, VisualizerController, RecordingController

#### R3. Thread Safety in Signal
**Action**: Copy slots before calling to prevent reentrancy issues

---

## 📋 ALL PRIORITIES

### Current Focus: Testing Fixes 1, 2, 3
1. ✅ Preset selection fix (timing, signal order, duplicate names)
2. ✅ Flickering fix (presetLoading_ flag, FBO clearing)
3. ✅ Logging fix (reduced verbosity)

### Status: v1.0-beta Ready + Critical Fixes
- All major preset selection issues resolved
- Command-line `--preset` works correctly
- Next/Previous buttons work with one click
- Handles duplicate preset names gracefully
- Proper GL context management for preset loading
- ✅ C1: Race condition in VideoRecorder - FIXED
- ✅ C2: Memory leak in FrameGrabber - FIXED

### Next: Critical Bug Fixes (C1-C4)
1. ✅ Race condition in VideoRecorder - FIXED
2. ✅ Memory leak in FrameGrabber - FIXED
3. ✅ Use-after-free in PresetBrowser - FIXED
4. ✅ Missing OpenGL context check - FIXED

### All Critical Bugs Fixed! 🎉
All 4 critical bugs identified by Claude 4.5 Sonnet have been resolved.

### Then: Major Issues (M1-M4)
1. 🟡 Blocking UI thread - Investigating (M1)
2. ✅ FFmpeg error recovery - FIXED (M2)
3. ✅ Signal deadlock risk - FIXED (M3)
4. ✅ Resource leaks - FIXED (M4)

### Finally: Improvements & Refactoring
1. ✅ RAII wrappers - DONE (I1)
2. ✅ Validation - DONE (I2)
3. 🟢 Telemetry
4. 🟢 Architecture improvements

### 🚨 NEW BUGS
1. ✅ Preset Shuffling Issue - FIXED. Added manual timer control and UI toggles.
2. ✅ GUI Ghosting on Resize - FIXED. Disabled alpha buffer.
3. ✅ Recording Output Issue - FIXED. Corrected frame submission and audio feeding.
4. ✅ UI Panel Persistence - FIXED. Consolidated Tools dock and added visibility tracking.

