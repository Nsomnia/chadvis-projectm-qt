# Suno API Integration Guide for C++/Qt6 Music Video Creator

**Purpose:** This document provides complete API integration information for retrieving Suno songs, lyrics, cover art, and metadata for use in a Qt6-based music player with karaoke-style lyric overlay functionality.

**Important:** This is an UNOFFICIAL wrapper API. Suno does not provide an official public API. This implementation uses browser session cookies to authenticate requests.

---

## Architecture Overview

### What This API Provides
- **Retrieval Only:** Get existing Suno songs, lyrics, cover art, and metadata
- **No Generation:** Song creation endpoints are not relevant for this use case
- **Authentication:** Uses browser session cookies from suno.com
- **Auto Token Refresh:** Built-in token keep-alive mechanism (refreshes every 5 seconds)

### Tech Stack
- **Backend:** Python FastAPI (async)
- **HTTP Client:** aiohttp (async)
- **Authentication:** Bearer token (JWT) obtained from Suno's Clerk auth
- **Format:** RESTful API with JSON responses

---

## Prerequisites

### Environment Setup

Create a `.env` file in the project root:

```env
BASE_URL=https://studio-api.suno.ai
SESSION_ID=<your_suno_session_id>
COOKIE=<your_suno_cookie_string>
```

### How to Obtain Credentials

1. **Open Suno in Browser:** Navigate to https://suno.com
2. **Open Developer Tools:** Press F12 → Network tab
3. **Find Session Request:** Look for any request with `?__clerk_api_version`
4. **Copy Session ID:** Extract from the request URL or headers
5. **Copy Cookie:** Copy the full `Cookie` header value

**Note:** The API automatically refreshes the authentication token every 5 seconds, so these credentials don't expire.

---

## Core Retrieval Endpoints

### 1. Get Song Feed (Audio URL, Cover Art, Metadata)

**Endpoint:** `GET /feed/{aid}`

**Purpose:** Retrieve complete song information including audio URL, cover art image, and metadata.

**Request Parameters:**
- `aid` (path parameter): Audio ID of the song

**Authentication:** Bearer token (automatically injected by wrapper)

**Example Request:**
```bash
curl -X GET "http://localhost:8000/feed/abc123xyz" \
  -H "Authorization: Bearer <auto_token>"
```

**Response Structure:**
```json
[
  {
    "id": "abc123xyz",
    "audio_url": "https://cdn.suno.ai/audio/abc123xyz.mp3",
    "video_url": "https://cdn.suno.ai/video/abc123xyz.mp4",  // Optional
    "image_url": "https://cdn.suno.ai/images/abc123xyz.jpg",
    "image_large_url": "https://cdn.suno.ai/images/abc123xyz_large.jpg",
    "metadata": {
      "title": "Song Title",
      "tags": "pop, upbeat, female vocals",
      "description": "Song description or prompt",
      "duration": 184.5,
      "created_at": "2025-01-13T10:30:00Z",
      "play_count": 1234,
      "like_count": 56,
      "is_instrumental": false,
      "model_name": "chirp-v3-5"
    }
  }
]
```

**Key Fields for Music Player:**
- `audio_url`: MP3 audio file URL (for playback)
- `image_url` / `image_large_url`: Cover art images
- `video_url`: Optional video file (if available)
- `metadata.duration`: Song duration in seconds
- `metadata.title`: Song title
- `metadata.tags`: Genre/style information
- `metadata.created_at`: Creation date/time

**Polling Pattern:**
The `audio_url` field may be empty if the song is still processing. Implement polling:

```python
import time
import requests

def get_song_with_polling(aid, max_wait_seconds=90):
    start_time = time.time()

    while time.time() - start_time < max_wait_seconds:
        response = requests.get(f"http://localhost:8000/feed/{aid}")
        data = response.json()[0]

        if data.get("audio_url"):
            return data  # Song is ready

        time.sleep(5)  # Wait 5 seconds before checking again

    raise TimeoutError("Song processing timed out")
```

---

### 2. Get Song Lyrics

**Endpoint:** `GET /lyrics/{lid}`

**Purpose:** Retrieve lyrics for a specific song.

