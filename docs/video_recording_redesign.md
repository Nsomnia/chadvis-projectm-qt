# Video Recording Subsystem Redesign

## Current State Analysis

The current video recording system uses a native FFmpeg integration (`libavcodec`, `libavformat`, etc.) to encode frames captured from the OpenGL visualizer.

### Components
- `VideoRecorder`: High-level API and state management.
- `VideoRecorderThread`: Asynchronous encoding loop.
- `VideoRecorderFFmpeg`: Low-level FFmpeg wrapper.
- `FrameGrabber`: Handles `glReadPixels` to pull frames from GPU to CPU.

### Pain Points & Limitations
1.  **CPU Readback Bottleneck:** `glReadPixels` is a synchronous operation that stalls the GPU pipeline, significantly limiting capture performance at high resolutions.
2.  **Real-time Coupling:** The current system is designed for real-time capture. For batch processing ("9000 songs"), we need a non-real-time "offline" mode that renders and encodes as fast as possible.
3.  **Lack of Hardware Acceleration:** No support for NVENC (Nvidia), AMF (AMD), or QuickSync (Intel) encoders, which would offload the CPU.
4.  **Fixed Pipeline:** Hard to extend with new overlays or post-processing effects without modifying the core encoding loop.
5.  **Memory Management:** Large frame buffers are allocated and copied, which could be optimized with a pool or zero-copy path.

## Research: Browser-Based APIs (MediaRecorder, WebCodecs)

As requested, we researched the viability of using Chromium's recording APIs via `QWebEngine`.

### Findings
- **Viability:** While possible, it is **not recommended** for a C++ OpenGL source. The overhead of moving pixels from a C++ OpenGL context into a Chromium process is massive (requires CPU readback and IPC serialization).
- **Performance:** Native FFmpeg is 5-10x faster and uses significantly less memory.
- **Headless Batching:** Chromium is not optimized for headless "render farm" style batching of 9000+ items; it is prone to process exhaustion and GPU context loss.

### When to use Browser APIs?
If the application moves towards **Web-based visualizers** (e.g., Butterchurn/WebGL), then `WebCodecs` + `mp4-muxer` inside the browser would be the natural choice. For the current projectM (C++) visualizer, native FFmpeg remains the superior choice.

## Proposed New Architecture

The new architecture will focus on **Performance**, **Stability**, and **Automation**.

### 1. "Offline" Rendering Mode (Headless)
- Decouple rendering from the system clock.
- Implement a "Pull" model where the encoder requests frames from the renderer.
- Allow rendering at >60fps for faster-than-real-time batch processing.

### 2. Optimized Capture Path
- Use **Pixel Buffer Objects (PBOs)** for asynchronous `glReadPixels`. This allows the GPU to continue rendering while the previous frame is being copied to CPU memory.
- Explore **DMA-BUF** or **EGL Images** for zero-copy paths on Linux (VA-API).

### 3. Hardware Acceleration
- Add support for hardware-accelerated encoders:
    - `h264_nvenc`, `hevc_nvenc` (Nvidia)
    - `h264_vaapi`, `hevc_vaapi` (Intel/AMD on Linux)
    - `h264_amf` (AMD)

### 4. Modular Filter Pipeline
- Use FFmpeg's `libavfilter` to handle overlays, watermarks, and karaoke text.
- This moves the "drawing" of text from the OpenGL thread to the encoding thread (or GPU filters), reducing render thread load.

### 5. Automation Interface
- CLI-only mode for batch processing.
- JSON-based job descriptions for "9000 songs" use case.
- Progress callbacks and health monitoring for long-running jobs.

## Implementation Plan

1.  **Phase 1: Modernize FFmpeg Wrapper**
    - Update `VideoRecorderFFmpeg` to support more codecs and hardware acceleration flags.
    - Implement a frame pool to reduce allocations.

2.  **Phase 2: Asynchronous Capture (PBOs)**
    - Refactor `FrameGrabber` to use a double-buffered PBO approach.

3.  **Phase 3: Offline Mode**
    - Implement a headless render loop that doesn't rely on `QTimer` or screen refresh rate.

4.  **Phase 4: Automation & CLI**
    - Create a dedicated CLI tool or mode for batch processing.
