---
name: projectm-gl-render
description: >-
  Works on ChadVis's projectM + OpenGL visualizer: rendering, framebuffer targets,
  preset scanning, offscreen capture, and GL teardown. Use whenever a task touches
  src/visualizer/, src/recorder/FrameGrabber or RenderExecutor, preset management,
  or anything that issues a GL call or reads pixels back. Encodes the
  framebuffer-0 rule, the leak-instead-of-wrong-namespace-delete rule, and why a
  green headless run proves nothing here.
---

# projectM + GL rendering

## The one architectural fact that decides most questions

**projectM v4 has exactly one render destination: framebuffer 0.** It binds
`GL_DRAW_FRAMEBUFFER` to 0 for its closing texture copy, next to the upstream note
*"ToDo: Allow external apps to provide a custom target framebuffer"*.

Consequences, all load-bearing:

- An **FBO is never a render target** here. `RenderTarget` exists only as a
  *readback* destination, used by `VisualizerRenderer` to rescale a framebuffer-0
  readback when the recording resolution differs from the window's
  (`RenderTarget::blitFromDefault`).
- **GL compositing of Qt Quick overlays is impossible.** Framebuffer 0 is the
  framebuffer the Quick scene graph owns; projectM and Quick would fight over the
  same draw target every frame. Overlay and karaoke burn-in is therefore an
  **FFmpeg post-process**, not a compositing problem. Do not "add an FBO" to solve
  an overlay request.

The reasoning is owned by [`docs/dev/INVARIANTS.md`](../../../docs/dev/INVARIANTS.md)
§ "Recording and timestamps"; read it before proposing a render-target change.

## Teardown: a leak is the correct trade

GL object names live in the creating context's share group. Deleting with no
context current is undefined behaviour; deleting while a **different** context is
current can silently remove unrelated names (on macOS a current Qt Quick context
can make the wrong-namespace delete succeed).

So the tree has two teardown paths and they are not interchangeable:

| Path | When | Behaviour |
| --- | --- | --- |
| `destroy()` / `cleanup()` | the creating context is current | deletes normally |
| `abandon()` / `abandonGlObjects()` | `makeCurrent` failed or context is gone | drops every handle, issues **zero** GL calls |

`RenderTarget::destroy()` enforces this per-object via `createdWith_`.
`VisualizerRenderer::abandonGlObjects()` drops projectM, playlist, PBOs and the
capture target together. **Do not "fix" the leak** — a leak is strictly better than
a wrong-namespace delete, and `cleanup()` after `abandonGlObjects()` is a safe
no-op by design.

## Verifying: a skip is not a pass

This is the single most expensive mistake available in this subsystem.

- `QOffscreenSurface` is **unusable for anything larger than 1×1 on macOS** — Qt
  hands it a 1×1 drawable, so a larger `glReadPixels` is out of bounds and returns
  zeros with `GL_INVALID_FRAMEBUFFER_OPERATION` (0x506).
- The GL fixture is a real `QWindow` with `setSurfaceType(QWindow::OpenGLSurface)`
  and an event-loop spin until `isExposed()`.
- The macOS `offscreen` QPA plugin **cannot create an OpenGL context at all**, so
  the headless/GL suite split is real and must not be collapsed.
- `integration_gl_tests` and `test_OffscreenRenderSpike` **skip headless**, and
  ctest counts a skip as success. A green `--safe` run is not evidence that
  anything rendered.

If the change affects pixels, say plainly that it is **unverified** unless a real
runtime observation exists.

## Pixel bugs to check before debugging deeper

- **`FrameGrabber::flipImage` is load-bearing.** `glReadPixels` returns rows
  bottom-up; the encoder uploads them as they arrive. Deleting that one call yields
  an upside-down video while the non-emptiness assertion still passes — which is
  why that assertion must not be weakened.
- **PTS derives from the frame's own capture time, never from a counter.**
  `GrabbedFrame::frameNumber` has no producer and is always 0; do not revive it as
  a "sanity check". It becomes a second source of truth for the timeline.

## Presets

`PresetManager` generations are **shared, never mutated in place**: every scan
builds a fresh vector and swaps it in, so a `const PresetInfo*` taken before a
rescan stays valid, and a failed scan retains the previous generation. Do not
"optimise" a scan into filling an existing vector.

The synchronous `scan()` is intentional — `pm::Bridge` reads `empty()` /
`selectByIndex(0)` immediately after calling it. Converting it to async is a
behaviour change requiring deferred first-preset selection.

`PresetBridge` wiring lives in `attachManager()` and must not be moved behind an
explicit call: `qmlEngine_->load()` runs *inside* `init()`, so QML's one-time
`model:` binding evaluates before an async scan publishes.

## Gates

```bash
./build.sh --fast --tests --safe
./build.sh --tsan --tests && ./build.sh --tsan --safe   # recorder/session work
```

Then, for anything touching capture or burn-in, run the GL entry explicitly and
confirm it **executed** rather than skipped. `docs/dev/TESTING.md` owns the suite
inventory; `docs/dev/manual-qa.md` owns the real-GUI checklist.

## Layout

`VisualizerRenderer` (GL + PBO capture + projectM bridge), `RenderTarget` (readback
FBO), `VisualizerWindow` (context ownership, `makeCurrent` assert),
`PresetManager`/`PresetScanner`/`PresetPersistence`, `RatingManager`,
`projectm/Bridge`, and `src/recorder/` for the capture → encode path.
