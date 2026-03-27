


### C++23 Benefits for `chadvis-projectm-qt`
Moving to C++23 on a rolling release distro like Arch (which natively ships GCC 13/14 or Clang 16+) provides immense architectural and performance benefits for a multimedia application like this:
1. **`std::expected`**: You can completely replace your custom `vc::Result` class. It standardizes error handling without the overhead of exceptions, matching perfectly with your current monadic architecture.
2. **`std::mdspan`**: Perfect for multidimensional audio buffers (channels x frames) and OpenGL pixel data handling during video recording, allowing zero-cost slicing and view passing.
3. **`std::print`**: Replaces `fmt` and `spdlog` basic usages with standard, lightning-fast, type-safe formatting natively. 
4. **Deducing `this`**: Drastically simplifies CRTP (Curiously Recurring Template Pattern) and avoids duplication in `const` and non-`const` member functions (like your `Config` accessors).
5. **Standardized Ranges/Views**: `std::ranges::views::enumerate`, `zip`, and `chunk` will heavily optimize how you parse FFmpeg frames and manipulate Suno API JSON arrays.

### Suno API Research & Architecture
The official Suno API does not exist publicly, but the community has thoroughly reverse-engineered the backend. Projects like `gcui-art/suno-api` and `SunoAI-API/Suno-API` reveal the following architecture:
*   **Auth**: It relies on passing the `__session` cookie and a Clerk `Bearer` token to the internal `studio-api.suno.ai` endpoints. The tokens must be kept alive.
*   **Mechanism**: All actions (generate, extend, lyrics) are async HTTP POST calls that return task IDs. You must poll a `GET` endpoint or rely on WebSockets/SSE to know when the generation (or WAV conversion) is complete.
*   **Target**: The user has provided sniffed endpoints in `.agents/`. You will need to parse this file to implement the bleeding-edge `/b-side` and `/experimental` features alongside the standard text-to-music calls.

---

# AGENT DIRECTIVES & SYSTEM INSTRUCTIONS
*For the Autonomous Agent (`opencode` or similar):*

1.  **File Versioning**: EVERY file modified or created MUST contain a version number and a date-time stamp in its header block (e.g., `@version 2.0.1 - 2026-03-27 01:33:00 MDT`).
2.  **Git Operations**: Make frequent, atomic git commits. Keep `CHANGELOG.md` heavily updated. If `CHANGELOG.md` gets too long, archive older entries into the `docs/` directory.
3.  **Agent Workspace**: You are permitted and encouraged to create and maintain an `AGENTS.md` file in any directory to track your own thoughts, architecture diagrams, or persistent memory across loops.
4.  **TODO Modification Rules**: You may add, refactor, structure, and rank the items in the `TODO.md` list below. **However, ONLY the user is permitted to remove/check off an item.** 

---

# MASTER TODO.md
*(Copy this block into a `TODO.md` file in the project root)*

> **NOTE:** Only the user may mark checkboxes as `[x]`. The Agent may add new items, reorder, rank, and categorize them to plan its execution loops. Partially implemented can be tracked with a tilde `[~]`.

### [P0] Critical Fixes & Rearchitecture
- [ ] **Gut and Rewrite Video Recording Pipeline:** The current recording implementation is non-functional. Completely rip it out. Build a new, versatile FFmpeg pipeline that handles automation, conflict resolution (e.g., file locks, name collisions), and allows starting/stopping at *any* point in an audio track.
- [ ] **Background Video Finalization:** Ensure the new video recorder safely finalizes and muxes incomplete videos in the background if aborted, or auto-deletes them cleanly without leaving corrupted `.mp4` files on the disk.
- [ ] **Window Resizing / Render Dimensions:** Fix the bug where resizing the Qt Window fails to properly update the projectM OpenGL render dimensions or framebuffer size.
- [ ] **Reimplement Audio Playlist:** Restore the pre-QML audio playlist functionality. Ensure it has robust, gapless-capable handling of both local audio files and remote HTTP Suno streams.

###[P1] ProjectM Visualizer Control & Headers
- [ ] **Explore ProjectM-4 Headers:** Investigate `/usr/include/projectM-4/` (specifically `playlist.h`, `projectM.h`) to implement native playlist transitions and deeper shader control.
- [ ] **Automated Blacklisting:** Implement logic to detect broken projectM shaders (e.g., OpenGL compilation failures or rendering crashes) and automatically blacklist them.
- [ ] **Manual Blacklisting:** Add UI/API support to manually add specific shaders to the blacklist.
- [ ] **Favorites System:** Implement a "Favorite" toggle for shaders.
- [ ] **Mass Automation Constraints:** Create a specific mass-recording mode that can cleanly and intelligently handle recoding multiple playlist items whether Suno remote or locat audio file as well a a mixture of both consecutively.
- [ ] **ProjectM Visualizer Favorites:** In addition for the ability to rank shaders a star rating by the end-user, a setting can be toggled to only select from (and thus render and record when recording to video is active) visualizer shaders the user has ranks as favorites OR optionally above a certain rating rank.

### [P2] Suno API Integration
- [ ] **Parse Agent Endpoints:** Read the `.agents/` directory to extract the user's mass-sniffed Suno API endpoints.
- [ ] **Official Features:** Implement robust HTTP POST wrappers for standard Suno generation, lyrics generation, and extending tracks.
- [ ] **B-Side & Experimental:** Map out and implement the undocumented `/b-side` and `/experimental` API endpoints found in the user's sniffed data.
- [ ] **Session Keep-Alive:** Build a background worker to maintain the Clerk.dev JWT and session cookies to prevent auth timeouts during mass generation.

### [P3] Tech Debt & Modernization
- [ ] **Migrate to C++23:** Update `CMakeLists.txt` to `CMAKE_CXX_STANDARD 23`.
- [ ] **Replace Custom Result:** Swap `vc::Result` for `std::expected`.
- [ ] **Add File Versioning:** Pass through the entire codebase and add `@version` and Date-Time stamps to all headers and source files.
- [ ] **Create AGENTS.md:** Initialize `AGENTS.md` in the root directory to store architectural findings regarding FFmpeg recording pipelines and Suno API structures.
- [ ] **Audio Library Database For Remote and Tags:** Reading and writing id3 tags for local audio or remote when a download mode is active. Remote Suno library tracks  are kept in the local database as well and a cleanup function can have the user choose to remove items no longer found in the remote.