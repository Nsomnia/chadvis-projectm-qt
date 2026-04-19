# VideoRecorder

<details>
<summary>Relevant source files</summary>

The following files were used as context for generating this wiki page:

- [src/core/Config.cpp](src/core/Config.cpp)
- [src/recorder/VideoRecorder.cpp](src/recorder/VideoRecorder.cpp)

</details>



## Purpose and Scope

The `VideoRecorder` class implements FFmpeg-based video recording for the chadvis-projectm-qt visualizer. It captures rendered visualization frames and audio playback, encoding them into video files (MP4/MKV) with configurable codecs and quality settings. The recorder operates asynchronously on a dedicated encoding thread to avoid blocking the main rendering loop.

This page documents the `VideoRecorder` class, its FFmpeg pipeline initialization, codec configuration, asynchronous encoding architecture, and integration with the configuration system. For details about frame capture using Pixel Buffer Objects, see [Frame Capture & PBOs](#5.2).

**Sources:** [src/recorder/VideoRecorder.cpp:1-639]()

---

## Class Architecture

The `VideoRecorder` class manages the complete recording pipeline from frame submission to file output. It uses FFmpeg libraries (libavcodec, libavformat, libswscale, libswresample) for encoding and muxing.

### Component Diagram

```mermaid
graph TB
    subgraph "VideoRecorder Class"
        VR["VideoRecorder"]
        
        subgraph "State Management"
            STATE["state_<br/>RecordingState"]
            STATS["stats_<br/>RecordingStats"]
            SETTINGS["settings_<br/>EncoderSettings"]
        end
        
        subgraph "FFmpeg Context (ffmpegMutex_)"
            FORMAT_CTX["formatCtx_<br/>AVFormatContext*"]
            VIDEO_CTX["videoCodecCtx_<br/>AVCodecContext*"]
            AUDIO_CTX["audioCodecCtx_<br/>AVCodecContext*"]
            VIDEO_STREAM["videoStream_<br/>AVStream*"]
            AUDIO_STREAM["audioStream_<br/>AVStream*"]
            PACKET["packet_<br/>AVPacket*"]
        end
        
        subgraph "Conversion Contexts"
            SWS_CTX["swsCtx_<br/>SwsContext*<br/>(RGBA → YUV420P)"]
            SWR_CTX["swrCtx_<br/>SwrContext*<br/>(f32 → codec format)"]
            VIDEO_FRAME["videoFrame_<br/>AVFrame*"]
            AUDIO_FRAME["audioFrame_<br/>AVFrame*"]
        end
        
        subgraph "Frame Queue (audioMutex_)"
            FRAME_GRABBER["frameGrabber_<br/>FrameGrabber"]
            AUDIO_BUF["audioBuffer_<br/>vector<f32>"]
        end
        
        subgraph "Encoding Thread"
            THREAD["encodingThread_<br/>std::thread"]
            SHOULD_STOP["shouldStop_<br/>atomic<bool>"]
        end
    end
    
    VR --> STATE
    VR --> STATS
    VR --> SETTINGS
    VR --> FORMAT_CTX
    VR --> VIDEO_CTX
    VR --> AUDIO_CTX
    VR --> VIDEO_STREAM
    VR --> AUDIO_STREAM
    VR --> PACKET
    VR --> SWS_CTX
    VR --> SWR_CTX
    VR --> VIDEO_FRAME
    VR --> AUDIO_FRAME
    VR --> FRAME_GRABBER
    VR --> AUDIO_BUF
    VR --> THREAD
    VR --> SHOULD_STOP
    
    FORMAT_CTX -.-> VIDEO_STREAM
    FORMAT_CTX -.-> AUDIO_STREAM
    VIDEO_CTX -.-> VIDEO_FRAME
    AUDIO_CTX -.-> AUDIO_FRAME
```

**Sources:** [src/recorder/VideoRecorder.cpp:1-639]()

---

## State Machine and Lifecycle

The recorder operates as a state machine with five states managed by the `RecordingState` enum.

### Recording State Transitions

```mermaid
stateDiagram-v2
    [*] --> Stopped
    
    Stopped --> Starting: start()
    Starting --> Recording: initFFmpeg() success
    Starting --> Error: initFFmpeg() failure
    
    Recording --> Stopping: stop()
    Stopping --> Stopped: cleanup complete
    
    Error --> Stopped: reset
    
    note right of Starting
        - Validate settings
        - Initialize FFmpeg contexts
        - Create output file
        - Start encoding thread
    end note
    
    note right of Recording
        - submitVideoFrame() active
        - submitAudioSamples() active
        - Encoding thread processing
    end note
    
    note right of Stopping
        - Signal thread to stop
        - Join encoding thread
        - flushEncoders()
        - av_write_trailer()
    end note
```

### Lifecycle Methods

| Method | State Transition | Key Operations |
|--------|------------------|----------------|
| `start(EncoderSettings)` | Stopped → Starting → Recording | [VideoRecorder.cpp:18-65]() |
| `stop()` | Recording → Stopping → Stopped | [VideoRecorder.cpp:73-111]() |
| `cleanupFFmpeg()` | Any → Stopped | [VideoRecorder.cpp:521-537]() |

**Sources:** [src/recorder/VideoRecorder.cpp:18-111](), [src/recorder/VideoRecorder.cpp:521-537]()

---

## FFmpeg Pipeline Initialization

The initialization process creates FFmpeg contexts in a specific order to ensure proper muxing.

### Initialization Sequence

```mermaid
sequenceDiagram
    participant Client
    participant VR as VideoRecorder
    participant FFmpeg as FFmpeg API
    participant FileSystem
    
    Client->>VR: start(EncoderSettings)
    VR->>VR: validate settings
    VR->>FileSystem: ensureDir(outputPath)
    
    VR->>FFmpeg: avformat_alloc_output_context2()
    FFmpeg-->>VR: formatCtx_
    
    VR->>VR: initVideoStream()
    VR->>FFmpeg: avcodec_find_encoder_by_name()
    VR->>FFmpeg: avformat_new_stream()
    VR->>FFmpeg: avcodec_alloc_context3()
    VR->>FFmpeg: avcodec_open2(opts: preset, crf, tune)
    VR->>FFmpeg: sws_getContext(RGBA→YUV420P)
    
    VR->>VR: initAudioStream()
    VR->>FFmpeg: avcodec_find_encoder_by_name()
    VR->>FFmpeg: avformat_new_stream()
    VR->>FFmpeg: avcodec_alloc_context3()
    VR->>FFmpeg: avcodec_open2()
    VR->>FFmpeg: swr_alloc_set_opts2(f32→codec format)
    
    VR->>FFmpeg: avio_open(outputPath, WRITE)
    VR->>FFmpeg: avformat_write_header()
    
    VR->>VR: Start encodingThread_
    VR-->>Client: Result<void>::ok()
```

### Video Stream Initialization

The `initVideoStream()` method configures the video encoder with codec-specific options.

**Key Configuration Steps:**

1. **Codec Selection** [VideoRecorder.cpp:329-334]()
   - Uses `settings_.video.codecName()` (e.g., "libx264", "libx265")
   - Falls back to error if codec not found

2. **Codec Context Setup** [VideoRecorder.cpp:346-357]()
   ```
   videoCodecCtx_->width = settings_.video.width
   videoCodecCtx_->height = settings_.video.height
   videoCodecCtx_->time_base = {1, fps}
   videoCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P
   videoCodecCtx_->gop_size = fps * 2 (default)
   videoCodecCtx_->max_b_frames = settings_.video.bFrames
   ```

3. **H.264/H.265 Options** [VideoRecorder.cpp:362-369]()
   - `preset`: ultrafast, veryfast, etc. (encoding speed vs compression)
   - `crf`: 0-51 (quality, lower = better, 23 is default)
   - `tune`: zerolatency (for real-time encoding)

4. **SwScale Context** [VideoRecorder.cpp:401-414]()
   - Converts RGBA (from OpenGL) to YUV420P (for H.264)
   - Uses `SWS_BILINEAR` scaling algorithm

**Sources:** [src/recorder/VideoRecorder.cpp:328-423]()

### Audio Stream Initialization

The `initAudioStream()` method configures the audio encoder with channel layout and sample format conversion.

**Key Configuration Steps:**

1. **Codec Selection** [VideoRecorder.cpp:426-432]()
   - Uses `settings_.audio.codecName()` (e.g., "aac", "libmp3lame")
   - Logs warning and continues without audio if codec not found

2. **Codec Context Setup** [VideoRecorder.cpp:444-454]()
   ```
   audioCodecCtx_->sample_rate = settings_.audio.sampleRate (44100 Hz)
   audioCodecCtx_->bit_rate = settings_.audio.bitrate * 1000
   audioCodecCtx_->sample_fmt = codec->sample_fmts[0] (usually AV_SAMPLE_FMT_FLTP)
   av_channel_layout_default(&layout, settings_.audio.channels)
   ```

3. **SwResample Context** [VideoRecorder.cpp:491-511]()
   - Converts float32 PCM (from AudioEngine) to codec format (e.g., FLTP for AAC)
   - Handles sample rate conversion if needed
   - Configured with `swr_alloc_set_opts2()` and initialized with `swr_init()`

**Sources:** [src/recorder/VideoRecorder.cpp:425-519]()

---

## Encoding Thread Architecture

The encoding thread runs asynchronously to avoid blocking the render loop. It processes video frames and audio samples, encodes them, and writes packets to the output file.

### Thread Execution Flow

```mermaid
flowchart TD
    START["encodingThread() starts"]
    
    WAIT["Wait for next frame<br/>frameGrabber_.getNextFrame(timeout=10ms)"]
    
    HAS_VIDEO{"hasVideo?"}
    PROCESS_VIDEO["processVideoFrame(frame)<br/>- sws_scale(RGBA→YUV420P)<br/>- encodeVideoFrame()<br/>- writePacket()"]
    
    PROCESS_AUDIO["processAudioBuffer()<br/>- swr_convert(f32→codec fmt)<br/>- encodeAudioFrame()<br/>- writePacket()"]
    
    CHECK_STOP{"shouldStop_ &&<br/>no frames?"}
    
    UPDATE_STATS["Update stats every 1s<br/>- elapsed time<br/>- avgFps<br/>- framesDropped<br/>emit statsUpdated"]
    
    END["Thread exits<br/>flushEncoders() called by stop()"]
    
    START --> WAIT
    WAIT --> HAS_VIDEO
    HAS_VIDEO -->|yes| PROCESS_VIDEO
    HAS_VIDEO -->|no| PROCESS_AUDIO
    PROCESS_VIDEO --> PROCESS_AUDIO
    PROCESS_AUDIO --> CHECK_STOP
    CHECK_STOP -->|yes| END
    CHECK_STOP -->|no| UPDATE_STATS
    UPDATE_STATS --> WAIT
```

**Thread Safety:**
- `ffmpegMutex_` protects all FFmpeg context access during encoding
- `audioMutex_` protects `audioBuffer_` during `submitAudioSamples()` and `processAudioBuffer()`
- `shouldStop_` is an atomic flag for thread coordination

**Sources:** [src/recorder/VideoRecorder.cpp:172-211]()

---

## Video Frame Processing

Video frames are submitted from the render thread and processed asynchronously by the encoding thread.

### Frame Submission Path

```mermaid
graph LR
    VW["VisualizerWindow<br/>(render thread)"]
    SUBMIT["submitVideoFrame()<br/>(2 overloads)"]
    GRABBED["GrabbedFrame<br/>struct"]
    QUEUE["FrameGrabber<br/>queue"]
    ENCODING["encodingThread_<br/>(background)"]
    
    VW -->|"data, width, height, timestamp"| SUBMIT
    SUBMIT -->|"construct GrabbedFrame"| GRABBED
    GRABBED -->|"std::move()"| QUEUE
    QUEUE -->|"getNextFrame()"| ENCODING
```

### Frame Processing Implementation

**Submission Methods** [VideoRecorder.cpp:113-148]():

| Method | Signature | Data Handling |
|--------|-----------|---------------|
| `submitVideoFrame(std::vector<u8>&&, ...)` | Move semantics | Zero-copy transfer |
| `submitVideoFrame(const u8*, ...)` | Pointer + size | Copies data into vector |

**Processing Pipeline** [VideoRecorder.cpp:213-239]():

1. **Retrieve Frame** from `FrameGrabber` queue
2. **Color Conversion** using `sws_scale()`:
   ```
   Source: RGBA (4 bytes/pixel, from OpenGL)
   Destination: YUV420P (1.5 bytes/pixel, for H.264)
   Algorithm: SWS_BILINEAR
   ```
3. **Set PTS** (Presentation Timestamp):
   ```cpp
   videoFrame_->pts = videoFrameCount_++;
   ```
4. **Encode Frame** via `encodeVideoFrame()`
5. **Update Stats**: `++stats_.framesWritten`

**Sources:** [src/recorder/VideoRecorder.cpp:113-148](), [src/recorder/VideoRecorder.cpp:213-239]()

---

## Audio Sample Processing

Audio samples are buffered and processed in frame-sized chunks required by the codec.

### Audio Processing Flow

```mermaid
flowchart TD
    AE["AudioEngine<br/>(audio callback thread)"]
    
    SUBMIT["submitAudioSamples()<br/>(f32* data, samples, channels, sampleRate)"]
    
    BUFFER["audioBuffer_<br/>vector<f32><br/>(protected by audioMutex_)"]
    
    CHECK{"buffer.size() >=<br/>frameSize * channels?"}
    
    EXTRACT["Extract frameSize*channels samples<br/>erase from buffer front"]
    
    CONVERT["swr_convert()<br/>(f32 → codec sample_fmt)"]
    
    ENCODE["encodeAudioFrame()<br/>audioFrame_->pts = audioFrameCount_<br/>audioFrameCount_ += frameSize"]
    
    WRITE["writePacket(audioStream_)"]
    
    AE -->|"lock audioMutex_"| SUBMIT
    SUBMIT -->|"append samples"| BUFFER
    
    BUFFER --> CHECK
    CHECK -->|yes| EXTRACT
    CHECK -->|no| RETURN["Return (wait for more)"]
    
    EXTRACT --> CONVERT
    CONVERT --> ENCODE
    ENCODE --> WRITE
    WRITE --> CHECK
```

### Implementation Details

**Audio Buffering** [VideoRecorder.cpp:150-170]():
- Samples arrive asynchronously from `AudioEngine`
- Stored as float32 PCM in `audioBuffer_`
- Thread-safe via `audioMutex_`

**Frame-based Processing** [VideoRecorder.cpp:241-279]():
- Most codecs (e.g., AAC) require fixed-size frames
- `audioCodecCtx_->frame_size` determines frame size (typically 1024 samples for AAC)
- Loop continues until insufficient samples remain

**Sample Format Conversion**:
```cpp
swr_convert(swrCtx_.get(),
            audioFrame_->data,      // destination (codec format, e.g., FLTP)
            frameSize,
            srcData,                // source (float32 PCM)
            frameSize);
```

**Sources:** [src/recorder/VideoRecorder.cpp:150-170](), [src/recorder/VideoRecorder.cpp:241-279]()

---

## Configuration Integration

Recording settings are loaded from the TOML configuration file and mapped to `EncoderSettings`.

### Configuration Structure

```mermaid
classDiagram
    class RecordingConfig {
        +bool enabled
        +bool autoRecord
        +fs::path outputDirectory
        +string defaultFilename
        +string container
        +VideoEncoderConfig video
        +AudioEncoderConfig audio
    }
    
    class VideoEncoderConfig {
        +VideoCodec codec
        +u32 width
        +u32 height
        +u32 fps
        +u32 crf
        +string preset
        +string pixelFormat
        +u32 gopSize
        +u32 bFrames
        +string codecName()
        +string presetName()
    }
    
    class AudioEncoderConfig {
        +AudioCodec codec
        +u32 sampleRate
        +u32 channels
        +u32 bitrate
        +string codecName()
    }
    
    class EncoderSettings {
        +fs::path outputPath
        +VideoEncoderConfig video
        +AudioEncoderConfig audio
        +static fromConfig() EncoderSettings
        +validate() Result~void~
    }
    
    RecordingConfig *-- VideoEncoderConfig
    RecordingConfig *-- AudioEncoderConfig
    EncoderSettings *-- VideoEncoderConfig
    EncoderSettings *-- AudioEncoderConfig
```

### TOML Configuration Format

The configuration is loaded by `parseRecording()` in the `Config` class.

**Example Configuration** [Config.cpp:206-248]():
```toml
[recording]
enabled = true
auto_record = false
output_directory = "~/Videos/ChadVis"
default_filename = "chadvis-projectm-qt_{date}_{time}"
container = "mp4"

[recording.video]
codec = "libx264"
crf = 23
preset = "ultrafast"
pixel_format = "yuv420p"
width = 1280
height = 720
fps = 30

[recording.audio]
codec = "aac"
bitrate = 192
```

### Configuration Validation

The `EncoderSettings::validate()` method ensures settings are within acceptable ranges:

- **Video dimensions**: Must be even numbers (H.264 requirement)
- **FPS**: Clamped to 10-120 range
- **CRF**: Clamped to 0-51 (H.264/H.265)
- **Bitrate**: Clamped to 64-640 kbps
- **Output path**: Must have write permissions

**Sources:** [src/core/Config.cpp:206-248]()

---

## Encoding and Packet Writing

The encoding process follows FFmpeg's asynchronous encoding API, introduced in FFmpeg 3.x.

### Encoding Flow Diagram

```mermaid
sequenceDiagram
    participant Thread as Encoding Thread
    participant Codec as AVCodecContext
    participant Format as AVFormatContext
    participant File as Output File
    
    Thread->>Codec: avcodec_send_frame(frame)
    
    loop While packets available
        Thread->>Codec: avcodec_receive_packet(packet)
        Codec-->>Thread: packet or EAGAIN
        
        alt Got packet
            Thread->>Thread: av_packet_rescale_ts()<br/>(codec timebase → stream timebase)
            Thread->>Thread: set packet->stream_index
            Thread->>Format: av_interleaved_write_frame(packet)
            Format->>File: Write muxed packet
            Thread->>Thread: stats_.bytesWritten += packet->size
        else EAGAIN or EOF
            Note over Thread,Codec: Break loop, need more frames
        end
    end
```

### Encode Methods

**Video Encoding** [VideoRecorder.cpp:539-567]():
```cpp
bool encodeVideoFrame(AVFrame* frame) {
    avcodec_send_frame(videoCodecCtx_.get(), frame);
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(videoCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        
        writePacket(packet_.get(), videoStream_);
    }
    return true;
}
```

**Audio Encoding** [VideoRecorder.cpp:569-597]():
- Identical structure to video encoding
- Uses `audioCodecCtx_` and `audioStream_`

**Packet Writing** [VideoRecorder.cpp:599-615]():
- Rescales packet timestamps from codec timebase to stream timebase
- Writes interleaved packets (maintains A/V sync)
- Updates `stats_.bytesWritten`

### Encoder Flushing

When recording stops, remaining frames must be flushed from codec buffers.

**Flush Implementation** [VideoRecorder.cpp:617-637]():
```cpp
void flushEncoders() {
    // Send NULL frame to signal end of stream
    avcodec_send_frame(videoCodecCtx_.get(), nullptr);
    
    // Receive all remaining packets
    while (avcodec_receive_packet(videoCodecCtx_.get(), packet_.get()) >= 0) {
        writePacket(packet_.get(), videoStream_);
    }
    
    // Repeat for audio
    avcodec_send_frame(audioCodecCtx_.get(), nullptr);
    while (avcodec_receive_packet(audioCodecCtx_.get(), packet_.get()) >= 0) {
        writePacket(packet_.get(), audioStream_);
    }
}
```

**Sources:** [src/recorder/VideoRecorder.cpp:539-637]()

---

## Error Handling and Statistics

The recorder provides robust error handling and real-time statistics via signal emission.

### Signal Emission System

| Signal | Type | Emitted When |
|--------|------|--------------|
| `stateChanged` | `Signal<RecordingState>` | State transitions |
| `statsUpdated` | `Signal<RecordingStats>` | Every second during recording |
| `error` | `Signal<std::string>` | Encoding errors, write failures |

### RecordingStats Structure

```cpp
struct RecordingStats {
    u64 framesWritten;      // Total frames encoded
    u64 framesDropped;      // Frames dropped by FrameGrabber
    u64 bytesWritten;       // Total file size in bytes
    Duration elapsed;       // Recording duration
    f64 avgFps;            // Average encoding FPS
    std::string currentFile; // Output file path
};
```

**Stats Update Logic** [VideoRecorder.cpp:190-207]():
- Updated every 1 second in encoding thread
- `avgFps` calculated as: `framesWritten * 1000 / elapsed.count()`
- `framesDropped` retrieved from `FrameGrabber`

### Error Handling Patterns

**Initialization Errors** [VideoRecorder.cpp:18-65]():
```cpp
if (auto result = initFFmpeg(); !result) {
    cleanupFFmpeg();
    state_ = RecordingState::Error;
    stateChanged.emitSignal(state_);
    return result;  // Return Result<void>::err()
}
```

**Encoding Errors** [VideoRecorder.cpp:541-558]():
```cpp
int ret = avcodec_send_frame(videoCodecCtx_.get(), frame);
if (ret < 0) {
    std::string errMsg = "Error sending video frame: " + ffmpegError(ret);
    LOG_WARN("{}", errMsg);
    error.emitSignal(errMsg);
    return false;
}
```

**Cleanup on Stop** [VideoRecorder.cpp:73-111]():
- Sets `state_ = RecordingState::Stopping`
- Signals thread to stop via `shouldStop_ = true`
- Waits for thread join
- Flushes encoders and writes trailer
- Calls `cleanupFFmpeg()` to release resources

**Sources:** [src/recorder/VideoRecorder.cpp:18-111](), [src/recorder/VideoRecorder.cpp:172-211](), [src/recorder/VideoRecorder.cpp:539-637]()

---

## Integration with Application

The `VideoRecorder` is owned by the `Application` singleton and controlled through the `RecordingController`.

### Ownership and Access Pattern

```mermaid
graph TB
    APP["Application<br/>(singleton)"]
    VR["VideoRecorder<br/>(owned)"]
    RC["RecordingController<br/>(non-owning ptr)"]
    RW["RecordingControls<br/>(UI widget)"]
    VW["VisualizerWindow<br/>(non-owning ptr)"]
    AE["AudioEngine<br/>(non-owning ptr)"]
    
    APP -->|owns| VR
    APP -->|provides ptr| RC
    RC -->|controls| VR
    RC -->|connects signals| RW
    
    VW -->|"submitVideoFrame()"| VR
    AE -->|"submitAudioSamples()"| VR
    
    VR -.->|"stateChanged signal"| RC
    VR -.->|"statsUpdated signal"| RC
    VR -.->|"error signal"| RC
```

**Sources:** Based on architectural patterns from Diagram 1 and Diagram 3 in the high-level system overview.