# AGENTS.md - projectM Qt Visualizer
**"I use Arch, BTW" - This is a Chad developer's codebase.**

## Quick Start for Agents

### Build Commands
```bash
# Check dependencies
./scripts/check_deps.sh

# Debug build (uses ninja with -j1 for potato-safe builds)
./build.sh build

# Release build
./build.sh release

# Clean rebuild
./build.sh clean

# Run application
./build.sh run

# Run all tests
./build.sh test
```

### Running Single Tests
```bash
# Build first
./build.sh build

# Run specific test executable
cd build
./tests/unit/unit_tests          # Unit tests
./tests/integration/integration_tests  # Integration tests

# Or use ctest for specific test
ctest -R unit_tests --output-on-failure
ctest -R integration_tests --output-on-failure

# Run with verbose output
./tests/unit/unit_tests -v
```

## Code Style Guidelines

### File Organization
- **ONE class per file** - No kitchen-sink files
- **Max 200 lines** per file (400 absolute max)
- **Directory structure**:
  - `src/app/` - Application lifecycle, config
  - `src/core/` - Types, interfaces, utilities (no Qt)
  - `src/projectm/` - projectM v4 integration
  - `src/audio/` - Audio file handling
  - `src/gui/` - Qt widgets and windows
  - `src/platform/` - OS-specific code

### Naming Conventions
```cpp
// Classes: PascalCase
class ProjectMWrapper;
class VisualizerWidget;

// Functions: camelCase
void initializeGL();
void renderFrame();

// Variables: camelCase
int frameCount;
std::unique_ptr<ProjectMWrapper> m_projectM;

// Constants: SCREAMING_SNAKE_CASE
const int SAMPLE_RATE = 44100;

// Files: PascalCase.cpp/.hpp
ProjectMWrapper.cpp, ProjectMWrapper.hpp

// Private members: m_ prefix
std::vector<float> m_silenceBuffer;
```

### Header Includes
```cpp
// Standard library first
#include <memory>
#include <vector>
#include <string>

// Qt includes
#include <QObject>
#include <QWidget>

// Third-party
#include <projectM-4/projectM.h>

// Project includes (use full path from src/)
#include "projectm/ProjectMWrapper.hpp"
#include "core/utils/Logger.hpp"

// Group with blank lines, sort alphabetically within groups
```

### Documentation Style
```cpp
/**
 * @file ProjectMWrapper.hpp
 * @brief C++ wrapper around projectM v4 C API
 *
 * Single responsibility: Manage projectM lifecycle and provide C++ interface.
 * 
 * AGENT NOTE: projectM v4 uses a C API. This wrapper provides RAII safety.
 * The wrapper is NOT copyable - projectm_handle is unique.
 */
#ifndef PROJECTMWRAPPER_HPP
#define PROJECTMWRAPPER_HPP

// Code here

#endif // PROJECTMWRAPPER_HPP
```

### Error Handling
```cpp
// Use std::optional for expected failures
std::optional<Error> initialize() {
    if (!condition) {
        return Error::FailedToInitialize;
    }
    return std::nullopt;
}

// Use exceptions for truly exceptional cases
void criticalOperation() {
    if (criticalFailure) {
        throw std::runtime_error("Critical failure");
    }
}

// Always log before propagating
qWarning() << "Failed to load preset:" << path;
return false;
```

### Memory Management
```cpp
// Prefer unique_ptr for ownership
std::unique_ptr<ProjectMWrapper> m_projectM;

// Prefer references for non-owning
void setConfig(const Config& config);

// Raw pointers only for optional non-owning
VisualizerWidget* m_parent;  // Not owned, may be null
```

### Qt-Specific Patterns
```cpp
// QOpenGLWidget lifecycle
void initializeGL() override;   // One-time setup
void paintGL() override;        // Per-frame render
void resizeGL(int w, int h) override;  // Handle resize

// Always check GL context
if (!initializeOpenGLFunctions()) {
    qCritical() << "Failed to initialize OpenGL functions!";
    return;
}

// Use Qt logging
qDebug() << "State:" << value;
qWarning() << "Something unexpected";
qCritical() << "Failed to initialize";
```

### projectM v4 API Usage
```c
// Must be called with OpenGL context current
projectm_handle projectM = projectm_create();

// Configure before use
projectm_set_window_size(projectM, width, height);
projectm_set_fps(projectM, 60);

// Load preset
projectm_load_preset_file(projectM, "idle://", false);

// Feed audio (in audio callback or timer)
projectm_pcm_add_float(projectM, samples, count, PROJECTM_STEREO);

// Render each frame
projectm_opengl_render_frame(projectM);

// Cleanup
projectm_destroy(projectM);
```

