# Suno AI Integration Technical Documentation

This document describes the implementation details of the Suno AI integration in ChadVis, covering authentication, data persistence, and UI synchronization.

## Architecture

The integration follows the application's **Singleton-Engine-Controller** pattern:

1.  **SunoClient (`src/suno/SunoClient.*`)**: Low-level API client handling HTTP communication, authentication flows (Clerk API), and JSON parsing.
2.  **SunoDatabase (`src/suno/SunoDatabase.*`)**: SQLite-based persistence for clip metadata and aligned lyrics.
3.  **SunoController (`src/ui/controllers/SunoController.*`)**: Orchestration layer that coordinates syncing, downloading, and integration with `AudioEngine` and `OverlayEngine`.
4.  **SunoBrowser (`src/ui/SunoBrowser.*`)**: UI widget for displaying and interacting with the library.

## Authentication Flow

Suno uses a complex two-tier authentication system:

1.  **Session Cookies**: Users provide cookies (`__client`, `__session`) extracted from their browser.
2.  **JWT Extraction**: If the `__session` cookie contains a JWT (starts with `eyJ`), it is used directly as the Bearer token.
3.  **Clerk API Refresh**: If no token is present or it expires:
    *   The client calls `clerk.suno.com/v1/client` with the cookies to get a Session ID (`sid`).
    *   It then POSTs to `clerk.suno.com/v1/client/sessions/${sid}/tokens` to obtain a fresh JWT.
4.  **Persistence**: The `SunoClient` emits a `tokenChanged` signal whenever a new JWT is obtained. The `SunoController` listens to this and automatically saves it to the application's `config.toml`, preventing the need for frequent re-authentication.

## Data Persistence (SQLite)

The `SunoDatabase` stores all metadata retrieved from the Suno API. 

### Schema Evolution (Migration)
The database includes an automatic migration mechanism in `init()`. It checks for the existence of columns and uses `ALTER TABLE` to add missing fields (e.g., `duration`, `model_name`, `is_liked`) without losing user data from previous versions.

### Stored Fields
- **Identification**: UUID, Title, Handle, Display Name.
- **Media**: Audio URL, Video URL, Image URLs (small/large).
- **Metadata**: Model version, Tags, Prompt, Lyrics, Duration.
- **Status**: Stream/Complete status, Liked/Trashed/Public flags.
- **Lyrics**: Raw JSON of word-level aligned lyrics.

## Karaoke & Synced Lyrics

The integration includes a sophisticated karaoke system:

1.  **Alignment**: `SunoClient` fetches aligned lyrics (JSON with word-level timestamps) from Suno's API.
2.  **Fallback**: If aligned lyrics are missing, `SunoLyrics::align` attempts to align the prompt text with the audio duration heuristically.
3.  **Rendering**: 
    *   **OverlayEngine**: Renders the lyrics on top of the visualizer for that immersive experience.
    *   **KaraokeWidget**: A dedicated UI widget for viewing lyrics in a scrolling list, perfect for a second monitor setup.
4.  **Customization**: Users can configure font, size, colors (active/inactive/shadow), and vertical position in the Settings dialog.

## Synchronization and UI

- **Startup**: On application launch, `SunoController` loads all cached clips from the database immediately. `SunoBrowser` displays these cached clips on construction, providing instant access even offline.
- **Library Sync**: When "Sync Library" is clicked, the controller fetches all pages from the Suno API (20 clips per page) until the library is fully updated.
- **Lyrics Queue**: To avoid API rate limits, lyrics are fetched asynchronously via a internal queue with a concurrency limit of 5 requests.
- **Type Safety**: The parser handles varied JSON types from the Suno API (e.g., duration being returned as both numbers and strings).

## UI Improvements

- **Docking**: The Tools dock is configured to be floatable and resizable with a sensible minimum width, ensuring the Suno data table is readable.
- **Responsive Layout**: `SunoBrowser` uses an `Expanding` size policy to fill available dock space.

## Reliable Shutdown

To ensure background threads (FFmpeg encoding, network requests) do not hang the application:
- Manual signal handlers in `main.cpp` were removed to let `QApplication` handle signals cleanly.
- `Application::quit()` executes a hard `std::exit(0)` after triggering Qt's cleanup, ensuring the process terminates even if some low-level threads are blocked.