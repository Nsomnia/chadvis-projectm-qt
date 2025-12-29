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