## Architecture Overview

### Layer Stack
```
┌─────────────────────────────────────────┐
│ GUI (MainWindow, VisualizerWidget)      │
├─────────────────────────────────────────┤
│ Application (lifecycle, config)         │
├─────────────────┬───────────────────────┤
│ projectM        │ Audio                 │
│ Integration     │ Handling              │
├─────────────────┴───────────────────────┤
│ Core (Types, Interfaces, Utilities)     │
├─────────────────────────────────────────┤
│ Platform (OS-specific, OpenGL helpers)  │
└─────────────────────────────────────────┘
```

### Key Components
- **VisualizerWidget**: QOpenGLWidget that renders projectM
- **ProjectMWrapper**: RAII wrapper for projectM v4 C API
- **PulseAudioSource**: Captures system audio via PulseAudio/PipeWire
- **MainWindow**: Top-level window with menu bar and controls

### Render Flow
```
QTimer (60fps) → onFrameTimer() → update() → paintGL() → projectM::renderFrame()
```

### Audio Flow
```
System Audio → PulseAudio Monitor → PulseAudioSource → projectM PCM API → Visualization
```

## Testing Guidelines

### Unit Tests
- Location: `tests/unit/`
- Use Qt Test framework
- Test one class/component per file
- Mock external dependencies

### Integration Tests
- Location: `tests/integration/`
- Test component interaction
- May require OpenGL context
- Use real projectM instance

### Running Tests
```bash
# All tests
./build.sh test

# Specific test (after building)
cd build
./tests/unit/unit_tests -v

# With ctest
ctest --output-on-failure
ctest -R <pattern>  # Filter by name
```

## Common Patterns

### Safe File Operations
```bash
# NEVER use rm on source files
# INSTEAD use backup script
./scripts/utils/backup_file.sh src/file.cpp
# Moves to .backup_graveyard/ with timestamp
```

### Git Workflow
```bash
# Commit with build verification
./scripts/git/commit_safe.sh "Fix audio capture"

# Search for projectM references
./scripts/utils/search_projectm_repos.sh
```

### Debugging OpenGL
```bash
# Check GL version
glxinfo | grep "OpenGL version"

# Run with GL debug
QT_LOGGING_RULES="qt.opengl=true" ./build/src/projectm-qt-visualizer
```

## Important Notes

### projectM v4 vs v3
- **v4 uses C API** (`projectm_handle`, `projectm_create()`)
- **v3 used C++ API** (`class projectM`)
- Arch package `libprojectm` provides v4
- Header path: `/usr/include/projectM-4/`

### PulseAudio/PipeWire
- Captures from `default.monitor` (system audio)
- Requires `libpulse` and `libpulse-simple`
- Fallback to silent mode if unavailable

### OpenGL Context
- Must be current before `projectm_create()`
- Qt handles this in `initializeGL()`
- projectM requires OpenGL 2.1+ or ES 3.0+

### Potato-Safe Builds
- Uses ninja with `-j1` (single core)
- Prevents system hang on low-end hardware
- Configured in `build.sh`

## Troubleshooting

### Build fails
```bash
# Check dependencies
./scripts/check_deps.sh

# Clean and rebuild
./build.sh clean
```

### Black screen
- Check if idle:// preset loads
- Verify audio is being fed (even silent)
- Check GL context in initializeGL()

### No audio capture
- Verify PulseAudio is running: `pactl info`
- Check if monitor source exists: `pactl list sources`
- Test with `./build.sh run` and check console

### Tests fail
- Ensure build completed successfully
- Check test output: `./tests/unit/unit_tests -v`
- May need display for OpenGL tests

## Agent Directives

### Before Making Changes
1. Read `.agent/CURRENT_STATE.md`
2. Read `.agent/NEXT_TASKS.md`
3. Check `.agent/KNOWN_ISSUES.md`
4. Run `./scripts/check_deps.sh`

### While Working
- **ONE change at a time** → compile → test → verify
- **Document in SESSION_HANDOFF.md**
- **Use backup_file.sh** for any file deletion
- **Ask human for visual/audio verification**

### After Finishing
1. Update `.agent/CURRENT_STATE.md`
2. Update `.agent/SESSION_HANDOFF.md`
3. Run `./build.sh build` (verify compiles)
4. Commit with `./scripts/git/commit_safe.sh`

## References
- projectM v4 API: `/usr/include/projectM-4/`
- Qt6 Docs: https://doc.qt.io/qt-6/
- Source: https://github.com/projectM-visualizer/projectm
- Presets: https://github.com/projectM-visualizer/presets-cream-of-the-crop

---

# 📋 ACTIVE TASK LIST

