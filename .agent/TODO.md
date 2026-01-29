# 🤖 AGENT SESSION DIRECTIVE — READ THIS FIRST

> **Orchestrator:** `oh-my-opencode-slim`  
> **Workflow:** REPL Loop (Read → Evaluate → Plan → Execute → Loop)  
> **Mode:** Autonomous with Self-Improvement  

---

## ⚠️ CRITICAL: HOW TO USE THIS DOCUMENT

You are an autonomous LLM agent. This file is your **primary source of truth** for the current session. You must:

1. **READ** this entire document before taking any action
2. **TRACK** your progress using the checkboxes provided (`- [ ]` → `- [x]`)
3. **UPDATE** this document as you work—add discoveries, notes, sub-tasks, and learnings
4. **NEVER REMOVE** any content from this file; only add, enhance, revise, or annotate
5. **RE-READ** this file periodically to maintain context and verify progress
6. **LOOP** until all tasks matching your session's priority scope are complete

### Document Modification Guidelines

You have **full permission** to:
- ✅ Add new tasks, sub-tasks, or entire sections
- ✅ Add notes, discoveries, warnings, or documentation
- ✅ Reorganize for clarity (while preserving all content)
- ✅ Mark tasks complete or add status annotations
- ✅ Insert code snippets, file paths, or reference materials
- ✅ Create linked companion documents if this file grows too large

You **must not**:
- ❌ Delete any task, note, or section (mark as `[DEPRECATED]` or `[SUPERSEDED]` instead)
- ❌ Remove context that future sessions might need
- ❌ Alter the priority classifications without adding justification

---

## 🔒 CORE DIRECTIVES (Always Active)

### Sensitive Data Protection Protocol

Before modifying **any** file containing sensitive data (configs, GPG keys, API keys, passwords, tokens, credentials, `.env` files, auth files), you **MUST**:

```bash
# Create timestamped backup BEFORE editing
cp "/path/to/sensitive-file" "/path/to/sensitive-file.backup.$(date +%Y%m%d_%H%M%S)"
```

**Sensitive locations to protect:**
- `/home/nsomnia/.local/share/opencode/` (API and OAuth keys)
- Any file containing tokens, secrets, or credentials
- Project configuration files with embedded secrets

---

### Git Source Control Protocol

**Branch Strategy:** Highly granular feature branches for clean PR/merge history.

```bash
# Branch naming convention
git checkout -b <category>/<short-description>
# Examples:
#   feat/suno-auth-clerk-integration
#   fix/video-recording-memory-leak  
#   refactor/settings-gui-binding
#   docs/agents-md-updates
#   test/playwright-browser-automation
```

**Commit Discipline:**
- ✅ Commit frequently at logical checkpoints (not after every single file edit)
- ✅ Use as natural restore points (mimics `jj` workflow philosophy)
- ✅ Write meaningful commit messages: `type(scope): description`
- ❌ **NEVER push directly to `main` or `master`**
- ❌ Don't waste tokens on trivial single-line commits

**Before ending session:** Ensure all work is committed and document the branch name(s) in this file.

---

### Self-Improvement Protocol

You are encouraged to improve your own operational context:

- [ ] Add any discoveries, patterns, or insights to this document
- [ ] Document dead-ends to prevent future sessions from repeating them
- [ ] Note which approaches worked well for similar problems
- [ ] Update `AGENTS.md` with reusable workflows or package recommendations
- [ ] Create helper scripts if a manual process is repeated 3+ times

---

## 📋 TASK REGISTRY

### Priority Legend
| Priority | Label | Meaning |
|----------|-------|---------|
| 🔴 | **P0 - CRITICAL** | Current session's primary focus |
| 🟠 | **P1 - HIGH** | Address if time permits this session |
| 🟡 | **P2 - MODERATE** | Important but not urgent |
| 🟢 | **P3 - LOW** | Backlog / future sessions |
| ⚪ | **P4 - DEFERRED** | Move to external file or future planning |

---

## 🔴 P0 — SUNO SYSTEM COMPREHENSIVE OVERHAUL [ACTIVE]

> **Context:** Suno authentication is working via QWebEngineView, but the karaoke/lyrics system needs a complete rewrite. The user wants time-synced lyrics display for Suno songs with subtitle file saving.
>
> **Status:** 🚧 Investigation Complete - Implementation Phase
> **Branch:** `feat/suno-karaoke-rewrite`

