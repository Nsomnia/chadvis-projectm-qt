# Audio System

<details>
<summary>Relevant source files</summary>

The following files were used as context for generating this wiki page:

- [AGENTS.md](AGENTS.md)
- [src/audio/AudioEngine.cpp](src/audio/AudioEngine.cpp)
- [src/audio/AudioEngine.hpp](src/audio/AudioEngine.hpp)

</details>



## Purpose and Scope

The Audio System handles all aspects of audio playback, decoding, analysis, and track management in chadvis-projectm-qt. It serves as the audio data source for the visualization pipeline, providing real-time PCM samples and spectrum data to projectM. This document covers the architecture of the audio subsystem, the buffer processing pipeline, and integration points with other systems.

For detailed implementation of the main audio engine, see [AudioEngine](#3.1). For playlist functionality and M3U file handling, see [Playlist Management](#3.2). For how audio data drives visualizations, see [Visualization System](#4).

---

## System Architecture

The Audio System consists of three primary components working in concert: `AudioEngine` for playback orchestration, `AudioAnalyzer` for frequency spectrum extraction, and `Playlist` for track management. The system is designed around Qt's Multimedia framework with custom signal routing to provide zero-copy audio data to visualization and recording subsystems.

```mermaid
graph TB
    subgraph "AudioEngine Component"
        AE["AudioEngine<br/>(QObject)"]
        PLAYER["QMediaPlayer<br/>player_"]
        AUDIO_OUT["QAudioOutput<br/>audioOutput_"]
        BUFFER_OUT["QAudioBufferOutput<br/>bufferOutput_"]
    end
    
    subgraph "Analysis Component"
        ANALYZER["AudioAnalyzer<br/>analyzer_"]
        SPECTRUM["AudioSpectrum<br/>currentSpectrum_"]
        SCRATCH["vector&lt;f32&gt;<br/>scratchBuffer_"]
    end
    
    subgraph "Playlist Component"
        PL["Playlist<br/>playlist_"]
        ITEMS["vector&lt;PlaylistItem&gt;"]
        M3U["last_session.m3u"]
    end
    
    subgraph "External Interfaces"
        MEDIA_FILE["Local Audio Files<br/>MP3/FLAC/WAV/etc"]
        SUNO["Suno Downloads<br/>via SunoController"]
    end
    
    subgraph "Output Signals"
        STATE_SIG["Signal&lt;PlaybackState&gt;<br/>stateChanged"]
        SPECTRUM_SIG["Signal&lt;AudioSpectrum&gt;<br/>spectrumUpdated"]
        PCM_SIG["Signal&lt;vector&lt;f32&gt;, u32, u32, u32&gt;<br/>pcmReceived"]
        TRACK_SIG["Signal&lt;&gt;<br/>trackChanged"]
    end
    
    MEDIA_FILE --> PL
    SUNO --> PL
    PL --> ITEMS
    PL -.->|"load on change"| AE
    
    AE --> PLAYER
    PLAYER --> AUDIO_OUT
    PLAYER --> BUFFER_OUT
    
    BUFFER_OUT -->|"onAudioBufferReceived"| AE
    AE -->|"processAudioBuffer"| SCRATCH
    SCRATCH --> ANALYZER
    ANALYZER --> SPECTRUM
    
    AE --> STATE_SIG
    AE --> SPECTRUM_SIG
    AE --> PCM_SIG
    AE --> TRACK_SIG
    
    PL -.->|"persist"| M3U
```

**Sources:** [src/audio/AudioEngine.hpp:1-127](), [src/audio/AudioEngine.cpp:1-279]()

---

## Component Responsibilities

| Component | Primary Responsibilities | Key Types |
|-----------|-------------------------|-----------|
| `AudioEngine` | Playback orchestration, volume control, seeking, signal emission | `PlaybackState`, `Duration` |
| `AudioAnalyzer` | FFT-based frequency analysis, beat detection | `AudioSpectrum` |
| `Playlist` | Track queue management, M3U I/O, shuffle/repeat modes | `PlaylistItem` |
| `QMediaPlayer` | Hardware-accelerated decoding via Qt Multimedia | Qt-owned |
| `QAudioBufferOutput` | Real-time PCM buffer capture for analysis | Qt-owned |

**Sources:** [src/audio/AudioEngine.hpp:26-127](), [AGENTS.md:80-86]()

---

## Audio Processing Pipeline

The audio processing pipeline transforms compressed audio files into frequency spectrum data for visualization. This happens in real-time during playback with minimal latency.

```mermaid
flowchart LR
    subgraph "Input Stage"
        FILE["Audio File<br/>QMediaPlayer::setSource"]
    end
    
    subgraph "Decode Stage"
        DECODER["Qt Multimedia Decoder<br/>Hardware-accelerated"]
        BUFFER["QAudioBuffer<br/>16-bit or 32-bit samples"]
    end
    
    subgraph "Conversion Stage"
        CONVERT["Format Conversion<br/>processAudioBuffer"]
        SCRATCH["scratchBuffer_<br/>f32 normalized samples"]
    end
    
    subgraph "Analysis Stage"
        FFT["AudioAnalyzer::analyze<br/>FFT computation"]
        SPECTRUM_OUT["AudioSpectrum<br/>frequency bins"]
    end
    
    subgraph "Output Stage"
        EMIT_SPECTRUM["spectrumUpdated signal"]
        EMIT_PCM["pcmReceived signal"]
    end
    
    FILE --> DECODER
    DECODER --> BUFFER
    BUFFER -->|"onAudioBufferReceived"| CONVERT
    CONVERT --> SCRATCH
    SCRATCH --> FFT
    FFT --> SPECTRUM_OUT
    SPECTRUM_OUT --> EMIT_SPECTRUM
    SCRATCH --> EMIT_PCM
```

**Sources:** [src/audio/AudioEngine.cpp:191-274]()

---

## Buffer Processing Details

The `processAudioBuffer` method performs zero-allocation audio processing by reusing a pre-allocated `scratchBuffer_`. This method handles multiple sample formats from Qt Multimedia and normalizes them to `f32` values in the range [-1.0, 1.0].

### Sample Format Conversion

| Input Format | Source Type | Conversion Formula | Code Location |
|--------------|-------------|-------------------|---------------|
| `QAudioFormat::Float` | `f32*` | Direct copy (no conversion) | [src/audio/AudioEngine.cpp:250-252]() |
| `QAudioFormat::Int16` | `i16*` | `sample / 32768.0f` | [src/audio/AudioEngine.cpp:254-257]() |
| `QAudioFormat::Int32` | `i32*` | `sample / 2147483648.0f` | [src/audio/AudioEngine.cpp:258-263]() |

### Processing Flow

```mermaid
flowchart TB
    START["onAudioBufferReceived<br/>QAudioBuffer buffer"]
    
    VALIDATE{"buffer.isValid()"}
    
    LOCK["std::lock_guard&lt;std::mutex&gt;<br/>audioMutex_"]
    
    EXTRACT["Extract metadata:<br/>sampleRate, channels,<br/>frameCount, format"]
    
    RESIZE{"scratchBuffer_.size()<br/>&lt; totalSamples?"}
    
    GROW["scratchBuffer_.resize<br/>(totalSamples)"]
    
    CONVERT["Format-specific conversion<br/>to f32 in scratchBuffer_"]
    
    ANALYZE["analyzer_.analyze<br/>(scratchBuffer_, rate, ch)"]
    
    STORE["currentSpectrum_ =<br/>returned spectrum"]
    
    EMIT_SPEC["spectrumUpdated.emitSignal<br/>(currentSpectrum_)"]
    
    EMIT_PCM["pcmReceived.emitSignal<br/>(scratchBuffer_, ...)"]
    
    END["Return"]
    
    START --> VALIDATE
    VALIDATE -->|false| END
    VALIDATE -->|true| LOCK
    LOCK --> EXTRACT
    EXTRACT --> RESIZE
    RESIZE -->|true| GROW
    RESIZE -->|false| CONVERT
    GROW --> CONVERT
    CONVERT --> ANALYZE
    ANALYZE --> STORE
    STORE --> EMIT_SPEC
    EMIT_SPEC --> EMIT_PCM
    EMIT_PCM --> END
```

**Sources:** [src/audio/AudioEngine.cpp:232-274]()

---

## Playback State Management

The `AudioEngine` maintains a simplified three-state model mapped from Qt's `QMediaPlayer::PlaybackState`. State transitions trigger signal emissions that update the UI and visualization systems.

### State Diagram

```mermaid
stateDiagram-v2
    [*] --> Stopped
    
    Stopped --> Playing: play()
    Playing --> Paused: pause()
    Playing --> Stopped: stop()
    Paused --> Playing: play()
    Paused --> Stopped: stop()
    
    Playing --> Stopped: End of track<br/>(autoPlayNext=false)
    Playing --> Playing: End of track<br/>playlist_.next()
    
    note right of Stopped
        player_->stop()
        analyzer_.reset()
    end note
    
    note right of Playing
        bufferReceivedSinceLastCheck_ = false
        Diagnostic timer active
    end note
```

**Sources:** [src/audio/AudioEngine.cpp:147-166](), [src/audio/AudioEngine.cpp:182-189]()

---

## Signal Interface

The `AudioEngine` exposes a signal-based API for reactive integration with UI and visualization components. All signals are instances of the custom `vc::Signal<T>` template, which provides type-safe callback registration without Qt's meta-object overhead for non-QObject classes.

### Signal Types

| Signal | Type Signature | Emission Trigger | Primary Consumer |
|--------|----------------|------------------|------------------|
| `stateChanged` | `Signal<PlaybackState>` | `onPlayerStateChanged` | UI controllers, overlay engine |
| `positionChanged` | `Signal<Duration>` | `onPositionChanged` (from QMediaPlayer) | Progress bar, time display |
| `durationChanged` | `Signal<Duration>` | `onDurationChanged` (from QMediaPlayer) | Progress bar total time |
| `spectrumUpdated` | `Signal<const AudioSpectrum&>` | After `analyzer_.analyze()` | ProjectMBridge visualization |
| `pcmReceived` | `Signal<const vector<f32>&, u32, u32, u32>` | After buffer conversion | VideoRecorder (if recording) |
| `trackChanged` | `Signal<>` | `onPlaylistCurrentChanged` | Metadata display, overlay |
| `errorSignal` | `Signal<std::string>` | `onErrorOccurred` | Error dialog, logging |

**Sources:** [src/audio/AudioEngine.hpp:76-83](), [src/audio/AudioEngine.cpp:32-57]()

---

## Playlist Integration

The `AudioEngine` owns a `Playlist` instance that manages the track queue. When the playlist's current item changes, the engine automatically loads and plays the new track. Playlist state persists to `last_session.m3u` in the config directory for session continuity.

### Track Loading Sequence

```mermaid
sequenceDiagram
    participant User
    participant Playlist
    participant AudioEngine
    participant QMediaPlayer
    
    User->>Playlist: next() / previous() / jumpTo(index)
    Playlist->>Playlist: Update currentIndex_
    Playlist->>AudioEngine: currentChanged signal (index)
    AudioEngine->>AudioEngine: onPlaylistCurrentChanged(index)
    AudioEngine->>AudioEngine: loadCurrentTrack()
    AudioEngine->>Playlist: currentItem()
    Playlist-->>AudioEngine: PlaylistItem*
    AudioEngine->>QMediaPlayer: setSource(QUrl::fromLocalFile)
    AudioEngine->>QMediaPlayer: play()
    QMediaPlayer-->>AudioEngine: playbackStateChanged
    AudioEngine->>AudioEngine: onPlayerStateChanged
    AudioEngine->>User: stateChanged signal
    AudioEngine->>User: trackChanged signal
```

**Sources:** [src/audio/AudioEngine.cpp:59-62](), [src/audio/AudioEngine.cpp:199-213]()

---

## Initialization Sequence

The `AudioEngine::init()` method sets up the Qt Multimedia pipeline and connects all internal signal handlers. This method returns a `Result<void>` to propagate initialization errors.

```mermaid
flowchart TB
    START["AudioEngine::init()"]
    
    CREATE_OUTPUT["audioOutput_ =<br/>make_unique&lt;QAudioOutput&gt;()"]
    
    SET_VOLUME["audioOutput_->setVolume<br/>(volume_)"]
    
    CREATE_PLAYER["player_ =<br/>make_unique&lt;QMediaPlayer&gt;()"]
    
    LINK_OUTPUT["player_->setAudioOutput<br/>(audioOutput_.get())"]
    
    CREATE_BUFFER["bufferOutput_ =<br/>make_unique&lt;QAudioBufferOutput&gt;()"]
    
    LINK_BUFFER["player_->setAudioBufferOutput<br/>(bufferOutput_.get())"]
    
    CONNECT_PLAYER["Connect QMediaPlayer signals:<br/>playbackStateChanged,<br/>positionChanged, etc."]
    
    CONNECT_BUFFER["Connect bufferOutput:<br/>audioBufferReceived"]
    
    CONNECT_PLAYLIST["Connect Playlist signals:<br/>currentChanged, changed"]
    
    LOAD_LAST["loadLastPlaylist()<br/>from last_session.m3u"]
    
    START_TIMER["bufferCheckTimer_.start<br/>(1000ms diagnostic)"]
    
    LOG["LOG_INFO initialization complete"]
    
    RETURN["return Result&lt;void&gt;::ok()"]
    
    START --> CREATE_OUTPUT
    CREATE_OUTPUT --> SET_VOLUME
    SET_VOLUME --> CREATE_PLAYER
    CREATE_PLAYER --> LINK_OUTPUT
    LINK_OUTPUT --> CREATE_BUFFER
    CREATE_BUFFER --> LINK_BUFFER
    LINK_BUFFER --> CONNECT_PLAYER
    CONNECT_PLAYER --> CONNECT_BUFFER
    CONNECT_BUFFER --> CONNECT_PLAYLIST
    CONNECT_PLAYLIST --> LOAD_LAST
    LOAD_LAST --> START_TIMER
    START_TIMER --> LOG
    LOG --> RETURN
```

**Sources:** [src/audio/AudioEngine.cpp:19-81]()

---

## Thread Safety

The `AudioEngine` processes audio buffers on Qt's multimedia thread and emits signals that may be consumed by the visualization thread. A `std::mutex audioMutex_` protects shared state accessed from multiple threads.

### Protected Resources

| Resource | Type | Protected Operations | Access Pattern |
|----------|------|---------------------|----------------|
| `scratchBuffer_` | `std::vector<f32>` | Resize, write, read | Write in `processAudioBuffer`, read in `currentPCM()` |
| `currentSpectrum_` | `AudioSpectrum` | Write after analysis, read | Write in `processAudioBuffer`, read in `currentSpectrum()` |
| `analyzer_` internal state | `AudioAnalyzer` | FFT computation | Write in `processAudioBuffer`, read in `currentPCM()` |

**Sources:** [src/audio/AudioEngine.hpp:123](), [src/audio/AudioEngine.cpp:236](), [src/audio/AudioEngine.cpp:67-73]()

---

## Diagnostic Features

The audio system includes a diagnostic timer that checks whether audio buffers are being received during playback. This helps identify issues with `QAudioBufferOutput` functionality, which can fail silently on some platforms.

### Buffer Reception Monitoring

The `bufferCheckTimer_` runs every 1000ms while the engine is active. If the engine is in the `Playing` state but no buffers have been received since the last check, a warning is logged. This diagnostic proved essential during development when Qt Multimedia's buffer output failed without error messages.

```cpp
// Diagnostic pattern from src/audio/AudioEngine.cpp:68-76
connect(&bufferCheckTimer_, &QTimer::timeout, this, [this]() {
    if (state_ == PlaybackState::Playing && !bufferReceivedSinceLastCheck_) {
        LOG_WARN("AudioEngine: No audio buffers received in last 1000ms - "
                 "QAudioBufferOutput may not be working");
    }
    bufferReceivedSinceLastCheck_ = false;
});
```

**Sources:** [src/audio/AudioEngine.cpp:68-77](), [src/audio/AudioEngine.cpp:191-193]()

---

## Integration Points

The Audio System interfaces with several other subsystems to provide a cohesive playback and visualization experience.

### Downstream Consumers

```mermaid
graph LR
    AE["AudioEngine"]
    
    VIZ["ProjectMBridge<br/>(Visualization)"]
    REC["VideoRecorder<br/>(Recording)"]
    OVERLAY["OverlayEngine<br/>(Metadata Display)"]
    UI["AudioController<br/>(UI Updates)"]
    SUNO["SunoController<br/>(Playlist Integration)"]
    
    AE -->|"spectrumUpdated<br/>AudioSpectrum"| VIZ
    AE -->|"pcmReceived<br/>f32 samples"| REC
    AE -->|"trackChanged<br/>stateChanged"| OVERLAY
    AE -->|"positionChanged<br/>durationChanged<br/>stateChanged"| UI
    AE <-->|"playlist() access<br/>add tracks"| SUNO
```

**Sources:** [src/audio/AudioEngine.hpp:76-83](), [AGENTS.md:80-86]()

---

## Configuration

The Audio System reads configuration from the `AudioConfig` section of the global `CONFIG` singleton. Settings are persisted to `config.toml` in the user's config directory.

### Audio Configuration Parameters

| Parameter | Type | Purpose | Default |
|-----------|------|---------|---------|
| `volume` | `f32` | Initial playback volume (0.0-1.0) | 1.0 |
| `autoPlayNext` | `bool` | Automatically play next track on track end | `true` |
| `lastPlaylistPath` | `std::string` | Path to `last_session.m3u` | `~/.config/.../last_session.m3u` |

**Sources:** [src/audio/AudioEngine.cpp:215-230](), [AGENTS.md:80]()

---

## Error Handling

All initialization operations return `Result<void>` types to propagate errors without exceptions. Runtime errors from Qt Multimedia are captured via the `QMediaPlayer::errorOccurred` signal and converted to `std::string` messages emitted through the `errorSignal` signal.

### Error Flow

```mermaid
flowchart LR
    QMP["QMediaPlayer::errorOccurred<br/>(QMediaPlayer::Error, QString)"]
    SLOT["AudioEngine::<br/>onErrorOccurred"]
    LOG["LOG_ERROR<br/>(formatted message)"]
    EMIT["errorSignal.emitSignal<br/>(std::string)"]
    UI["Error dialog in UI"]
    
    QMP --> SLOT
    SLOT --> LOG
    SLOT --> EMIT
    EMIT --> UI
```

**Sources:** [src/audio/AudioEngine.cpp:176-180](), [AGENTS.md:54-65]()