## 🔴 CRITICAL - Immediate Fix Required

### Issue 1: Preset Stuck on Default Visualizer
**Status**: 🔴 BLOCKING
**Priority**: P0 - Critical Bug
**Assigned**: Pending

**Description**: 
- User settings selection does not change preset
- System stays stuck on default projectM visualizer
- Affects all preset selection methods (CLI, GUI, config)

**Expected Behavior**:
- `--preset "Name"` should load specified preset
- GUI preset selection should change visualizer
- Config file preset should be respected

**Actual Behavior**:
- Always shows default visualizer
- Settings appear to be ignored

**Files to Check**:
- `src/visualizer/ProjectMBridge.cpp` - preset loading logic
- `src/visualizer/VisualizerWindow.cpp` - preset change handling
- `src/ui/MainWindow.cpp` - preset selection methods
- `src/core/Application.cpp` - command line preset handling

**Test Commands**:
```bash
# Test 1: CLI preset
./build.sh run --preset "Aderrasi - Airhandler"

# Test 2: Default preset mode
./build.sh run --default-preset

# Test 3: GUI selection
# Launch app, use Visualizer menu → select preset
```

**Debug Steps**:
1. Add logs to `ProjectMBridge::selectPreset()`
2. Verify `useDefaultPreset` flag is correctly set
3. Check if `projectm_load_preset_file()` is called
4. Verify GL context is current when loading

---

### Issue 2: Visual Artifacts on Rapid Preset Changes
**Status**: 🔴 ACTIVE
**Priority**: P0 - Critical Bug
**Assigned**: Pending

**Description**:
- When presets are changed rapidly, visual artifacts appear
- Artifacts last for 1-2 frames
- Likely a race condition or FBO clearing issue

**Expected Behavior**:
- Preset changes should be smooth
- No visual corruption during transitions

**Actual Behavior**:
- Ghosting/artifacts for 1-2 frames
- Happens with rapid changes (< 100ms between changes)

**Files to Check**:
- `src/visualizer/ProjectMBridge.cpp` - preset change method
- `src/visualizer/VisualizerWindow.cpp` - render loop
- `src/visualizer/RenderTarget.cpp` - FBO management
- `src/audio/AudioEngine.cpp` - audio feeding during transitions

**Root Cause Theories**:
1. **FBO not cleared** between preset loads
2. **Audio feeding continues** during transition
3. **Race condition** in render thread
4. **projectM internal state** not reset properly

**Fix Requirements**:
- Clear FBO immediately on preset change
- Pause audio feeding during transition
- Ensure atomic preset change operations
- Add frame delay or transition animation

**Test Commands**:
```bash
# Rapid preset change test
./build.sh run
# Then rapidly switch presets using keyboard shortcuts

# Automated test needed
./scripts/test_rapid_preset_changes.sh
```

---

## 🟡 HIGH - Important Improvements

### Issue 3: Audio-Visual Sync Issues
**Status**: 🟡 IN PROGRESS
**Priority**: P1 - Performance
**Assigned**: Pending

**Description**:
- `framesToFeed` calculation may cause jitter
- Audio may not sync perfectly with visuals
- Affects beat detection timing

**Current Implementation**:
```cpp
u32 framesToFeed = (audioSampleRate_ + targetFps_ - 1) / targetFps_;
```

**Issue**: 
- Timer-based calculation is imprecise
- Jitter in render loop affects sync

**Recommended Fix**:
- Feed ALL available audio from queue
- Let projectM handle variable buffer sizes
- Remove frame rate dependency

**Files**:
- `src/visualizer/VisualizerWindow.cpp` - renderFrame()
- `src/audio/AudioEngine.cpp` - processAudioBuffer()

---

### Issue 4: Video Recording Threading
**Status**: 🟡 TODO
**Priority**: P1 - Performance
**Assigned**: Pending

**Description**:
- `VideoRecorder::submitVideoFrame()` blocks render thread
- Uses `sws_scale` and encoding on main thread
- Will cause FPS drops during recording

**Current Code**:
```cpp
void VideoRecorder::submitVideoFrame(...) {
    // ...
    processVideoFrame(frame); // BLOCKS RENDER THREAD
}
```

**TODO Requirements**:
- Implement PBO async read from GPU
- Push frames to FrameGrabber queue
- Process in encodingThread
- Add `// TODO: PBO Async Recording` marker

**Files**:
- `src/recorder/VideoRecorder.cpp`
- `src/recorder/FrameGrabber.cpp`

---

## 🟢 MEDIUM - Future Enhancements

### Issue 5: Config File Corruption Prevention
**Status**: ✅ COMPLETE
**Priority**: P2 - Reliability
**Assigned**: Agent