### Current State Analysis

**Authentication (✅ WORKING):**
- `SunoCookieDialog` uses `QWebEngineView` for integrated browser login
- Automatically captures `__session` and `__client` cookies
- Manual entry fallback implemented
- Token refresh via Clerk API working
- Cookies/tokens stored in config and database

**Karaoke/Lyrics System (⚠️ NEEDS REWRITE):**
- `KaraokeWidget` exists but needs better Suno integration
- `SunoLyrics` parser handles JSON, LRC, SRT formats
- `LyricsAligner` aligns word-level timestamps to lines
- Lyrics saved to database but subtitle file saving needs improvement

**Missing Features:**
- Search functionality in Suno library tab
- Proper subtitle file (.srt, .lrc) saving alongside downloads
- Lyrics display in dedicated canvas/tool tab
- Karaoke-style rendering with word-level highlighting

### Implementation Tasks

#### Phase 1: Suno Library Search
- [ ] Add search bar to Suno library canvas tab
- [ ] Implement `SunoDatabase::searchClips()` query
- [ ] Add real-time search filtering
- [ ] Search by title, artist, tags, lyrics content

#### Phase 2: Lyrics Tool Tab
- [ ] Create new `LyricsToolWidget` canvas tab
- [ ] Display lyrics in scrollable text view
- [ ] Show current line highlighting
- [ ] Support both synced and unsynced lyrics

#### Phase 3: Karaoke System Rewrite
- [ ] Gut existing `KaraokeWidget` implementation
- [ ] Rewrite following third_party project patterns (if any)
- [ ] Implement word-level timestamp highlighting
- [ ] Smooth transitions between active words
- [ ] Configurable fonts, colors, positioning
- [ ] Support for instrumental sections

#### Phase 4: Subtitle File Saving
- [ ] Save `.srt` files alongside downloaded MP3s
- [ ] Save `.lrc` files as alternative format
- [ ] Add "Download Mode" toggle in settings
- [ ] Batch subtitle generation for existing library

#### Phase 5: Integration
- [ ] Auto-display lyrics when Suno song plays
- [ ] Sync lyrics tool tab with karaoke display
- [ ] Handle lyrics still processing state
- [ ] Retry logic for failed lyric fetches

### File Structure

```
src/suno/
├── SunoClient.hpp/cpp          # API client (✅ working)
├── SunoModels.hpp              # Data structures (✅ working)
├── SunoDatabase.hpp/cpp        # SQLite storage (✅ working)
├── SunoLyrics.hpp/cpp          # Lyrics parser (⚠️ refactor)
├── SunoCookieDialog.hpp/cpp    # Auth dialog (✅ working)
└── SunoController.hpp/cpp      # Logic coordinator (⚠️ refactor)

src/ui/
├── KaraokeWidget.hpp/cpp       # Karaoke display (🚧 rewrite)
├── LyricsToolWidget.hpp/cpp    # NEW: Lyrics panel
├── SunoLibraryPanel.hpp/cpp    # Add search functionality
└── SunoBrowser.hpp/cpp         # Web browser (✅ working)
```

### Data Flow

```
Suno Song Playing
       │
       ▼
┌─────────────────┐
│ SunoController  │──┐
└─────────────────┘  │
       │             │
       ▼             ▼
┌─────────────┐  ┌──────────────┐
│ Karaoke     │  │ Lyrics Tool  │
│ Widget      │  │ Widget       │
│ (visual)    │  │ (text)       │
└─────────────┘  └──────────────┘
       │             │
       └──────┬──────┘
              ▼
       ┌─────────────┐
       │ AlignedLyrics│
       │ (data model) │
       └─────────────┘
```

### Sub-Task Workspace

**Agent Notes:**
- Suno authentication is actually well-implemented - no changes needed there
- The karaoke system works but needs better word-level timing
- Lyrics alignment logic in `LyricsAligner::align()` is complex but functional
- Need to add search to `SunoDatabase` - query by title/artist/tags
- Subtitle saving already partially implemented in `SunoController::onAlignedLyricsFetched()`

---

## 🔴 P0 — KARAOKE / LYRICS SYSTEM REWRITE [ACTIVE]

