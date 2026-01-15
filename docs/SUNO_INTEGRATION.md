# Suno AI Integration Architecture

This document describes the implementation of Suno AI integration within `chadvis-projectm-qt`, covering authentication, persistence, data modeling, and UI synchronization.

## Overview

The Suno integration allows users to browse their Suno AI song library, manage metadata, and download tracks for visualization. It consists of four main components:

1.  **`SunoClient`**: Low-level HTTP client handling Clerk authentication and Suno API requests.
2.  **`SunoDatabase`**: SQLite-based persistence for song metadata and lyrics.
3.  **`SunoController`**: The "Brain" that orchestrates data flow between the client, database, and UI.
4.  **`SunoBrowser`**: The Qt-based UI component for displaying and interacting with the library.

## Authentication Flow

Suno uses Clerk for authentication. The process involves two primary tokens:

*   **Session Cookie (`__client`)**: A long-lived cookie used to interact with Clerk.
*   **JWT (Bearer Token)**: A short-lived token used for actual Suno API requests.

### Token Refresh
When the Bearer token expires (or is missing), `SunoClient` uses the session cookie to request a new session from `clerk.suno.com`. The updated tokens are automatically saved back to the application configuration via the `tokenChanged` signal.

## Persistence

### Configuration (`config.toml`)
Authentication tokens and cookies are stored in the global configuration file managed by the `Config` class.
*   `suno.session_token`: The Bearer JWT.
*   `suno.cookie`: The session cookie.

### Song Library (`suno_library.db`)
Clips, metadata, and lyrics are stored in a dedicated SQLite database.

**Schema Highlights:**
*   `clips`: Stores core metadata (ID, title, status, model, duration, URLs).
*   `lyrics`: (Planned/Partial) Stores synchronized or raw lyric data.

**Automated Migrations:**
The `SunoDatabase` class implements a lightweight migration system in `init()` that checks for missing columns (e.g., `model_name`, `duration`) and adds them dynamically to support schema evolution without data loss.

## Data Modeling

### `SunoClip`
A C++ representation of a Suno song.
*   Handles polymorphic JSON parsing (supporting both root-level fields and nested `metadata` objects).
*   Robustly handles Suno's varied data types (e.g., numeric durations that sometimes arrive as strings, boolean statuses).

## UI Synchronization

The integration uses a reactive pattern powered by `Signal<T>` (a custom signal implementation):

1.  **Sync Triggered**: User clicks "Sync" in `SunoBrowser`.
2.  **API Fetch**: `SunoClient` fetches latest clips from Suno.
3.  **Database Update**: `SunoController` receives clips, saves them to SQLite.
4.  **UI Refresh**: `SunoController` emits `libraryUpdated`, and `SunoBrowser` refreshes its view from the database.

## Technical Details of Recent Fixes (Jan 2026)

*   **JSON Robustness**: Fixed crashes and missing data caused by Suno API changes where `duration` and `model_name` moved into a `metadata` sub-object.
*   **Persistence Loop**: Resolved issue where tokens weren't surviving restarts by connecting `SunoClient::tokenChanged` to `Config::set()`.
*   **Startup Recovery**: Implemented logic to load the cached library from SQLite immediately on startup, providing an "offline-first" experience.
*   **Shutdown Stability**: Fixed a deadlock/hang during application exit by ensuring `std::exit(0)` is called after `QApplication::quit()`, terminating background network threads cleanly.
