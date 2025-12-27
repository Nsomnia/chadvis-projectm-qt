# Analysis: ProjectM Ghost Image Issue

## Problem Description
The user reported a "ghost image of the desktop" behind the ProjectM visualizer rendering canvas. This manifest as seeing the desktop wallpaper or windows specifically through the visualizer area.

## Investigation Findings

### 1. Render Target Initialization
In `src/visualizer/RenderTarget.cpp`, the FBO texture is initialized using `glTexImage2D` with `nullptr` data:
```cpp
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
```
This allocates GPU memory but does not initialize it. The initial content is undefined (can be garbage or previous memory content).
**Crucially**, `RenderTarget::create` does *not* clear the texture after creation.

### 2. Missing Clear in Rendering Loop
In `src/visualizer/ProjectMBridge.cpp`, `renderToTarget` binds the FBO and immediately calls `projectm_opengl_render_frame`:
```cpp
void ProjectMBridge::renderToTarget(RenderTarget& target) {
    // ...
    target.bind();
    projectm_opengl_render_frame(projectM_); // No glClear here!
    target.unbind();
}
```
ProjectM often relies on the previous frame being present (for trail effects), so it does not clear the background itself by default (or might only partially clear).
If the FBO starts with garbage (or transparent) content, ProjectM draws on top of it.

### 3. Transparency/Alpha Channel
`RenderTarget` is created with `GL_RGBA8`, meaning it has an alpha channel.
If ProjectM renders with additive blending or doesn't write to the alpha channel (keeping it at initial 0.0), the resulting texture will be transparent.
In `VisualizerWidget::renderFrame`, this texture is blitted to the screen:
```cpp
renderTarget_.blitToScreen(width(), height(), true);
```
If the window system (Qt/OS) supports transparency (compositing), and the widget paints transparent pixels, the desktop will show through.

## Root Causes
1.  **Uninitialized FBO**: The `RenderTarget` texture is never cleared to opaque black upon creation/resize.
2.  **Alpha Channel Leak**: The rendering pipeline does not guarantee an opaque alpha channel (1.0). If `ProjectM` doesn't write opaque alpha, the transparency is preserved and blitted to the screen.

## Proposed Solution
1.  **Clear on Create**: Modify `RenderTarget::create` (or `resize`) to clear the FBO to `(0, 0, 0, 1)` (Opaque Black) immediately after creation.
    ```cpp
    // In RenderTarget::create
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ```
2.  **Ensure Opaque Rendering**:
    -   Option A: Clear `renderTarget_` to black before every frame in `renderFrame` *if* ProjectM is configured to not use feedback/trails.
    -   Option B: Use `glColorMask` or a shader to force Alpha to 1.0 when blitting to the screen.
    -   Option C: Ensure `VisualizerWidget` background is opaque black and `glEnable(GL_BLEND)` is handled correctly.

## References
-   `src/visualizer/RenderTarget.cpp`: L53 (glTexImage2D), L35 (create)
-   `src/visualizer/ProjectMBridge.cpp`: L85 (renderToTarget)
-   `src/visualizer/VisualizerWidget.cpp`: L110 (renderFrame)