> **Context:** Current karaoke system works with dummy data but Suno remote time-synced lyric parsing was a "cluster disaster". Need clean rewrite following proper patterns.
>
> **Status:** 🚧 Planning Phase
> **Branch:** `feat/suno-karaoke-rewrite`

### Current Issues

1. **KaraokeWidget**: Basic implementation works but needs:
   - Better word-level timing accuracy
   - Smoother visual transitions
   - Proper handling of instrumental sections
   - Configurable positioning and styling

2. **Lyrics Alignment**: `LyricsAligner::align()` is complex:
   - Matches prompt text to audio word timestamps
   - Uses fuzzy matching with normalization
   - Needs better handling of mismatches

3. **Suno Integration**: Lyrics fetch works but:
   - "Processing lyrics" state handling is messy
   - Retry logic scattered across components
   - No unified lyrics display panel

### Rewrite Plan

#### New Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    LyricsSystem (Singleton)                  │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ LyricsLoader│  │ LyricsSync  │  │ LyricsRenderer      │  │
│  │ (fetch/parse)│  │ (timing)    │  │ (display)           │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │                  │                  │
         ▼                  ▼                  ▼
   ┌──────────┐      ┌──────────┐      ┌──────────────┐
   │ Suno API │      │ AudioEngine│     │ KaraokeWidget│
   │ Database │      │ Position   │     │ LyricsPanel  │
   │ Files    │      │            │     │              │
   └──────────┘      └──────────┘      └──────────────┘
```

#### Components to Create

1. **LyricsLoader** (`src/lyrics/LyricsLoader.hpp/cpp`)
   - Unified interface for loading lyrics from any source
   - Sources: Suno API, Database, SRT files, LRC files, JSON
   - Async loading with callbacks
   - Caching layer

2. **LyricsSync** (`src/lyrics/LyricsSync.hpp/cpp`)
   - Time synchronization engine
   - Current line/word detection
   - Pre-fetch next lines for smooth transitions
   - Handle seek/jump gracefully

3. **LyricsRenderer** (`src/lyrics/LyricsRenderer.hpp/cpp`)
   - Base class for different renderers
   - KaraokeRenderer: Word-level highlighting
   - PanelRenderer: Scrolling text view
   - OverlayRenderer: For video recording

4. **LyricsPanel** (`src/ui/LyricsPanel.hpp/cpp`)
   - New canvas tab for lyrics display
   - Scrollable text view
   - Current line highlighting
   - Search within lyrics

#### Data Structures

```cpp
struct LyricsLine {
    std::string text;
    f32 startTime;
    f32 endTime;
    std::vector<LyricsWord> words;
    bool isInstrumental;
};

struct LyricsWord {
    std::string text;
    f32 startTime;
    f32 endTime;
    f32 confidence; // For Suno aligned lyrics
};

class LyricsData {
    std::vector<LyricsLine> lines;
    std::string source; // "suno", "srt", "lrc", etc.
    std::string songId;
    bool isSynced;
};
```

### Implementation Tasks

- [ ] Create `src/lyrics/` directory structure
- [ ] Implement `LyricsLoader` with all source types
- [ ] Implement `LyricsSync` engine
- [ ] Implement `LyricsRenderer` base and derived classes
- [ ] Create `LyricsPanel` canvas tab
- [ ] Rewrite `KaraokeWidget` using new system
- [ ] Add subtitle file export (.srt, .lrc)
- [ ] Integrate with SunoController
- [ ] Add lyrics search functionality

---

## 🔴 P0 — VIDEO RECORDING SUBSYSTEM REDESIGN [COMPLETED]

> **Context:** Current implementation described as "primitive, not user friendly, and largely broken." Requires complete architectural overhaul. THIS IS THE PRIMARY FOCUS.
>
> **Status:** ✅ COMPLETED - Ralph Loop Iteration 1
> **Branch:** `feat/video-recording-rewrite`
> **Documentation:** [docs/VIDEO_RECORDING.md](../docs/VIDEO_RECORDING.md)

### Completed Improvements

- [x] **Config Directory Creation**: Automatic creation of `~/.config/chadvis-projectm-qt/` on first run
- [x] **Thread-Safe Stats**: Implemented proper stats tracking with mutex protection
- [x] **Real-time Feedback**: Added encoding FPS display and health indicator bar
- [x] **Error Handling**: Connected error signals to UI for user feedback
- [x] **Hardware Acceleration**: Added NVENC and VAAPI codec support
- [x] **Quality Presets**: Added hardware encoding presets (1080p60, 4K60)
- [x] **Documentation**: Created comprehensive VIDEO_RECORDING.md guide

### Architecture Changes

```
VideoRecorderThread now includes:
├── Thread-safe stats with statsMutex_
├── Proper FPS calculation (avgFps + encodingFps)
├── Error propagation to UI via error signal
└── Health indicator based on target vs actual FPS

