# Implementation Plan: Fix Ghost Image & Improve Rendering

## Goal
Fix the "ghost image of desktop" issue in the visualizer rendering canvas.

## User Review Required
-   **Validation**: This fix assumes the ghosting is due to uninitialized FBO or alpha transparency. User should verify if the ghosting persists after applying the fix.

## Proposed Changes

### visualizer [src/visualizer]

#### [MODIFY] [RenderTarget.cpp](file:///home/nsomnia/Documents/code/chadvis-projectm-qt-vidz/chadvis-projectm-qt-vidz/src/visualizer/RenderTarget.cpp)
-   **Update `create()` method**:
    -   After binding the framebuffer (`glBindFramebuffer`), call `glClearColor(0, 0, 0, 1)` and `glClear(GL_COLOR_BUFFER_BIT)`. (Or `(0,0,0,0)` if transparency is desired initially, but unlikely).
    -   This prevents undefined/garbage memory in the texture.

#### [MODIFY] [VisualizerWidget.cpp](file:///home/nsomnia/Documents/code/chadvis-projectm-qt-vidz/chadvis-projectm-qt-vidz/src/visualizer/VisualizerWidget.cpp)
-   **Update `initializeGL()`**:
    -   Explicitly call `glClearColor(0, 0, 0, 1)`.
-   **Update `renderFrame()`**:
    -   Ensure the background is cleared if ProjectM is not in a mode that requires feedback (though hard to know per preset).
    -   Ensure blitting to screen forces alpha = 1.0 or use a blend mode that doesn't let the desktop show through.
    -   If `recording` is true, ensure the alpha channel is handled correctly (e.g. usually we want opaque video).

## Verification Plan

### Manual Verification
1.  **Launch Application**: Run the app.
2.  **Observe Visualizer**: Check if the visualizer background is solid black (or the visualization).
3.  **Check for Ghosting**: Look for desktop wallpaper or other windows visible "through" the visualizer.
4.  **Resize Window**: Resize the window to trigger `RenderTarget::resize` (re-creation) and ensure no garbage appears.

### Automated Tests
-   No specific automated tests for visual rendering artifacts exist. Verification must be visual.
