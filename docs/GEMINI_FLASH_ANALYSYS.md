An in-depth architectural and functional investigation of the `main` branch of the **[chadvis-projectm-qt](https://github.com/Nsomnia/chadvis-projectm-qt)** repository (authored by *Nsomnia* / Derek Vanee) details how each major subsystem is structured and implemented across the codebase.

---

### 1. High-Level Architectural Overview

**ChadVis** is designed as a high-performance native desktop workstation built with **C++23** and **Qt 6 (QtQuick/QML)**. It serves as a bridge between remote cloud AI music services (specifically Suno.com) and a local multimedia engine featuring **libprojectM v4** (Milkdrop preset visualizer), an **FFmpeg recording pipeline**, an **offline SQLite library**, and a **karaoke/lyrics engine**.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Qt 6 / QML Shell & UI                           │
│   (NavigationRail, Scene Editor, Preset Browser, LLM Chat, Player)     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ QML / C++ Bridges
┌───────────────────────────────────▼────────────────────────────────────┐
│                       Core C++23 Workstation                           │
│ ┌───────────────────┐ ┌────────────────────┐ ┌───────────────────────┐ │
│ │   Suno Client     │ │ projectM v4 Engine │ │   FFmpeg Recorder     │ │
│ │ • Auth / Keychain │ │ • OpenGL / PBO Grab│ │ • AudioQueue Sync     │ │
│ │ • Feed/v3 Cursor  │ │ • Preset Manager   │ │ • NVENC/VAAPI/SW H264 │ │
│ │ • Reverse API docs│ │ • Audio Reactivity │ │ • Batch Video Render  │ │
│ └─────────┬─────────┘ └─────────┬──────────┘ └───────────┬───────────┘ │
│           │                     │                        │             │
│ ┌─────────▼─────────┐ ┌─────────▼──────────┐ ┌───────────▼───────────┐ │
│ │  SQLite Database  │ │    Lyrics / Word   │ │   LLM Lyric Assistant │ │
│ │ • Offline Cache   │ │ • Suno Timestamped │ │ • System Prompts      │ │
│ │ • 5-Star Ratings  │ │ • Whisper Stems/LRC│ │ • Metatag Structuring │ │
│ └───────────────────┘ └────────────────────┘ └───────────────────────┘ │
└────────────────────────────────────────────────────────────────────────┘
```

#### Key Technical Principles Enforced in the Repo:
* **Modern C++23 Standards**: Heavy use of modern language features, standard library concepts, and strict linting/formatting (`.clang-tidy`, `.clang-format`, `.clangd`, and the `skills/cpp-coding-standards/` module).
* **Monadic Error Handling**: Strict prohibition of unhandled runtime exceptions in favor of value-based monadic error propagation via `vc::Result<T>`.
* **Modular CMake**: Modularized build configuration under `cmake/` (e.g., `cmake/Dependencies.cmake`), building cleanly on Linux (Arch-centric) and macOS (v14+) via `./build.sh`.
* **Fail-Closed API Interaction**: Because Suno publishes no official public API, endpoints derived via network capture are maintained in a research corpus under `docs/suno_api/` marked with verification flags (`[T1]`, `[LEAD]`, `[VERIFY]`). Risky endpoints (such as billing or unverified generation calls) are deliberately runtime-gated or disabled to protect user accounts and credits.

---

### 2. Suno Remote Integration & Reverse-Engineered Client

#### A. API Documentation and Provenance (`docs/suno_api/`)
The repo includes reverse-engineered documentation of the Suno studio API:
* **Endpoint Inventory (`docs/suno_api/ENDPOINT-INVENTORY.md`)**: Catalogs endpoints for authentication, clip feeds (`/api/feed/v2`, `/api/feed/v3`), stem separation, session renewals, and billing.
* **OAuth Gate (`docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md`)**: Documents Suno’s Clerk/OAuth authentication flow.

#### B. Client Implementation (`src/suno/`, `src/auth/`)
* **`SunoClient`**: Manages API calls, headers (browser spoofing/session cookies), and communicates with the system credential store/keychain for secure token persistence.
* **`ClipParser` & Cursor Contract (`feed/v3`)**: Ingests song clips, parsing title, audio URL, artwork URL, duration, genre tags, model version (v3.5, v4, etc.), prompt metadata, and stem metadata into C++ domain structs.
* **Safe Consumption Stance**: As documented by the author, generation triggers from the desktop client remain heavily restricted to prevent unexpected account flagging, focusing instead on read-back, library ingestion, and batch playback/export.

---

### 3. Visualizer Engine: projectM v4 Integration (`src/visualizer/`)

* **Milkdrop Compatibility**: Integrates `libprojectM` v4.x (using `projectm-eval` and OpenGL rendering pipelines) to render Milkdrop presets.
* **Zero-Copy Frame Acquisition**: Leverages OpenGL Pixel Buffer Objects (PBO) to facilitate zero-copy asynchronous GPU readback of rendered visualizer frames directly into system memory for the recording pipeline.
* **Preset Navigation**: Supports preset switching, random/shuffle mode, locking presets to a specific track, preset rating storage, and compatibility with the Jason Fletcher "Cream of the Crop" and original Milkdrop 1 & 2 preset collections.
* **Audio Reactivity**: Feeds live audio PCM data (via system audio or the internal player) into projectM’s FFT and beat-detection modules.

---

### 4. Batch Video Recorder & FFmpeg Pipeline (`src/recorder/`)

* **FFmpeg Direct C API**: Links natively against `libavcodec`, `libavformat`, `libavutil`, `libswscale`, and `libswresample`.
* **Hardware Acceleration Infrastructure (`EncoderSettings.hpp`, `VideoRecorderFFmpeg.cpp`)**:
  * Carries enum structures for `NVENC`, `VAAPI`, `QuickSync`, and `AMF`.
  * Code paths implement `av_hwdevice_ctx_create` and hardware frame allocations.
  * Dynamic encoder discovery uses `avcodec_find_encoder_by_name` (e.g., looking up `h264_nvenc` or `hevc_nvenc`).
  * Defaults safely to software H.264 if hardware codecs are missing from the host FFmpeg build.
* **`AudioQueue` Synchronization**: A dedicated audio FIFO queue buffers PCM audio data in lockstep with visualizer video frames, preventing A/V drift during offline or faster-than-real-time batch rendering.
* **Batch Music Video Mode**: Users can select lists of Suno remote clips or local audio files, bind a preset or scene sequence, and export `.mp4` music videos unattended.

---

### 5. Scene Editor & Animated Overlays

* **Textual and Graphical Compositor**: A graphical layer sitting atop the projectM viewport designed to render animated textual sequences, ID3 tag data, and custom vector elements.
* **Customization Controls**: Configurable layout, position anchoring, typography, font metrics, letter spacing, entry/exit animations, and overlay opacity.
* **Batch Application**: Scenes function as templates that dynamically bind track metadata (e.g., `{title}`, `{artist}`, `{style}`, `{prompt}`) across hundreds of queued audio files during video rendering.
* **UI Structure**: Built in QtQuick/QML, utilizing modern responsive components (a unified Navigation Rail shell, floating panels, and `ComingSoonPage` placeholders for components under active refactoring).

---

### 6. Word-Synchronized Karaoke Client & Whisper Integration (`src/lyrics/`)

* **Suno Synchronized Lyric Ingestion**: Ingests word-level timestamp payloads returned by Suno’s API, parsing word offsets into timeline events.
* **Real-Time Karaoke Rendering**: The QML lyric view animates lyrics in real time, highlighting spoken/sung words in sync with audio position.
* **Whisper Client**: Included to handle local audio files or Suno tracks that lack pre-computed timestamps. Audio tracks can be processed through a Whisper speech-to-text pipeline to generate time-aligned lyrics (LRC/SRT format) locally.
* **Format Parsers**: Supports standard LRC, SRT, and Suno-specific JSON lyric structures.

---

### 7. Integrated LLM Lyric Writing Assistant

* **Conversational AI Shell**: A dedicated chat view designed to interface with LLM endpoints (via OpenAI/Anthropic/local Ollama-compatible APIs).
* **System Prompting for Suno (`GENERAL_LLM_STARTING_PROMPT.md`)**:
  * Employs specialized system instructions guiding the model to generate lyrics formatted specifically for Suno’s generation engine.
  * Incorporates structural metatags (`[Verse]`, `[Chorus]`, `[Pre-Chorus]`, `[Bridge]`, `[Guitar Solo]`, `[Drop]`, `[Outro]`).
  * Assists in formulating style descriptors, genre blending, BPM pacing, and acoustic instructions.

---

### 8. Local Library Management & Star Rating System (`src/db/`)

* **Offline SQLite Database**: An embedded SQLite instance caches remote Suno clips and catalogs local directories.
* **Song Rating System**: Users can assign granular 1-to-5 star ratings to Suno tracks and local files. This allows sorting and filtering (e.g., separating "keepers" from duds in large batch generation dumps).
* **Dual Library View**: Seamlessly merges remote cloud tracks (cached with offline artwork and stream links) and local files into searchable, filterable playlists.

---

### 9. Current Status of the Codebase

1. **Active Architectural Refactoring**: The repo is undergoing active reorganization, moving monolithic C++ blocks into cleanly decoupled CMake modules, standardizing on modern C++23 conventions, and stripping dead code.
2. **Defensive API Approach**: Rather than serving as an unauthorized automated generation bot, ChadVis positions itself primarily as a **power-user client shell, library organizer, visualizer, and production studio** for tracks already associated with the user's account.
3. **Recording Nuance**: While full NVENC/VAAPI device-creation code exists in `src/recorder/`, it relies on the user's host FFmpeg installation possessing those capabilities, falling back gracefully to software encoding.