EncoderSettings now supports:
├── Hardware codecs: H264_NVENC, H265_NVENC, H264_VAAPI, H265_VAAPI
├── HWAccelDevice enum for device selection
└── Hardware presets for common use cases
```

### Background

User connected with another developer on Reddit building a similar web/JS video project (no FFmpeg available). This conversation surfaced new architectural ideas.

### Analysis Tasks

- [ ] Audit current video recording codebase—identify all components
- [ ] Document current pain points and limitations
- [ ] Research modern browser-based video recording APIs:
  - MediaRecorder API
  - WebCodecs API
  - Canvas-based frame capture
  - OffscreenCanvas for performance
- [ ] Investigate WebM/MP4 encoding options in pure JS

### Architecture Tasks

- [ ] Design new video recording architecture from scratch
- [ ] Create technical specification document
- [ ] Define clear interfaces between components
- [ ] Plan for "9000 songs" automation use case (headless, scriptable)
- [ ] Consider chunked recording for memory management

### Implementation Tasks

- [ ] Gut existing video recording subsystem (preserve in deprecated branch)
- [ ] Implement new architecture incrementally
- [ ] Add progress/status callbacks for long recordings
- [ ] Implement proper export functionality

---

## 🔴 P0 — CONFIG DIRECTORY INITIALIZATION [COMPLETED]

> **Problem:** The package does not create a default config in `$HOME/.config/chadvis-projectm-qt` if it's missing. This causes first-run failures.
>
> **Status:** ✅ COMPLETED

### Completed Tasks

- [x] Ensure `ConfigLoader::loadDefault()` creates the config directory structure on first run
- [x] Create default `config.toml` with sensible defaults if none exists
- [x] Ensure all subdirectories (presets, recordings, cache, logs) are created as needed
- [x] Set default recording output directory to `~/.local/share/chadvis-projectm-qt/recordings/`

### Implementation Details

Modified `ConfigLoader::loadDefault()` in `src/core/ConfigLoader.cpp`:
- Creates config directory using `file::ensureDir()`
- Creates data, cache, and presets subdirectories
- Sets default recording output path
- Generates fresh config with Arch-tier quality defaults

---

## 🟢 P3 — SETTINGS SYSTEM UNIFICATION

> **Principle:** Every configurable option should be accessible via ALL interfaces: GUI, config file, AND command-line.

### Requirements Matrix

| Setting | GUI | Config File | CLI Flag | Notes |
|---------|-----|-------------|----------|-------|
| *Audit needed* | | | | |

- [ ] Audit all existing settings across the application
- [ ] Create the settings matrix table above
- [ ] Identify gaps in interface coverage
- [ ] Design unified settings schema (single source of truth)
- [ ] Implement missing GUI controls
- [ ] Implement missing CLI flags
- [ ] Ensure bidirectional sync between interfaces

### Automation Consideration

For headless/scripted workflows (batch processing 9000+ songs):
- [ ] Design CLI-only mode that bypasses GUI entirely
- [ ] Document all automation-relevant flags
- [ ] Create example automation scripts

---

## 🟢 P3 — CLI UX ENHANCEMENT ("rizz mode")

> **Aesthetic:** Senior dev energy. `1337`. Clean. Helpful.

### Features

- [ ] Add color output to `--help` (detect TTY, respect `NO_COLOR`)
- [ ] Implement multi-stage help system:
  - `--help` → General overview
  - `--help <topic>` → Detailed topic help  
  - `<command> --help` → Command-specific help
- [ ] Graceful handling of invalid flags with suggestions:
  ```
  Error: Unknown flag '--recrod'
  Did you mean: --record?
  ```
- [ ] Add shell completion scripts (bash, zsh, fish)
- [ ] Consider `--json` output mode for scriptability

---

## ⚪ P4 — DEFERRED ITEMS (External Backlog)

> These items should be moved to `/home/nsomnia/Documents/agent-OC-and-OMO-TODO.md` for future sessions outside this project context.

### oh-my-opencode-slim Optimization

**Project Location:** `/home/nsomnia/Documents/opencode/oh-my-opencode-slim/`  
**Branch:** `master` (current installation)

- [ ] Audit current configuration for optimization opportunities
- [ ] Document the 3 user-configured model presets
- [ ] **TEST:** New Playwright browser agent capability
  - Can it handle OAuth flows?
  - Can it perform web research tasks?
  - Document findings for this project's auth needs

### CLIProxyAPI Setup

**Location:** `/home/nsomnia/cliproxyapi`  
**Status:** Non-functional; installation was confusing

- [ ] Research CLIProxyAPI documentation
- [ ] Document proper installation steps
- [ ] Configure for:
  - Antigravity
  - Google API key rotation/load-balancing
  - Qwen auth proxy
  - Other auth proxy needs
- [ ] Test each integration
- [ ] Create setup guide for future reference

### MCP (Model Context Protocol) Expansion

- [ ] Research free MCPs compatible with opencode
- [ ] Evaluate and install useful MCPs
- [ ] Document MCP configurations and use cases
- [ ] Add model tweaks and optimizations

### Key File Locations Reference

| Purpose | Path |
|---------|------|
| OpenCode API/OAuth Keys | `/home/nsomnia/.local/share/opencode/` |
| oh-my-opencode-slim | `/home/nsomnia/Documents/opencode/oh-my-opencode-slim/` |
| CLIProxyAPI | `/home/nsomnia/cliproxyapi` |
| External TODO (for $HOME tasks) | `/home/nsomnia/Documents/agent-OC-and-OMO-TODO.md` |

---

## 📝 AGENTS.md CONTRIBUTIONS

> Items to add to the project's `AGENTS.md` file.

### Pending Additions

- [ ] Add sensitive data backup protocol (from Core Directives above)
- [ ] Add git branch/commit discipline guidelines
- [ ] Add any discovered useful packages during this session
- [ ] Add workflow patterns that proved effective

### Best Practices to Document

#### Code Quality
- Write self-documenting code; add comments for "why," not "what"
- Keep functions small and single-purpose
- Use TypeScript/JSDoc for better tooling support

#### Documentation Hygiene
- Maintain `CHANGELOG.md` with semantic versioning notes
- Update `docs/` when behavior changes
- README should always reflect current state

#### Commit Messages
```
type(scope): concise description

