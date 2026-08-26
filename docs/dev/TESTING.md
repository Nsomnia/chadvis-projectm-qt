# 🧪 Testing Overview

Consolidates the former `docs/Unit-Tests-Overview.md` and `tests/manual/TEST_GUI_LAUNCH.md` (both archived).

---

## 🗺️ Module → Test Map

Unit tests are included for most systems and some sub-systems of each.

### Visualizer
- `tests/unit/visualizer/test_PresetScanner.cpp`
- `tests/unit/test_main.cpp`

### ProjectM
- `tests/unit/projectm/test_ProjectMWrapper.cpp`

### Core
- `tests/unit/core/test_Logger.cpp`
- `tests/unit/core/test_ConfigParsers.cpp`
- `tests/unit/core/test_AudioAnalyzer.cpp`

### Audio
- `tests/unit/audio/test_SilentAudioSource.cpp`

### Integration
- `tests/integration/test_projectm_render.cpp`
- `tests/integration/test_main.cpp`

### Manual
A collection of assorted manual tests written during development, not tied to one system/module. See [Manual Test: GUI Launch](#manual-test-gui-launch-mt-001) below.

> **Note:** Not all test targets are wired into the CMake build yet — see AGENTS.md (*test_PresetScanner / test_projectm_render not in CMake*).

---

## Manual Test: GUI Launch (MT-001)

**Test ID:** MT-001 · **Category:** GUI

### Prerequisites
- Application compiled successfully
- Desktop environment available

### Steps
1. Run the application:
```bash
./build.sh run
```
2. Observe the window:
   - [ ] Window appears
   - [ ] Window title shows "projectM Visualizer - Chad Edition"
   - [ ] Window is resizable
   - [ ] Menu bar visible with File and Help menus
3. Test File menu:
   - [ ] File -> Open Audio File... shows file dialog
   - [ ] File -> Exit closes the application
4. Test Help menu:
   - [ ] Help -> About shows about dialog with "I use Arch, BTW"
5. Test the visualization area:
   - [ ] Central area shows visualization (or hot pink if broken)
   - [ ] Visualization animates (even without audio)

### Expected Results
- All checkboxes should pass
- No crashes or freezes
- Console should not show errors

### Report Format
```
MT-001 Result: [PASS/FAIL]
Date: [date]
Notes: [any observations]
```

---

> "Untested code is broken code that hasn't admitted it yet." — *The Senior Dev*
