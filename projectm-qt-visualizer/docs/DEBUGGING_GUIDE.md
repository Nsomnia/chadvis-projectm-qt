# Debugging Guide - projectM Qt Visualizer

## Issues Identified & Fixed

### 1. QOpenGLWidget Ghosting/Screenshot Artifact
**Problem**: Window shows copy of desktop underneath, doesn't update properly
**Root Cause**: Qt's compositor caching and improper GL state management
**Fix Applied**:
- Added `setAttribute(Qt::WA_OpaquePaintEvent)` and `setAttribute(Qt::WA_NoSystemBackground)`
- Added explicit `glClear()` before projectM render in `paintGL()`
- Added `setUpdateBehavior(QOpenGLWidget::NoPartialUpdate)`
- Added proper GL state setup in `initializeGL()`

### 2. Black Screen in Fullscreen
**Problem**: Going fullscreen makes render area black
**Root Cause**: GL context state lost during window state transitions
**Fix Applied**:
- Added `changeEvent()` handler to reinitialize GL state on window state changes
- Added `makeCurrent()`/`doneCurrent()` around GL operations

### 3. JPEG Errors in File Dialog
**Problem**: File dialog tries to load thumbnails, gets binary data
**Root Cause**: File dialog showing all file types including non-images
**Fix Applied**:
- Updated `onOpenFile()` to use only audio file filters
- Removed image-related filters from dialog
- Added `QFileDialog::DontUseNativeDialog` option

### 4. No Visualization / Silent PCM Issues
**Problem**: projectM needs continuous audio data to render
**Root Cause**: Silent PCM feed was being called even when audio capture was active
**Fix Applied**:
- Modified `feedSilence()` to check if audio capture is running
- Only feeds silence when no audio source is active

### 5. Audio Capture Integration
**Problem**: No way to capture system audio
**Solution**: Added PulseAudio/PipeWire audio source
**Implementation**:
- Created `PulseAudioSource` class in `src/platform/linux/`
- Captures from `default.monitor` (system audio output)
- Feeds directly to projectM via `projectm_pcm_add_float()`
- Thread-safe with atomic running flag

## Files Modified

### Core Files
- `src/gui/VisualizerWidget.{hpp,cpp}` - Fixed GL state, added audio control
- `src/gui/MainWindow.{hpp,cpp}` - Added audio toggle, fixed file dialog
- `src/projectm/ProjectMWrapper.{hpp,cpp}` - Integrated PulseAudio, fixed silence logic

### New Files
- `src/platform/linux/PulseAudioSource.{hpp,cpp}` - PulseAudio capture implementation

### Build System
- `CMakeLists.txt` - Added PulseAudio detection
- `src/CMakeLists.txt` - Link PulseAudio to executable
- `src/projectm/CMakeLists.txt` - Link platform lib
- `src/gui/CMakeLists.txt` - Link platform lib
- `src/platform/CMakeLists.txt` - Conditional PulseAudio sources

## Build Requirements

```bash
# Required packages
sudo pacman -S qt6-base qt6-multimedia libprojectm cmake gcc ninja libpulse

# Optional (for file operations)
sudo pacman -S github-cli
```

## Testing Steps

1. **Build**: `./scripts/build.sh`
2. **Run**: `./build/src/projectm-qt-visualizer`
3. **Verify**: Window appears with "M" logo (idle preset)
4. **Test Audio**: Press Ctrl+A or use File menu to toggle audio capture
5. **Play Audio**: Play music in background, visualization should react
6. **Test Fullscreen**: Press F11 or use window controls
7. **Test File Open**: File → Open Audio File (should not show JPEG errors)

## Known Issues

### Playlist API
The projectM v4 playlist API requires a separate `projectm_playlist_handle`.
Current implementation stubs these methods with warnings.
**TODO**: Implement full playlist support.

### Audio Playback
File playback (via QMediaPlayer) not yet integrated.
Current focus is on real-time audio capture.
**TODO**: Add audio playback with stream duplication to projectM.

### Preset Loading
Preset path configuration not yet implemented.
Uses `idle://` protocol for default preset.
**TODO**: Add preset browser widget and path configuration.

## Next Steps

1. **Test the current build** - Verify all fixes work
2. **Fix any remaining build errors** - Likely PulseAudio linking issues
3. **Implement audio playback** - QMediaPlayer + projectM feed
4. **Add preset browser** - Browse and load presets
5. **Add audio device selection** - Choose different PulseAudio sources

## Architecture Notes

### Audio Flow
```
System Audio → PulseAudio Monitor → PulseAudioSource → projectM PCM API → Visualization
```

### Render Flow
```
QTimer (60fps) → VisualizerWidget::onFrameTimer() → update() → paintGL() → projectM::renderFrame()
```

### State Management
- `m_initialized` - GL context ready
- `m_audioActive` - MainWindow state
- `m_running` - PulseAudioSource thread state
- `m_projectM->isAudioCapturing()` - Wrapper query