**Request Parameters:**
- `lid` (path parameter): Lyrics ID (may differ from audio ID)

**Authentication:** Bearer token (automatically injected)

**Example Request:**
```bash
curl -X GET "http://localhost:8000/lyrics/lyrics123" \
  -H "Authorization: Bearer <auto_token>"
```

**Response Structure:**
```json
{
  "id": "lyrics123",
  "text": "[Verse 1]\nWake up in the morning, feeling brand new\nGonna shake off the worries, leave 'em in the rearview\n\n[Chorus]\nI got sunshine in my pocket, happiness in my soul...",
  "title": "Sunshine in your Pocket",
  "format": "plain_text",
  "metadata": {
    "line_count": 24,
    "word_count": 156,
    "structure": ["Verse", "Chorus", "Bridge", "Chorus"]
  }
}
```

**Lyrics Format:**
The lyrics are returned as plain text with section markers:
- `[Verse]`, `[Verse 2]`, `[Verse 3]`, etc.
- `[Chorus]`
- `[Bridge]`
- `[Intro]`, `[Outro]`, `[Pre-Chorus]`

**Note on Karaoke Sync:**
This API does **NOT** provide timestamped/aligned lyrics. For karaoke-style synchronization, you would need to:
1. Use third-party alignment services
2. Implement audio analysis algorithms (e.g., forced alignment)
3. Use the section markers to create coarse sync points

---

### 3. Get Account Credits

**Endpoint:** `GET /get_credits`

**Purpose:** Check account credit balance and usage.

**Authentication:** Bearer token (automatically injected)

**Example Request:**
```bash
curl -X GET "http://localhost:8000/get_credits" \
  -H "Authorization: Bearer <auto_token>"
```

**Response Structure:**
```json
{
  "credits_left": 45,
  "period": "monthly",
  "monthly_limit": 50,
  "monthly_usage": 5
}
```

**Fields:**
- `credits_left`: Remaining credits
- `period`: Billing period (daily/monthly)
- `monthly_limit`: Total credits allowed per period
- `monthly_usage`: Credits already used this period

**Use Case:** Display account status in your music player UI.

---

## Data Models (Schemas)

### Song Feed Response (from `/feed/{aid}`)

```python
class SongFeedResponse:
    id: str                    # Unique song identifier
    audio_url: str | None       # MP3 download URL (null if processing)
    video_url: str | None       # MP4 video URL (optional)
    image_url: str              # Cover art (small)
    image_large_url: str        # Cover art (large)
    metadata: SongMetadata

class SongMetadata:
    title: str
    tags: str                  # Comma-separated genres/styles
    description: str | None     # Song prompt/description
    duration: float             # Duration in seconds
    created_at: str             # ISO 8601 timestamp
    play_count: int | None     # Number of plays
    like_count: int | None     # Number of likes
    is_instrumental: bool      # True if no vocals
    model_name: str            # Model version (e.g., "chirp-v3-5")
```

### Lyrics Response (from `/lyrics/{lid}`)

```python
class LyricsResponse:
    id: str                    # Lyrics identifier
    text: str                  # Full lyrics with section markers
    title: str                 # Song title
    format: str                # Currently "plain_text"
    metadata: LyricsMetadata

class LyricsMetadata:
    line_count: int | None
    word_count: int | None
    structure: list[str] | None # Detected song sections
```

### Credits Response (from `/get_credits`)

```python
class CreditsResponse:
    credits_left: int
    period: str
    monthly_limit: int
    monthly_usage: int
```

---

## Complete Workflow for Music Player

### Step 1: Start the API Server

```bash
# Install dependencies
pip3 install -r requirements.txt

# Start FastAPI server
uvicorn main:app --host 0.0.0.0 --port 8000

# Or use Docker
docker compose build && docker compose up
```

The server will automatically:
- Load session credentials from `.env`
- Start the token keep-alive thread (refreshes every 5 seconds)
- Expose endpoints on `http://localhost:8000`
- Provide API documentation at `http://localhost:8000/docs`

### Step 2: Retrieve Song by ID

