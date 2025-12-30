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
