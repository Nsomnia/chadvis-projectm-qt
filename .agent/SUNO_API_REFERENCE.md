# Suno API Integration Reference

> **For:** AI agents implementing Suno features on `refactor/qml-modern-gui`
> **Version:** 1.0.0 - 2026-03-27
> **Purpose:** C++/Qt6 integration patterns for the Suno API

---

## Existing Architecture

The Suno integration follows the Singleton-Engine-Controller pattern:

```
SunoBridge (QML)  ←→  SunoController  ←→  SunoClient  ←→  Suno API
                            ↓                    ↓
                    SunoDatabase          QNetworkAccessManager
                    (SQLite)              (or future: cpr)
```

### Key Files (Current Branch)
- `src/suno/SunoClient.hpp/cpp` — HTTP client, auth, token management
- `src/suno/SunoDatabase.hpp/cpp` — SQLite persistence with auto-migration
- `src/suno/SunoLyrics.hpp` — Lyrics parsing and alignment
- `src/ui/controllers/SunoController.hpp/cpp` — Orchestration layer
- `src/qml_bridge/SunoBridge.hpp/cpp` — QML bridge (new)

### Key Files (Pre-QML Reference)
- `src/ui/controllers/SunoController.*` — Sync, download, lyrics queue management
- `src/ui/SunoBrowser.*` — Library UI (to be replaced by QML TableView)

---

## Auth Flow Implementation

```cpp
// Existing pattern in SunoClient:
// 1. Load cookies from config.toml on startup
// 2. Extract JWT from __session cookie (starts with "eyJ")
// 3. If expired: POST to Clerk token endpoint with cookies
// 4. Emit tokenChanged → SunoController saves to config
// 5. All API calls use: Authorization: Bearer <jwt>
```

### For QML Refactor
- Auth flow stays in C++ (`SunoClient`)
- QML exposes auth state via `SunoBridge` (logged in/out, token expiry)
- Auth UI: Use `SystemBrowserAuth` (localhost redirect) — do NOT reintroduce QtWebEngine

---

## Database Schema

```sql
CREATE TABLE IF NOT EXISTS clips (
    uuid TEXT PRIMARY KEY,
    title TEXT,
    handle TEXT,
    display_name TEXT,
    audio_url TEXT,
    video_url TEXT,
    image_url TEXT,
    image_large_url TEXT,
    model_version TEXT,
    tags TEXT,
    prompt TEXT,
    lyrics TEXT,              -- Raw JSON of word-level aligned lyrics
    duration REAL,
    status TEXT,              -- 'stream' or 'complete'
    is_liked INTEGER DEFAULT 0,
    is_trashed INTEGER DEFAULT 0,
    is_public INTEGER DEFAULT 1,
    created_at TEXT,
    synced_at TEXT            -- Last sync timestamp
);
```

**Migration:** `SunoDatabase::init()` checks for missing columns and uses `ALTER TABLE` to add them.

---

## Lyrics Sync Architecture

```
SunoLyrics::parse()
    ↓
LyricsData (immutable, binary search O(log n))
    ↓
LyricsSync (60fps time synchronization)
    ↓
LyricsRenderer (base class)
    ├── KaraokeRenderer (word-level glow highlighting)
    └── QML LyricsPanel (scrollable, click-to-seek)
```

### Alignment Strategy
1. **Primary:** Suno API provides word-level aligned lyrics (JSON with timestamps)
2. **Fallback:** `SunoLyrics::align()` heuristic alignment from prompt text
3. **Future:** `whisper.cpp` forced alignment for local audio files

---

## Rate-Limited Queue Pattern

```cpp
// Existing pattern — preserve during refactor:
// - Library fetch: max 1 req/5sec
// - Lyrics fetch: max 1 req/2sec
// - Concurrency limit: 3-5 concurrent requests
// - Progress callbacks for UI feedback (ETA, percentage)
```

---

## QML Bridge Pattern

```cpp
// SunoBridge exposes to QML:
class SunoBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY authStateChanged)
    Q_PROPERTY(int creditsLeft READ creditsLeft NOTIFY creditsChanged)
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY syncStateChanged)
    // ...
public slots:
    void syncLibrary();
    void fetchLyrics(const QString &clipId);
    void downloadClip(const QString &clipId);
signals:
    void authStateChanged();
    void creditsChanged();
    void libraryChanged();
    void syncProgress(int current, int total, const QString &eta);
};
```

---

## Common Pitfalls

1. **Duration type mismatch:** Suno API returns duration as both `number` and `string`. Handle both.
2. **Null audio_url:** Songs still processing have `null` audio_url. Implement polling.
3. **Token expiry:** JWTs expire ~24h. Refresh proactively, not just on 401.
4. **SQLite threading:** All DB access must happen on a single thread. Use signal/slot for cross-thread queries.
5. **Large libraries:** 10,000+ clips is common. Use lazy-loading model, never load all into memory at once.

---

## Testing Checklist

- [ ] Auth flow: login → token saved → persists across restart
- [ ] Library sync: fetches pages, handles rate limits, updates DB
- [ ] Lyrics fetch: respects concurrency limit, shows progress
- [ ] Download: handles null audio_url, saves to correct path
- [ ] QML: library table scrolls smoothly with 1000+ items
- [ ] Error handling: expired token, network failure, invalid JSON