```python
import requests

def get_song(aid):
    """Get complete song information"""
    response = requests.get(f"http://localhost:8000/feed/{aid}")

    if response.status_code != 200:
        raise Exception(f"API Error: {response.status_code}")

    song_data = response.json()[0]

    return {
        "id": song_data["id"],
        "audio_url": song_data.get("audio_url"),
        "cover_art_url": song_data.get("image_large_url"),
        "title": song_data["metadata"]["title"],
        "duration": song_data["metadata"]["duration"],
        "created_at": song_data["metadata"]["created_at"],
        "tags": song_data["metadata"]["tags"]
    }
```

### Step 3: Retrieve Lyrics

```python
def get_lyrics(lid):
    """Get song lyrics"""
    response = requests.get(f"http://localhost:8000/lyrics/{lid}")

    if response.status_code != 200:
        raise Exception(f"API Error: {response.status_code}")

    lyrics_data = response.json()

    return {
        "id": lyrics_data["id"],
        "title": lyrics_data["title"],
        "text": lyrics_data["text"],  # Full lyrics with section markers
    }
```

### Step 4: Download Audio File

```python
import os

def download_audio(audio_url, output_dir="downloads"):
    """Download audio file to local storage"""
    if not audio_url:
        raise ValueError("Audio URL is empty")

    response = requests.get(audio_url, stream=True)

    if response.status_code != 200:
        raise Exception("Failed to download audio")

    filename = os.path.basename(audio_url)
    filepath = os.path.join(output_dir, filename)

    os.makedirs(output_dir, exist_ok=True)

    with open(filepath, "wb") as f:
        for chunk in response.iter_content(chunk_size=1024):
            if chunk:
                f.write(chunk)

    return filepath
```

### Step 5: Download Cover Art

```python
def download_cover_art(image_url, output_dir="downloads/art"):
    """Download cover art to local storage"""
    if not image_url:
        raise ValueError("Image URL is empty")

    response = requests.get(image_url, stream=True)

    if response.status_code != 200:
        raise Exception("Failed to download cover art")

    filename = f"cover_{os.path.basename(image_url)}"
    filepath = os.path.join(output_dir, filename)

    os.makedirs(output_dir, exist_ok=True)

    with open(filepath, "wb") as f:
        for chunk in response.iter_content(chunk_size=1024):
            if chunk:
                f.write(chunk)

    return filepath
```

---

## Error Handling

### Common Error Responses

```json
{
  "code": 500,
  "msg": "An error occurred: <error_message>",
  "data": null
}
```

### Error Scenarios

| Scenario | HTTP Status | Response | Cause |
|----------|--------------|-----------|--------|
| Invalid Song ID | 500 | Error message | Song does not exist |
| Network Timeout | 500 | "An error occurred" | Upstream API unreachable |
| Authentication Failed | 500 | "An error occurred" | Invalid cookie/session_id |
| Song Still Processing | 200 | `audio_url: null` | Normal, wait and retry |

### Retry Strategy

```python
import time
from requests.exceptions import RequestException

def robust_get_song(aid, max_retries=3, retry_delay=2):
    """Get song with retry logic"""
    for attempt in range(max_retries):
        try:
            response = requests.get(f"http://localhost:8000/feed/{aid}", timeout=10)
            response.raise_for_status()
            return response.json()[0]

        except RequestException as e:
            if attempt == max_retries - 1:
                raise Exception(f"Failed after {max_retries} attempts: {e}")
            time.sleep(retry_delay)
```

---

## Integration with C++/Qt6

### Recommended HTTP Client: QNetworkAccessManager

**Why QNetworkAccessManager:**
- Built-in to Qt6
- Async/await support with QNetworkReply
- Handles redirects automatically
- SSL/TLS support built-in
- Signal/slot architecture fits Qt event loop

### Basic Qt6 Integration Example

