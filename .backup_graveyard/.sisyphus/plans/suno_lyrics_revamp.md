# Suno Lyrics Integration Revamp Plan

## Goal
Simplify and stabilize Suno lyrics fetching and display. Ensure that "karaoke style" synchronized lyrics are fetched, saved to disk, and displayed immediately upon playback without relying on a successful database sync.

## Proposed Changes

### 1. Robust JSON Parsing (`src/suno/SunoLyrics.cpp`)
*   Update `LyricsAligner::parseJson` to be more resilient to Suno's varied response formats.
*   Implement a "deep search" for any array containing objects with `word` and `start`/`end` fields.
*   Ensure that instrumental tags (e.g., `[Instrumental]`) are handled gracefully or filtered without failing the entire parse.

### 2. Direct Display Injection (`src/ui/controllers/SunoController.cpp`)
*   Modify `onAlignedLyricsFetched` to immediately parse and send lyrics to `OverlayEngine` if the `clipId` matches the currently playing track.
*   Remove the "wait for database" requirement for immediate display.
*   Implement a "direct mapping" cache for recently fetched lyrics to survive track restarts during a session.

### 3. Improved Track-to-ID Mapping (`src/ui/controllers/SunoController.cpp`)
*   Enhance `onTrackChanged` to extract UUIDs from filenames more aggressively if metadata is missing.
*   Use a fallback where the controller keeps track of the "last requested ID" to map incoming lyrics if the playlist state is in transition.

### 4. Automatic Persistence (`src/ui/controllers/SunoController.cpp`)
*   Ensure `onAlignedLyricsFetched` always attempts to save `.json` and `.srt` sidecar files next to the local MP3 if found in the download directory.
*   Verify that the `saveLyrics` config setting is respected.

### 5. Simple Working State for Overlay (`src/overlay/OverlayEngine.cpp`)
*   Ensure `drawToCanvas` properly clears and updates when new lyrics are injected.
*   Add better logging to trace exactly why a lyric line might be skipped (e.g., duration mismatch).

## Verification Plan

### Manual Tests
1.  **Direct ID Playback:** Run `./build/chadvis-projectm-qt --suno-id <UUID>` and verify lyrics appear as soon as the API response returns.
2.  **Local File Playback:** Play a previously downloaded Suno MP3 from the local file browser and verify it automatically fetches/loads the `.srt` or `.json` if available.
3.  **Persistence Check:** Verify that a new `.srt` and `.json` file are created in `~/Music` (or configured path) after playing a Suno song for the first time.

### Logging Verification
*   Monitor `SunoController: Displaying lyrics for [ID]` in logs.
*   Monitor `OverlayEngine: Received [N] lines of lyrics`.
