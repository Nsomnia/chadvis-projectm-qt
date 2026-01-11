# Frame Capture & PBOs

<details>
<summary>Relevant source files</summary>

The following files were used as context for generating this wiki page:

- [src/core/Config.cpp](src/core/Config.cpp)
- [src/recorder/VideoRecorder.cpp](src/recorder/VideoRecorder.cpp)
- [src/visualizer/VisualizerWindow.cpp](src/visualizer/VisualizerWindow.cpp)
- [src/visualizer/VisualizerWindow.hpp](src/visualizer/VisualizerWindow.hpp)

</details>



## Purpose & Scope

This page documents the asynchronous frame capture mechanism used in the recording system, specifically the implementation of double-buffered Pixel Buffer Objects (PBOs) for zero-copy GPU-to-CPU frame transfer. This covers the OpenGL capture pipeline in `VisualizerWindow`, frame data handling, and integration with the encoding thread.

For information about the overall recording architecture and FFmpeg encoding pipeline, see [VideoRecorder](#5.1). For visualization rendering details, see [VisualizerWindow](#4.1).

**Sources:** [src/visualizer/VisualizerWindow.hpp:1-125](), [src/visualizer/VisualizerWindow.cpp:1-426]()

---

## Overview

The frame capture system uses OpenGL Pixel Buffer Objects (PBOs) to achieve asynchronous, non-blocking transfer of rendered frames from GPU memory to CPU memory. This approach is critical for maintaining high frame rates during recording by eliminating GPU-CPU synchronization stalls.

### Key Design Principles

| Principle | Implementation | Benefit |
|-----------|----------------|---------|
| **Double Buffering** | Two PBOs alternate roles each frame | GPU writes to one PBO while CPU reads from the other |
| **Asynchronous Transfer** | `glReadPixels` with PBO writes immediately, `glMapBuffer` reads later | No GPU pipeline stalls |
| **Zero-Copy Semantics** | Move semantics for frame data (`std::move`) | Eliminates memory copies in signal emission |
| **Frame Queue** | `FrameGrabber` manages bounded queue | Decouples rendering thread from encoding thread |

**Sources:** [src/visualizer/VisualizerWindow.cpp:239-288]()

---

## Double-Buffered PBO Architecture

The system maintains two PBOs that alternate between **write** and **read** states each frame:

```mermaid
graph LR
    subgraph "Frame N"
        GPU_N["GPU renders to FBO"]
        PBO_A_N["PBO[0] (write)<br/>glReadPixels target"]
        PBO_B_N["PBO[1] (read)<br/>glMapBuffer source"]
        CPU_N["CPU processes<br/>frame N-1"]
    end
    
    subgraph "Frame N+1"
        GPU_N1["GPU renders to FBO"]
        PBO_A_N1["PBO[0] (read)<br/>glMapBuffer source"]
        PBO_B_N1["PBO[1] (write)<br/>glReadPixels target"]
        CPU_N1["CPU processes<br/>frame N"]
    end
    
    GPU_N --> PBO_A_N
    PBO_B_N --> CPU_N
    
    GPU_N1 --> PBO_B_N1
    PBO_A_N1 --> CPU_N1
    
    PBO_A_N -.->|"swap index"| PBO_A_N1
    PBO_B_N -.->|"swap index"| PBO_B_N1
```

**PBO State Machine:**

```mermaid
stateDiagram-v2
    [*] --> Uninitialized
    Uninitialized --> Allocated: setupPBOs()
    Allocated --> PBO_A_Write: First frame
    
    state "Double Buffer Cycle" as cycle {
        PBO_A_Write --> PBO_B_Read: captureAsync()
        PBO_B_Read --> PBO_B_Write: next frame
        PBO_B_Write --> PBO_A_Read: captureAsync()
        PBO_A_Read --> PBO_A_Write: next frame
    }
    
    cycle --> Destroyed: destroyPBOs()
    Destroyed --> [*]
```

### Member Variables

The PBO state is tracked by three member variables in `VisualizerWindow`:

```cpp
GLuint pbos_[2]{0, 0};        // OpenGL buffer objects
u32 pboIndex_{0};              // Current write index (0 or 1)
bool pboAvailable_{false};     // True after first frame captured
```

**Sources:** [src/visualizer/VisualizerWindow.hpp:103-106]()

---

## Frame Capture Pipeline

The complete pipeline from rendering to encoded video involves multiple stages across threads:

```mermaid
sequenceDiagram
    participant RT as "Render Thread<br/>(VisualizerWindow)"
    participant GL as "OpenGL Context"
    participant PBO as "PBO[pboIndex]"
    participant SIG as "Qt Signal"
    participant FG as "FrameGrabber<br/>(Queue)"
    participant ET as "Encoding Thread<br/>(VideoRecorder)"
    
    RT->>GL: renderFrame()
    GL->>PBO: glReadPixels(PBO[i], NULL)
    Note over PBO: Async GPU→PBO transfer starts
    
    RT->>PBO: glMapBuffer(PBO[1-i], READ_ONLY)
    PBO->>RT: u8* ptr (previous frame)
    RT->>RT: std::vector<u8> buffer(ptr, ptr+size)
    RT->>PBO: glUnmapBuffer()
    
    RT->>SIG: emit frameCaptured(std::move(buffer), ...)
    SIG->>FG: pushFrame(GrabbedFrame)
    
    Note over RT: pboIndex = (pboIndex + 1) % 2
    Note over RT: pboAvailable = true
    
    loop Encoding Thread
        FG->>ET: getNextFrame(frame, timeout_ms)
        ET->>ET: sws_scale(RGBA→YUV420P)
        ET->>ET: avcodec_send_frame()
    end
```

### Rendering Path with Recording

When recording is active, the rendering path includes overlay composition and frame capture:

**Sources:** [src/visualizer/VisualizerWindow.cpp:186-207]()

```mermaid
graph TB
    START["renderFrame() called"]
    
    CHECK_REC{"recording_ == true?"}
    START --> CHECK_REC
    
    CHECK_REC -->|No| SCREEN["Render directly to screen"]
    
    CHECK_REC -->|Yes| OVERLAY_CHECK{"overlayEngine_<br/>exists?"}
    
    OVERLAY_CHECK -->|Yes| OVERLAY_PATH["1. renderTarget_.bind()<br/>2. projectM_.renderToTarget()<br/>3. overlayTarget_.bind()<br/>4. renderTarget_.blitTo(overlayTarget_)<br/>5. overlayEngine_->render()"]
    
    OVERLAY_CHECK -->|No| DIRECT_PATH["1. renderTarget_.bind()<br/>2. projectM_.renderToTarget()"]
    
    OVERLAY_PATH --> CAPTURE["captureAsync()"]
    DIRECT_PATH --> CAPTURE
    
    CAPTURE --> SIGNAL["emit frameCaptured()"]
    SIGNAL --> UNBIND["overlayTarget_.unbind()"]
    
    UNBIND --> BLIT["renderTarget_.blitToScreen()"]
    SCREEN --> END["Frame complete"]
    BLIT --> END
```

**Sources:** [src/visualizer/VisualizerWindow.cpp:168-237]()

---

## PBO Lifecycle

### Setup Phase

PBOs are created when recording starts. Each PBO is allocated with size equal to `recordWidth_ * recordHeight_ * 4` (RGBA, 1 byte per channel):

```cpp
void VisualizerWindow::setupPBOs() {
    this->destroyPBOs();
    glGenBuffers(2, pbos_);
    u32 size = recordWidth_ * recordHeight_ * 4;
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, size, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboIndex_ = 0;
    pboAvailable_ = false;
}
```

**Usage Hint:** `GL_STREAM_READ` indicates the buffer will be written by the GPU (via `glReadPixels`) and read once by the CPU (via `glMapBuffer`).

**Sources:** [src/visualizer/VisualizerWindow.cpp:239-250]()

### Capture Phase

The `captureAsync()` method implements the core double-buffering logic:

**Algorithm:**
1. **Write to current PBO:** Call `glReadPixels` with `pbos_[pboIndex_]` bound as `GL_PIXEL_PACK_BUFFER`. The pixel pointer is `nullptr` because data writes to the bound PBO, not CPU memory.
2. **Read from previous PBO:** If `pboAvailable_` (i.e., not first frame), bind `pbos_[nextIndex]` and call `glMapBuffer` to get a CPU-accessible pointer.
3. **Copy to vector:** Copy data from mapped pointer to `std::vector<u8>`, which owns the memory.
4. **Emit signal:** Emit `frameCaptured` signal with moved vector (zero-copy).
5. **Swap indices:** Update `pboIndex_` to `nextIndex`.

**Sources:** [src/visualizer/VisualizerWindow.cpp:258-288]()

```mermaid
flowchart TD
    START["captureAsync() called"]
    
    CALC["nextIndex = (pboIndex_ + 1) % 2<br/>size = recordWidth_ * recordHeight_ * 4"]
    START --> CALC
    
    BIND_WRITE["glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[pboIndex_])"]
    CALC --> BIND_WRITE
    
    READ_PIX["glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, NULL)"]
    BIND_WRITE --> READ_PIX
    
    NOTE1["Note: GPU writes to pbos_[pboIndex_] asynchronously"]
    READ_PIX --> NOTE1
    
    AVAIL{"pboAvailable_?"}
    NOTE1 --> AVAIL
    
    AVAIL -->|No| SKIP["Skip read (first frame)"]
    
    AVAIL -->|Yes| BIND_READ["glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[nextIndex])"]
    
    BIND_READ --> MAP["u8* ptr = glMapBuffer(GL_READ_ONLY)"]
    
    MAP --> CHECK_PTR{"ptr != NULL?"}
    
    CHECK_PTR -->|No| SKIP
    
    CHECK_PTR -->|Yes| COPY["std::vector<u8> buffer(ptr, ptr + size)"]
    
    COPY --> UNMAP["glUnmapBuffer()"]
    
    UNMAP --> EMIT["emit frameCaptured(std::move(buffer), w, h, timestamp)"]
    
    EMIT --> UNBIND["glBindBuffer(GL_PIXEL_PACK_BUFFER, 0)"]
    SKIP --> UNBIND
    
    UNBIND --> SWAP["pboIndex_ = nextIndex<br/>pboAvailable_ = true"]
    
    SWAP --> END["Return"]
```

**Sources:** [src/visualizer/VisualizerWindow.cpp:258-288]()

### Destruction Phase

PBOs are destroyed when recording stops:

```cpp
void VisualizerWindow::destroyPBOs() {
    if (pbos_[0])
        glDeleteBuffers(2, pbos_);
    pbos_[0] = pbos_[1] = 0;
}
```

**Sources:** [src/visualizer/VisualizerWindow.cpp:252-256]()

---

## Integration with VideoRecorder

Captured frames flow from `VisualizerWindow` to `VideoRecorder` via Qt signals and the `FrameGrabber` queue:

```mermaid
graph TB
    subgraph "VisualizerWindow (Render Thread)"
        CAPTURE["captureAsync()"]
        SIGNAL["frameCaptured signal"]
        CAPTURE --> SIGNAL
    end
    
    subgraph "VideoRecorder (Main Thread)"
        SUBMIT["submitVideoFrame()<br/>std::move semantics"]
        FRAME_STRUCT["GrabbedFrame struct<br/>{data, width, height, timestamp}"]
        SUBMIT --> FRAME_STRUCT
    end
    
    subgraph "FrameGrabber (Thread-Safe Queue)"
        QUEUE["std::deque<GrabbedFrame>"]
        PUSH["pushFrame(std::move)"]
        POP["getNextFrame(timeout_ms)"]
        FRAME_STRUCT --> PUSH
        PUSH --> QUEUE
        QUEUE --> POP
    end
    
    subgraph "Encoding Thread"
        GET["getNextFrame() returns true"]
        SCALE["sws_scale(RGBA→YUV420P)"]
        ENCODE["avcodec_send_frame()"]
        GET --> SCALE
        SCALE --> ENCODE
    end
    
    SIGNAL -.->|"Qt signal"| SUBMIT
    POP --> GET
    
    style CAPTURE fill:#f9f9f9
    style SIGNAL fill:#f9f9f9
    style SUBMIT fill:#e8f4f8
    style FRAME_STRUCT fill:#e8f4f8
    style QUEUE fill:#fff4e6
    style GET fill:#e8f8e8
```

### Frame Data Structure

The `GrabbedFrame` struct (defined in `FrameGrabber.hpp`) holds captured frame data:

```cpp
struct GrabbedFrame {
    std::vector<u8> data;    // Pixel data (RGBA)
    u32 width;               // Frame width
    u32 height;              // Frame height  
    i64 timestamp;           // Capture timestamp (microseconds)
};
```

### Signal-Slot Connection

The connection is established when recording starts:

```cpp
// In VideoRecorder or controller setup:
connect(visualizerWindow, &VisualizerWindow::frameCaptured,
        videoRecorder, &VideoRecorder::submitVideoFrame);
```

The `submitVideoFrame` method uses move semantics to avoid copying the frame data:

**Sources:** [src/recorder/VideoRecorder.cpp:113-128]()

---

## Recording Start/Stop Workflow

### Starting Recording

When recording starts, the system performs these steps:

```mermaid
sequenceDiagram
    participant VR as "VideoRecorder"
    participant VW as "VisualizerWindow"
    participant GL as "OpenGL Context"
    
    VR->>VW: startRecording()
    
    VW->>VW: recording_ = true
    VW->>GL: makeCurrent()
    
    VW->>VW: renderTarget_.resize(recordWidth_, recordHeight_)
    VW->>VW: overlayTarget_.resize(recordWidth_, recordHeight_)
    VW->>VW: projectM_.resize(recordWidth_, recordHeight_)
    
    VW->>VW: setupPBOs()
    Note over VW: Creates pbos_[2], allocates GPU buffers
    
    VW->>GL: doneCurrent()
    
    Note over VW: Next renderFrame() will call captureAsync()
```

**Sources:** [src/visualizer/VisualizerWindow.cpp:316-324]()

### Stopping Recording

When recording stops:

```mermaid
sequenceDiagram
    participant VR as "VideoRecorder"
    participant VW as "VisualizerWindow"
    participant GL as "OpenGL Context"
    
    VR->>VW: stopRecording()
    
    VW->>VW: recording_ = false
    VW->>GL: makeCurrent()
    
    VW->>VW: destroyPBOs()
    Note over VW: Deletes pbos_[2], frees GPU memory
    
    VW->>GL: doneCurrent()
    
    Note over VW: Next renderFrame() skips captureAsync()
    Note over VW: FBOs resize to window dimensions
```

**Sources:** [src/visualizer/VisualizerWindow.cpp:327-333]()

---

## Performance Considerations

### Zero-Copy Semantics

The frame capture pipeline minimizes memory copies:

| Transfer Stage | Method | Copy? |
|----------------|--------|-------|
| GPU → PBO | `glReadPixels` with bound PBO | **No** (DMA transfer) |
| PBO → CPU vector | `std::vector` constructor from pointer | **Yes** (required for ownership) |
| Vector → Signal | `std::move(buffer)` | **No** (move semantics) |
| Signal → FrameGrabber | `pushFrame(std::move(frame))` | **No** (move semantics) |
| FrameGrabber → Encoding thread | `getNextFrame()` | **No** (returns reference) |

**Total copies per frame:** 1 (PBO to vector, unavoidable)

**Sources:** [src/visualizer/VisualizerWindow.cpp:273-277](), [src/recorder/VideoRecorder.cpp:120-127]()

### Asynchronous Overlap

The double-buffering enables overlap between GPU and CPU operations:

```
Frame N:   [GPU renders] [GPU→PBO_A transfer]
                                              [CPU reads PBO_B] [CPU process]
                                              
Frame N+1:                [GPU renders] [GPU→PBO_B transfer]
                                                               [CPU reads PBO_A] [CPU process]
```

Without PBOs, the pipeline would stall:
```
Frame N:   [GPU renders] [WAIT for GPU] [CPU reads pixels] [CPU process]
Frame N+1:                                                  [GPU renders] [WAIT]...
```

### Frame Queue Backpressure

The `FrameGrabber` implements a bounded queue with configurable capacity. When the encoding thread falls behind, `pushFrame()` will drop frames rather than block the render thread:

**Default behavior:** If queue is full, oldest frame is dropped and `droppedFrames_` counter increments.

**Sources:** Referenced in diagram from [src/recorder/VideoRecorder.cpp:51-52](), queue implementation in `FrameGrabber`

---

## Configuration

Recording resolution is set via the `RecordingConfig` section in `config.toml`:

```toml
[recording.video]
width = 1920          # Must be even (H.264 requirement)
height = 1080         # Must be even
fps = 30
```

The system enforces even dimensions during config loading:

**Sources:** [src/core/Config.cpp:228-236]()

PBO buffer size is automatically calculated as `width * height * 4` bytes (RGBA format with 1 byte per channel).

---

## Summary

The PBO-based frame capture system provides:

- **Asynchronous GPU-to-CPU transfer** via double-buffered PBOs
- **Zero-copy signal emission** via move semantics
- **Thread-safe frame queue** decoupling rendering from encoding
- **Automatic frame dropping** when encoding falls behind
- **Minimal pipeline stalls** through overlapped GPU/CPU operations

This architecture enables high-quality, high-framerate recording without impacting real-time visualization performance.

**Sources:** [src/visualizer/VisualizerWindow.cpp:239-288](), [src/visualizer/VisualizerWindow.hpp:99-106](), [src/recorder/VideoRecorder.cpp:113-128]()