```cpp
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

class SunoApiClient : public QObject {
    Q_OBJECT

public:
    explicit SunoApiClient(QObject *parent = nullptr) : QObject(parent) {
        m_networkManager = new QNetworkAccessManager(this);
        m_baseUrl = "http://localhost:8000";
    }

    void fetchSong(const QString &aid) {
        QUrl url(m_baseUrl + "/feed/" + aid);
        QNetworkRequest request(url);

        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray responseData = reply->readAll();
                QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
                QJsonArray songs = jsonDoc.array();
                QJsonObject song = songs.first().toObject();

                // Extract data
                QString audioUrl = song["audio_url"].toString();
                QString coverArtUrl = song["image_large_url"].toString();
                QJsonObject metadata = song["metadata"].toObject();
                QString title = metadata["title"].toString();
                double duration = metadata["duration"].toDouble();

                // Emit signal with song data
                emit songFetched({
                    {"id", song["id"].toString()},
                    {"title", title},
                    {"audioUrl", audioUrl},
                    {"coverArtUrl", coverArtUrl},
                    {"duration", duration}
                });
            } else {
                emit errorOccurred(reply->errorString());
            }
            reply->deleteLater();
        });
    }

    void downloadFile(const QString &url, const QString &savePath) {
        QUrl fileUrl(url);
        QNetworkRequest request(fileUrl);

        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, [this, reply, savePath]() {
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                QFile file(savePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(data);
                    file.close();
                    emit downloadComplete(savePath);
                }
            } else {
                emit errorOccurred(reply->errorString());
            }
            reply->deleteLater();
        });
    }

signals:
    void songFetched(const QJsonObject &songData);
    void downloadComplete(const QString &filePath);
    void errorOccurred(const QString &errorMessage);

private:
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
};
```

### Audio Playback in Qt6

```cpp
#include <QMediaPlayer>
#include <QAudioOutput>

class MusicPlayer : public QObject {
    Q_OBJECT

public:
    explicit MusicPlayer(QObject *parent = nullptr) : QObject(parent) {
        m_player = new QMediaPlayer(this);
        m_audioOutput = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOutput);
    }

    void loadSong(const QString &audioUrl) {
        m_player->setSource(QUrl(audioUrl));
    }

    void play() {
        m_audioOutput->setVolume(0.8);
        m_player->play();
    }

    void pause() {
        m_player->pause();
    }

    void stop() {
        m_player->stop();
    }

    qint64 duration() const {
        return m_player->duration();
    }

    qint64 position() const {
        return m_player->position();
    }

signals:
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);

private:
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
};
```

---

## Karaoke-Style Lyric Overlay Implementation

### Data Structure for Lyrics

```cpp
struct LyricLine {
    QString section;      // "Verse", "Chorus", "Bridge", etc.
    QString text;        // The actual lyric text
    int startIndex;      // Start position in song (milliseconds)
    int endIndex;        // End position in song (milliseconds)
};

class LyricsParser {
public:
    static QList<LyricLine> parseLyrics(const QString &lyricsText) {
        QList<LyricLine> lines;
        QStringList rawLines = lyricsText.split('\n');

        QString currentSection;

        for (const QString &rawLine : rawLines) {
            QString trimmed = rawLine.trimmed();

            // Detect section markers: [Verse], [Chorus], etc.
            QRegularExpression sectionPattern(R"(^\[([^\]]+)\])");
            QRegularExpressionMatch match = sectionPattern.match(trimmed);

            if (match.hasMatch()) {
                currentSection = match.captured(1);
            } else if (!trimmed.isEmpty()) {
                LyricLine line;
                line.section = currentSection;
                line.text = trimmed;
                line.startIndex = -1;  // To be set by sync algorithm
                line.endIndex = -1;    // To be set by sync algorithm
                lines.append(line);
            }
        }

        return lines;
    }
};
```

### Visualizer Integration (projectM)

```cpp
#include <projectM.hpp>

class KaraokeOverlay : public QObject {
    Q_OBJECT

public:
    explicit KaraokeOverlay(QObject *parent = nullptr) : QObject(parent) {
        // Initialize projectM visualizer
        m_visualizer = new projectM;
        m_visualizer->projectM_init();

        // Setup OpenGL widget for visualization
        m_glWidget = new QOpenGLWidget;
    }

    void setLyrics(const QList<LyricLine> &lyrics) {
        m_lyrics = lyrics;
    }

    void updateLyricsForTime(qint64 positionMs) {
        QString currentLyric;
        QString nextLyric;

        for (int i = 0; i < m_lyrics.size(); i++) {
            const LyricLine &line = m_lyrics[i];

            if (positionMs >= line.startIndex && positionMs <= line.endIndex) {
                currentLyric = line.text;
                if (i + 1 < m_lyrics.size()) {
                    nextLyric = m_lyrics[i + 1].text;
                }
                break;
            }
        }

        emit lyricsUpdated(currentLyric, nextLyric);
    }

private:
    projectM *m_visualizer;
    QOpenGLWidget *m_glWidget;
    QList<LyricLine> m_lyrics;

signals:
    void lyricsUpdated(const QString &current, const QString &next);
};
```

