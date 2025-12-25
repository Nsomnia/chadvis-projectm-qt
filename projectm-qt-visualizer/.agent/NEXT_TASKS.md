# Next Tasks Queue
Priority: P0 (critical) > P1 (important) > P2 (nice-to-have)
## P0: Foundation
### Task: Verify Build Environment
```bash
./scripts/check_deps.sh
```
Expected: All dependencies found
### Task: First Successful Compile
1. Implement minimal main.cpp
2. Configure CMakeLists.txt properly
3. Run ./scripts/build.sh
4. Fix any errors
Goal: `build/projectm-qt-visualizer` binary exists
### Task: Empty Qt Window
1. Implement MainWindow.cpp minimally
2. Show empty window
3. HUMAN_VERIFICATION_REQUIRED: "Does a window appear?"
### Task: OpenGL Context
1. Add VisualizerWidget with QOpenGLWidget
2. Clear to recognizable color (hot pink?)
3. HUMAN_VERIFICATION_REQUIRED: "Is the window filled with pink?"
## P1: Core Visualization
### Task: Initialize projectM
1. Complete ProjectMWrapper implementation
2. Load default preset (or blank)
3. Connect to render pipeline
### Task: Idle Visualization
1. Implement SilentAudioSource
2. Feed silent PCM in render loop
3. HUMAN_VERIFICATION_REQUIRED: "Do you see animated visualization?"
## P2: Audio Playback
### Task: File Open Dialog
### Task: Audio File Loading
### Task: PCM Extraction to projectM