**Description**:
- Atomic config saving implemented
- Uses temp file + rename pattern
- Prevents corruption on crash

**Files Modified**:
- `src/core/Config.cpp` - save() method

**Verification**:
```bash
# Test atomic save
./build.sh run
# Change settings, kill process mid-save
# Verify config file integrity
```

---

### Issue 6: Zero-Allocation Audio Path
**Status**: ✅ COMPLETE
**Priority**: P2 - Performance
**Assigned**: Agent

**Description**:
- Scratch buffer prevents heap allocations
- Reuses memory across frames
- Reduces GC pressure

**Files Modified**:
- `src/audio/AudioEngine.hpp` - scratchBuffer_ member
- `src/audio/AudioEngine.cpp` - processAudioBuffer()

---

### Issue 7: Wayland/Hyprland Compatibility
**Status**: ✅ COMPLETE
**Priority**: P2 - Platform Support
**Assigned**: Agent

**Description**:
- RenderGuard prevents compositor hangs
- DontUseNativeDialog avoids portal issues
- All file dialogs protected

**Files Modified**:
- `src/ui/MainWindow.cpp` - All dialog methods

**Test Commands**:
```bash
# Test on Hyprland
QT_QPA_PLATFORM=wayland ./build.sh run
# Open file dialog - should not hang
```

---

### Issue 8: Diagnostic Timer Fix
**Status**: ✅ COMPLETE
**Priority**: P2 - Reliability
**Assigned**: Agent

**Description**:
- Only warns when state == Playing
- Resets flag on play()
- No false positives

**Files Modified**:
- `src/audio/AudioEngine.cpp` - play() method

---

## 🔵 LOW - Nice to Have

### Issue 9: Unified Build System
**Status**: ✅ COMPLETE
**Priority**: P3 - Developer Experience
**Assigned**: Agent

**Description**:
- Single `build.sh` with all commands
- Removed redundant scripts
- Updated all documentation

**Commands**:
```bash
./build.sh build      # Debug
./build.sh release    # Release
./build.sh run        # Run
./build.sh test       # Test
./build.sh clean      # Clean
./build.sh check-deps # Check deps
./build.sh help       # Help
```

---

### Issue 10: Repository Rename
**Status**: ✅ COMPLETE
**Priority**: P3 - Project Organization
**Assigned**: Agent

**Description**:
- Renamed to `chadvis-projectm-qt`
- Updated all references
- Fixed documentation

**Old**: `Nsomnia/mimo-v2-flash-with-claude-inital-message`
**New**: `Nsomnia/chadvis-projectm-qt`

---

## 📊 PROGRESS TRACKING

### Completed This Session:
✅ Zero-allocation audio hot path
✅ Atomic config saving
✅ Wayland/Hyprland hang fix
✅ Diagnostic timer fix
✅ Unified build system
✅ Repository rename
✅ Documentation updates

### In Progress:
🟡 Audio-visual sync optimization
🟡 Video recording threading

### Blocked:
🔴 Preset selection bug
🔴 Rapid change artifacts

### Next Steps:
1. **FIX**: Preset stuck on default (P0)
2. **FIX**: Visual artifacts on rapid changes (P0)
3. **IMPROVE**: Audio sync (P1)
4. **IMPROVE**: Video recording (P1)

---

## 📝 SESSION NOTES

### Current State
- Repository: `Nsomnia/chadvis-projectm-qt`
- Build system: Unified `build.sh`
- Status: Ready for development

### Known Working
- Default preset mode
- Audio feeding to projectM
- Frame rendering at 30 FPS
- Preset rotation (when not default mode)

### Known Issues
1. Preset selection not working (stays on default)
2. Visual artifacts on rapid preset changes

### User Feedback Pending
- Beat detection verification
- Visual quality assessment
- Performance on target hardware

---

## 🎯 AGENT DIRECTIVES

### When Working on This Project:

1. **ALWAYS** check this task list first
2. **ALWAYS** run `./build.sh check-deps` before starting
3. **ALWAYS** compile after each change
4. **ALWAYS** test with `./build.sh run`
5. **NEVER** skip the build verification step
6. **DOCUMENT** all changes in this file
7. **ASK** for visual/audio verification when needed

### Priority Order:
1. Fix critical bugs (🔴)
2. Improve performance (🟡)
3. Add features (🟢)
4. Refactor/cleanup (🔵)

### Commit Messages:
Use format: `type: description`

Examples:
- `fix: Resolve preset selection bug`
- `feat: Add PBO async recording`
- `perf: Optimize audio hot path`
- `docs: Update AGENTS.md task list`

---

**Last Updated**: 2025-12-30  
**Agent**: opencode  
**Session**: Active development