---

## Complete C++/Qt6 Workflow Example

```cpp
// Main application
class MusicVideoCreator : public QObject {
    Q_OBJECT

public:
    explicit MusicVideoCreator(QObject *parent = nullptr) : QObject(parent) {
        m_api = new SunoApiClient(this);
        m_player = new MusicPlayer(this);
        m_overlay = new KaraokeOverlay(this);

        connect(m_api, &SunoApiClient::songFetched,
                this, &MusicVideoCreator::onSongFetched);
    }

    void loadSongById(const QString &aid) {
        m_api->fetchSong(aid);
    }

private slots:
    void onSongFetched(const QJsonObject &songData) {
        QString audioUrl = songData["audioUrl"].toString();
        QString lyricsId = songData["id"].toString();

        // Download and load audio
        QString audioPath = downloadToCache(audioUrl);
        m_player->loadSong(audioPath);

        // Fetch lyrics
        m_api->fetchLyrics(lyricsId);
    }

    void onLyricsFetched(const QString &lyricsText) {
        QList<LyricLine> lyrics = LyricsParser::parseLyrics(lyricsText);
        m_overlay->setLyrics(lyrics);

        // Note: You'll need to sync lyrics with audio timing
        // This can be done using third-party alignment services
        // or audio analysis algorithms
    }

private:
    SunoApiClient *m_api;
    MusicPlayer *m_player;
    KaraokeOverlay *m_overlay;
};
```

---

## Performance Optimization

### Caching Strategy

```cpp
class CacheManager {
public:
    static CacheManager &instance() {
        static CacheManager manager;
        return manager;
    }

    QString getCachedAudioPath(const QString &aid) {
        QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QString cachePath = cacheDir + "/audio/" + aid + ".mp3";

        if (QFile::exists(cachePath)) {
            return cachePath;
        }
        return QString();
    }

    void cacheAudio(const QString &aid, const QString &sourcePath) {
        QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QString cachePath = cacheDir + "/audio/" + aid + ".mp3";

        QDir().mkpath(cacheDir + "/audio");
        QFile::copy(sourcePath, cachePath);
    }
};
```

### Async Preloading

```cpp
class Preloader : public QObject {
    Q_OBJECT

public:
    void preloadNextSong(const QString &aid) {
        QString cachePath = CacheManager::instance().getCachedAudioPath(aid);

        if (!cachePath.isEmpty()) {
            emit nextSongReady(cachePath);
            return;
        }

        // Fetch song info
        m_api->fetchSong(aid);
    }

    void onSongFetched(const QJsonObject &songData) {
        QString audioUrl = songData["audioUrl"].toString();
        QString cachePath = downloadToCache(audioUrl);
        emit nextSongReady(cachePath);
    }

signals:
    void nextSongReady(const QString &audioPath);

private:
    SunoApiClient *m_api;
};
```

---

## Rate Limiting and Throttling

### Upstream Limits (Suno)
- **Credit-based:** Free tier = 50 credits/day (~10 songs)
- **No local enforcement:** This API does not rate-limit clients
- **Upstream enforcement:** Suno's backend enforces limits

### Recommended Client-Side Throttling