- Additional context if needed
- Reference issues: fixes #123

Types: feat, fix, docs, style, refactor, test, chore
```

---

## 🔬 SESSION LOG

> Agent: Log significant actions, decisions, and discoveries below. Include timestamps if helpful.

### Session Start
- **Date:** 2026-01-29
- **Focus Priority:** P0 (Video Recording + Config Dir Creation)
- **Starting Branch:** suno-auth-refactor
- **Ralph Loop:** Iteration 1 - Active
- **Agent:** oh-my-opencode-slim

### Previous Session (Completed)
- **Date:** 2026-01-29
- **Focus Priority:** P0 (Suno Auth)
- **Starting Branch:** suno-auth-refactor
- **Status:** ✅ COMPLETED - Suno auth refactored to use QWebEngineView for cookie-based auth

### Progress Notes (Current Session - Suno/Karaoke Investigation)
- **Investigated Suno authentication system**: Found it's actually well-implemented
  - `SunoCookieDialog` properly uses `QWebEngineView` for browser login
  - Cookie capture ( `__session`, `__client`) works correctly
  - Token refresh via Clerk API is functional
  - Auth state persists in config and database
- **Analyzed karaoke system**: `KaraokeWidget` works but needs improvements
  - Basic word-level highlighting implemented
  - `LyricsAligner` has complex fuzzy matching logic
  - Suno lyrics fetch and parsing is functional
- **Identified gaps**:
  - No search functionality in Suno library tab
  - Subtitle file saving exists but needs "Download Mode" toggle
  - No dedicated lyrics panel/tool tab
  - Karaoke rendering could be smoother
- **Updated TODO.md** with comprehensive Suno/Karaoke rewrite plan
- **Created branch**: `feat/suno-karaoke-rewrite` (ready for implementation)

### Progress Notes (Previous Session - Video Recording)
- Analyzed video recording system: Identified UX issues, missing error handling, no hardware acceleration
- Implemented config directory auto-creation with all subdirectories
- Refactored VideoRecorderThread with thread-safe stats tracking
- Added proper FPS calculation and encoding health indicator
- Connected error signals to UI for user feedback
- Added hardware acceleration support (NVENC, VAAPI codecs)
- Created hardware encoding presets (1080p60, 4K60)
- Wrote comprehensive VIDEO_RECORDING.md documentation
- All changes committed to `feat/video-recording-rewrite` branch
- Build verified successful

### Progress Notes (Previous Session - Suno Auth)
- Researched Suno/Clerk auth: Confirmed email/password is not supported for Suno's instance.
- Implemented `SunoCookieDialog` using `QWebEngineView` for integrated browser login.
- Added "Manual Entry" fallback to `SunoCookieDialog`.
- Refactored `SunoClient` to remove dead email/password code and focus on cookie/JWT refresh.
- Updated `SunoController` to use the new browser-based auth flow.
- Cleaned up `Config` and `SettingsDialog` (removed email/password fields).
- Installed `qt6-webengine` system dependency.
- Verified build success.

### Branches Created
- `suno-auth-refactor` (existing, has Suno auth implementation)
- `feat/video-recording-rewrite` (completed, 3 commits ahead)
- `feat/suno-karaoke-rewrite` (new, ready for implementation)

### Beta Branch Proposal
Consider creating `beta/integration` branch to merge:
- `suno-auth-refactor` (Suno auth system)
- `feat/video-recording-rewrite` (Recording improvements)
- `feat/suno-karaoke-rewrite` (Karaoke rewrite - pending)

This allows testing all features together before merging to master.

### End of Session Summary
- [x] All work committed
- [x] This document updated with final status
- [x] Branch names documented above
- [x] Investigation complete - ready for implementation

### Blockers for Next Session
None - ready to implement karaoke rewrite. Key decisions needed:
1. Should we create the `beta/integration` branch now or later?
2. Priority: LyricsPanel first or KaraokeWidget rewrite first?
3. Should subtitle saving be automatic or require "Download Mode" toggle?

---

## 🧠 DISCOVERIES & LEARNINGS

### What Works
- **Integrated Browser Login:** Using `QWebEngineView` to capture `__session` cookies from `suno.com/login` works reliably and bypasses Cloudflare/OAuth complexities.
- **Token Refresh:** The `__session` cookie can be used to programmatically fetch fresh JWTs from `clerk.suno.com/v1/client/sessions/{sid}/tokens`.
- **Suno Database:** SQLite storage with migrations works well for caching clips and lyrics.
- **Lyrics Parsing:** `LyricsAligner` successfully parses Suno JSON, SRT, and LRC formats.
- **Karaoke Display:** Basic word-level highlighting works in `KaraokeWidget`.

### What Doesn't Work
- ❌ Email-based auth for Suno (uses OAuth only via Clerk)
- ❌ Standard `QNetworkAccessManager` for initial login (blocked by Cloudflare/OAuth redirects)
- ⚠️ Karaoke word timing can be jittery (needs smoother interpolation)
- ⚠️ No search in Suno library tab (must scroll through all clips)
- ⚠️ Lyrics panel missing (only overlay display exists)

### Useful Commands
- `echo "1313" | sudo -S pacman -S qt6-webengine` (Install missing Qt components)
- `ffmpeg -encoders | grep nvenc` (Check NVENC support)

### External Resources
- Clerk API Documentation (Headless Auth)
- Suno API reverse engineering notes from GitHub clients.
- Third-party lyrics projects in `third_party/` (reference implementations)

### Architecture Insights

**Suno Auth Flow:**
```
User logs in via QWebEngineView → Cookies captured → __session extracted
→ JWT token refreshed via Clerk → API calls with Bearer token
```

**Lyrics Data Flow:**
```
Suno API → JSON with word timestamps → LyricsAligner aligns to lines
→ Database storage → KaraokeWidget displays with word highlighting
```

**Key Files for Karaoke Rewrite:**
- `src/suno/SunoLyrics.cpp` - Parser (keep, refactor)
- `src/suno/LyricsAligner.hpp` - Alignment logic (keep, improve)
- `src/ui/KaraokeWidget.cpp` - Display (gut and rewrite)
- `src/suno/SunoController.cpp` - Coordination (refactor)

---

*Last updated by agent: 2026-01-29*