```cpp
class RateLimiter {
public:
    RateLimiter(int requestsPerMinute) {
        m_minIntervalMs = 60000.0 / requestsPerMinute;
    }

    void wait() {
        qint64 elapsed = m_lastRequest.msecsTo(QDateTime::currentDateTime());

        if (elapsed < m_minIntervalMs) {
            QThread::msleep(m_minIntervalMs - elapsed);
        }

        m_lastRequest = QDateTime::currentDateTime();
    }

private:
    qint64 m_minIntervalMs;
    QDateTime m_lastRequest;
};

// Usage
RateLimiter limiter(10);  // 10 requests per minute

void fetchMultipleSongs(const QStringList &songIds) {
    for (const QString &aid : songIds) {
        limiter.wait();
        m_api->fetchSong(aid);
    }
}
```

---

## Testing

### Test Endpoints

```bash
# Test API health
curl http://localhost:8000/

# Test song retrieval
curl http://localhost:8000/feed/test_aid

# Test lyrics retrieval
curl http://localhost:8000/lyrics/test_lid

# Test credits
curl http://localhost:8000/get_credits
```

### Python Test Script

```python
# test_retrieval.py
import requests

def test_api():
    base_url = "http://localhost:8000"

    # Test song feed
    song_id = "test_aid"
    response = requests.get(f"{base_url}/feed/{song_id}")
    print("Song Feed:", response.status_code, response.json())

    # Test lyrics
    lyrics_id = "test_lid"
    response = requests.get(f"{base_url}/lyrics/{lyrics_id}")
    print("Lyrics:", response.status_code, response.json())

    # Test credits
    response = requests.get(f"{base_url}/get_credits")
    print("Credits:", response.status_code, response.json())

if __name__ == "__main__":
    test_api()
```

---

## Troubleshooting

### Common Issues

**Issue:** `audio_url` is null
- **Cause:** Song is still processing
- **Solution:** Implement polling with 5-30 second intervals

**Issue:** 500 Internal Server Error
- **Cause:** Invalid credentials or network issues
- **Solution:** Verify `.env` file and check API server logs

**Issue:** Token expiration
- **Cause:** Keep-alive thread stopped
- **Solution:** Restart API server

**Issue:** Download failures
- **Cause:** CDN timeout or network issues
- **Solution:** Implement retry logic with exponential backoff

### Logging

The API logs:
- Token refresh events (every 5 seconds)
- HTTP requests and responses
- Error messages

Check logs with:
```bash
# If running directly
# Logs appear in console

# If using Docker
docker compose logs -f suno-api
```

---

## Security Considerations

### Credential Storage
- **Never commit `.env` file to version control**
- Use environment variables in production
- Rotate credentials periodically

### Token Management
- Tokens are auto-refreshed by the wrapper
- No manual token refresh needed
- Tokens are stored in memory only

### Network Security
- API runs on HTTP by default (configure HTTPS for production)
- All requests to Suno use HTTPS
- Consider adding API key authentication if hosting publicly

---

## API Reference Summary

| Endpoint | Method | Purpose | Auth |
|----------|--------|---------|------|
| `/feed/{aid}` | GET | Get song metadata, audio URL, cover art | Bearer |
| `/lyrics/{lid}` | GET | Get song lyrics | Bearer |
| `/get_credits` | GET | Get account credit info | Bearer |
| `/docs` | GET | Interactive API documentation | None |

---

## File Structure

```
Suno-API/
├── main.py              # FastAPI application and route definitions
├── schemas.py           # Pydantic data models
├── utils.py             # HTTP request utilities
├── cookie.py            # Session and token management
├── deps.py             # Dependency injection (get_token)
├── test.py             # Test scripts
├── requirements.txt      # Python dependencies
├── .env.example        # Environment template
├── Dockerfile          # Docker configuration
├── docker-compose.yml  # Docker compose config
└── README.md           # Documentation
```

---

## Conclusion

This API wrapper provides a simple, async interface for retrieving Suno songs, lyrics, and cover art. The key features for your music video creator are:

1. **Complete song metadata** (title, duration, creation date, tags)
2. **Audio file URLs** (for playback)
3. **Cover art images** (for display)
4. **Lyrics** (plain text with section markers)
5. **Auto token refresh** (no manual auth needed)

For karaoke synchronization, you'll need to implement a lyric alignment algorithm or use third-party services, as the API does not provide timestamped lyrics.

---

**Last Updated:** January 13, 2026
**API Version:** Unofficial Wrapper (based on Suno's internal API)
