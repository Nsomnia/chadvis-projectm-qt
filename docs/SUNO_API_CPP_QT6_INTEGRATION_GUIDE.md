# Suno API Integration Guide for C++/Qt6 Music Video Creator

**Purpose:** This document provides complete information for making direct HTTP requests to Suno's internal API to retrieve songs, lyrics, cover art, and metadata for use in a Qt6-based music player with karaoke-style lyric overlay functionality.

**Important:** There is NO official Suno API. This guide describes how to make **direct HTTP requests** to Suno's internal endpoints using **Clerk session authentication** (cookies/session tokens) extracted from a browser session. This is the same approach used by community projects like `gcui-art/suno-api`, `SunoAI-API/Suno-API`, and others.

**Architecture:**
- Your C++/Qt6 application makes direct HTTP requests to `studio-api.suno.ai` (or similar internal endpoints)
- Authentication uses Clerk session cookies extracted from a logged-in browser session
- No intermediary server, proxy, or wrapper API - direct communication with Suno

---

## Architecture Overview

### What This Integration Provides
- **Retrieval Only:** Get existing Suno songs, lyrics, cover art, and metadata
- **No Generation:** Song creation endpoints are not covered in this guide (focus on retrieval for playback)
- **Authentication:** Direct Clerk session cookies from suno.com browser session
- **No Intermediary:** Your application communicates directly with Suno's internal API endpoints

### Suno's Internal API Structure
- **Base URL:** Typically `https://studio-api.suno.ai` or `https://api.suno.ai` (may change)
- **Authentication:** Clerk session tokens extracted from browser cookies
- **Format:** RESTful API with JSON responses
- **Rate Limiting:** Enforced by Suno based on user account tier (free: ~50 credits/day)

### Your Application Stack
- **HTTP Client:** Qt6 `QNetworkAccessManager` (built-in, async)
- **Authentication:** Clerk session token stored and used in Authorization header
- **Response Parsing:** Qt6 `QJsonDocument`, `QJsonObject`, `QJsonArray`

---

## Prerequisites

### Authentication Setup

Since there is no official Suno API, you must obtain authentication credentials from a logged-in browser session.

#### Step 1: Obtain Clerk Session Token

1. **Open Suno in Browser:** Navigate to https://suno.com and log in
2. **Open Developer Tools:** Press F12 → Network tab
3. **Filter Requests:** Look for requests to `studio-api.suno.ai` or similar
4. **Find Authorization Header:** Look for `Authorization: Bearer <token>` in request headers
5. **Copy Session Token:** Copy the Bearer token value

**Alternative Method (Cookie-based):**
1. In the Network tab, find requests containing `__clerk_api_version` in the URL
2. Copy the full `Cookie` header value
3. Extract the session token from the cookie string

#### Step 2: Store Credentials Securely

Store the session token in your application's configuration:

```cpp
// In your config file or environment variables
QString SUNO_BASE_URL = "https://studio-api.suno.ai";  // May vary, check community projects
QString SUNO_SESSION_TOKEN = "<your_bearer_token>";
```

**Security Notes:**
- Never commit session tokens to version control
- Tokens may expire (typically 7 days to 30 days, varies)
- Suno may update their authentication mechanism (community projects track these changes)
- Consider implementing token refresh logic if supported by current Suno implementation

#### Step 3: Keep Updated

Suno periodically updates their client and API structure. Monitor community projects for authentication changes:
- https://github.com/gcui-art/suno-api (issues)
- https://github.com/SunoAI-API/Suno-API (issues)

---

## Core Retrieval Endpoints

**Note:** The following endpoints are based on reverse-engineering of Suno's internal API. Endpoints and response structures may change. Monitor community projects for updates.

### 1. Get Song Feed (Audio URL, Cover Art, Metadata)

**Endpoint:** `GET /feed/v2/{clip_id}`

**Base URL:** `https://studio-api.suno.ai` (may vary)

**Purpose:** Retrieve complete song information including audio URL, cover art image, and metadata.

**Request Parameters:**
- `clip_id` (path parameter): Unique identifier for the song

**Authentication:** `Authorization: Bearer <session_token>`

**Example Request:**
```bash
curl -X GET "https://studio-api.suno.ai/feed/v2/abc123xyz" \
  -H "Authorization: Bearer <your_session_token>" \
  -H "Content-Type: application/json"
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

### Feed Pagination (Bulk Retrieval)

**Endpoint:** `GET /feed/v2` (if available in your wrapper)

**Purpose:** Retrieve multiple songs with pagination support.

**Request Parameters:**
- `page` (query parameter): Page number (starts at 0 or 1)

**Authentication:** Bearer token (automatically injected)

**Example Request:**
```bash
curl -X GET "http://localhost:8000/feed/v2?page=1" \
  -H "Authorization: Bearer <auto_token>"
```

**Response Structure:**
```json
{
  "clips": [
    {
      "id": "abc123xyz",
      "title": "Song Title",
      "audio_url": "https://cdn.suno.ai/audio/abc123xyz.mp3",
      "image_url": "https://cdn.suno.ai/images/abc123xyz.jpg",
      "display_name": "Display Name"
    }
  ],
  "page": 1,
  "total_pages": 5
}
```

**Use Cases:**
- Build a song library/playlists
- Sync user's generated songs
- Browse songs with infinite scroll

**Note:** Check if your API wrapper implements the paginated feed endpoint. The single-song `/feed/{aid}` endpoint is the primary documented method.

---

### Important: clip_id vs task_id

**clip_id (Audio/Lyrics ID):**
- Identifies a specific song/generation
- Used for retrieving audio, lyrics, stems, timing
- **Format:** UUID (e.g., "a624123d-xxxx-xxxx-xxxx-xxxxxxxxxxxx")
- **Used in:** `/feed/{clip_id}`, `/lyrics/{clip_id}`, `/suno/stems`, `/suno/timing`

**task_id (Generation Task):**
- Identifies a generation request (may produce multiple songs)
- Used for checking generation status
- **Format:** UUID
- **Used in:** `/suno/fetch?task_id=xxx` (generation endpoints)

**Key Insight:**
- This guide focuses on **retrieval** using `clip_id`
- If you need song generation, you would work with `task_id` first, then use the returned `clip_id` for retrieval
- All retrieval operations in this guide expect a `clip_id` (also referred to as `aid` or `lid` in the documentation)

---

### 2. Get Song Lyrics

**Endpoint:** `GET /lyrics/{clip_id}`

**Base URL:** `https://studio-api.suno.ai` (may vary)

**Purpose:** Retrieve lyrics for a specific song.

**Request Parameters:**
- `clip_id` (path parameter): Song identifier (same as used in feed endpoint)

**Authentication:** `Authorization: Bearer <session_token>`

**Example Request:**
```bash
curl -X GET "https://studio-api.suno.ai/lyrics/abc123xyz" \
  -H "Authorization: Bearer <your_session_token>" \
  -H "Content-Type: application/json"
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
This API does **NOT** provide timestamped/aligned lyrics via the `/lyrics/{lid}` endpoint. The `/suno/timing` endpoint mentioned in other Suno API wrappers is **not available** in the official API.

For karaoke-style synchronization, you must implement timing locally using libraries or external projects.

---

## Local Lyric Timing Solutions

Since timing is not available from the Suno API, you have several local implementation options:

### Option 1: OpenAI Whisper (Recommended for Accuracy)

Whisper provides word-level timestamps and is relatively easy to integrate.

**Pros:**
- High accuracy across multiple languages
- Word-level timestamps built-in
- Active development, well-documented
- Open source (MIT licensed)

**Cons:**
- Heavier computational requirement
- May need GPU for real-time performance

**Integration Example (Python):**
```python
import whisper

def get_lyrics_timing(audio_path, lyrics_text=None):
    # Load model (tiny, base, small, medium, large)
    model = whisper.load_model("base")

    # Transcribe with timestamps
    result = model.transcribe(audio_path, word_timestamps=True)

    # Parse into lyric lines
    timed_lyrics = []
    for segment in result["segments"]:
        timed_lyrics.append({
            "text": segment["text"].strip(),
            "start_ms": int(segment["start"] * 1000),
            "end_ms": int(segment["end"] * 1000)
        })

    return timed_lyrics
```

**C++ Integration:**
Use `whisper.cpp` (C++ port of Whisper):
- GitHub: https://github.com/ggerganov/whisper.cpp
- Lightweight, portable
- Provides C API for easy Qt integration

---

### Option 2: Gentle Forced Alignment

**Gentle** is a forced alignment tool that aligns known text with audio.

**Pros:**
- Faster than Whisper
- Designed specifically for alignment (not transcription)
- Works well with known lyrics

**Cons:**
- Requires Kaldi models (heavier setup)
- Less accurate than Whisper for music

**GitHub:** https://github.com/lowerquality/gentle

---

### Option 3: DTW (Dynamic Time Warping) - Do It Yourself

Implement DTW to align lyrics with audio features (MFCCs).

**Pros:**
- No external dependencies
- Full control over algorithm
- Lightweight

**Cons:**
- Requires significant development effort
- Lower accuracy than ML-based solutions
- Complex to tune

**Basic Approach:**
1. Extract MFCC features from audio
2. Convert lyrics to phonemes
3. Use DTW to find optimal alignment path
4. Map alignment to timestamps

---

### Option 4: Hybrid Approach (Recommended for Production)

Combine coarse section markers with fine-grained alignment:

**Implementation:**
1. Use section markers (`[Verse]`, `[Chorus]`) for structure
2. Use Whisper for word-level timing within each section
3. Cache timing results per song

**Example Architecture:**
```cpp
class LyricTimingManager {
public:
    struct TimedLyric {
        QString text;
        int64_t startMs;
        int64_t endMs;
        QString section;  // Verse, Chorus, etc.
    };

    QList<TimedLyric> getTimedLyrics(const QString &audioPath,
                                      const QString &lyricsText) {
        QString cacheKey = generateCacheKey(audioPath, lyricsText);

        if (cacheExists(cacheKey)) {
            return loadFromCache(cacheKey);
        }

        // Run alignment (could be in a background thread)
        auto timedLyrics = runWhisperAlignment(audioPath, lyricsText);

        // Cache the result
        saveToCache(cacheKey, timedLyrics);

        return timedLyrics;
    }

private:
    QString runWhisperAlignment(const QString &audioPath,
                                  const QString &lyricsText) {
        // Call whisper.cpp or external Python script
        // Return timed lyrics
    }

    QString generateCacheKey(const QString &audioPath,
                             const QString &lyricsText) {
        return QCryptographicHash::hash(
            (audioPath + lyricsText).toUtf8(),
            QCryptographicHash::Sha256
        ).toHex();
    }
};
```

---

## Recommended Implementation Strategy

### For MVP (Minimum Viable Product):
1. Use **whisper.cpp** for word-level timing
2. Cache timing results per song ID
3. Display current lyric + next lyric for smooth karaoke experience

### For Production:
1. Implement timing generation as a background task
2. Use section markers from Suno lyrics for structure
3. Run Whisper alignment in worker thread
4. Cache results permanently (not just in-memory)
5. Add fallback: if timing fails, use coarse section sync

---

## Performance Considerations

**Whisper Model Selection:**
- `tiny`: Fastest, ~32MB, good enough for timing
- `base`: Better accuracy, ~74MB, recommended
- `small`: Good balance, ~244MB
- `medium/higher`: Diminishing returns for timing

**Caching Strategy:**
```cpp
struct LyricCacheEntry {
    QString songId;
    QString audioUrlHash;  // Hash of audio URL
    QList<TimedLyric> timedLyrics;
    QDateTime cachedAt;
    qint64 durationMs;  // To detect re-uploads
};
```

**Background Processing:**
```cpp
class TimingGenerator : public QObject {
    Q_OBJECT

public slots:
    void generateTiming(const QString &audioPath,
                       const QString &lyricsText) {
        // Run in background thread
        auto result = runAlignment(audioPath, lyricsText);
        emit timingReady(result);
    }

signals:
    void timingReady(const QList<TimedLyric> &lyrics);
    void timingFailed(const QString &error);
};
```

---

### 3. Get Account Credits

**Endpoint:** `GET /get_credits`

**Base URL:** `https://studio-api.suno.ai` (may vary)

**Purpose:** Check account credit balance and usage.

**Authentication:** `Authorization: Bearer <session_token>`

**Example Request:**
```bash
curl -X GET "https://studio-api.suno.ai/get_credits" \
  -H "Authorization: Bearer <your_session_token>" \
  -H "Content-Type: application/json"
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

### 4. Audio Separation (Stems)

**Endpoint:** `POST /suno/stems` (if available in your wrapper)

**Purpose:** Separate a song into vocal and instrumental tracks.

**Authentication:** Bearer token

**Request Body:**
```json
{
  "clip_id": "a624123d-xxxx"
}
```

**Response Structure:**
```json
{
  "clip_id": "a624123d-xxxx",
  "vocals_url": "https://cdn.suno.ai/stems/vocals_a624123d-xxxx.wav",
  "instrumental_url": "https://cdn.suno.ai/stems/instrumental_a624123d-xxxx.wav"
}
```

**Files Returned:**
- `vocals.wav` - Isolated vocal track
- `instrumental.wav` - Isolated instrumental track

**Use Cases:**
- Karaoke backing tracks (instrumental)
- Vocal-only analysis
- Remixing and sampling

**Note:** Check if your API wrapper implements this endpoint.

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

### Step 1: Configure Your Qt6 Application

Set up your application with Suno API base URL and authentication:

```cpp
#include <QNetworkAccessManager>
#include <QNetworkRequest>

class SunoApiClient {
public:
    SunoApiClient() {
        m_networkManager = new QNetworkAccessManager();
        m_baseUrl = "https://studio-api.suno.ai";  // May vary, verify with community projects
        m_sessionToken = "<your_bearer_token>";  // Load from secure storage
    }

    QNetworkRequest* createRequest(const QString &endpoint) {
        QUrl url(m_baseUrl + endpoint);
        QNetworkRequest *request = new QNetworkRequest(url);
        request->setRawHeader("Authorization", ("Bearer " + m_sessionToken).toUtf8());
        request->setRawHeader("Content-Type", "application/json");
        return request;
    }

private:
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
    QString m_sessionToken;
};
```

### Step 2: Retrieve Song Metadata

```cpp
void SunoApiClient::fetchSong(const QString &clipId) {
    QNetworkRequest *request = createRequest("/feed/v2/" + clipId);
    QNetworkReply *reply = m_networkManager->get(*request);

    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject song = doc.object();  // May need to access array first

            QString audioUrl = song["audio_url"].toString();
            QString coverArtUrl = song["image_large_url"].toString();
            QJsonObject metadata = song["metadata"].toObject();

            emit songFetched({
                {"id", song["id"].toString()},
                {"title", metadata["title"].toString()},
                {"audioUrl", audioUrl},
                {"coverArtUrl", coverArtUrl},
                {"duration", metadata["duration"].toDouble()}
            });
        }
        reply->deleteLater();
    });
}
```

### Step 3: Retrieve Lyrics

```cpp
void SunoApiClient::fetchLyrics(const QString &clipId) {
    QNetworkRequest *request = createRequest("/lyrics/" + clipId);
    QNetworkReply *reply = m_networkManager->get(*request);

    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject lyricsObj = doc.object();

            QString lyricsText = lyricsObj["text"].toString();
            QString title = lyricsObj["title"].toString();

            emit lyricsFetched(lyricsText, title);
        }
        reply->deleteLater();
    });
}
```

### Step 4: Download Files (Audio/Cover Art)

```cpp
void SunoApiClient::downloadFile(const QString &url, const QString &savePath) {
    QNetworkRequest request(QUrl(url));
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
        }
        reply->deleteLater();
    });
}
```

---

## Error Handling

### Common HTTP Status Codes

| Status | Meaning | Action |
|--------|----------|--------|
| 200 OK | Success | Parse response normally |
| 401 Unauthorized | Invalid/expired token | Re-authenticate with new session token |
| 403 Forbidden | Access denied | Check token permissions, re-authenticate |
| 404 Not Found | Endpoint or resource not found | Verify endpoint URL, check if Suno updated their API |
| 429 Too Many Requests | Rate limit exceeded | Implement backoff and retry |
| 500 Internal Server Error | Suno server error | Retry with exponential backoff |

### Error Scenarios

**Authentication Errors (401/403):**
- **Cause:** Session token expired or invalid
- **Solution:** Extract fresh session token from browser
- **Prevention:** Implement token refresh logic if supported

**Song Not Found (404):**
- **Cause:** Invalid clip_id or song deleted
- **Solution:** Verify clip_id is correct

**Rate Limited (429):**
- **Cause:** Exceeded Suno's rate limits (free tier: ~50 credits/day)
- **Solution:** Implement throttling, wait before retrying

**Song Still Processing (200, audio_url: null):**
- **Cause:** Song generation not complete
- **Solution:** Implement polling (see Polling Pattern below)

**Suno API Changes (500/404):**
- **Cause:** Suno updated their internal API
- **Solution:** Monitor community projects for authentication/endpoint updates

### Retry Strategy

```cpp
class SunoApiClient {
public:
    void fetchSongWithRetry(const QString &clipId, int maxRetries = 3) {
        int retryCount = 0;
        while (retryCount < maxRetries) {
            QNetworkReply *reply = makeRequest(clipId);
            connect(reply, &QNetworkReply::finished, [this, reply, &retryCount, maxRetries]() {
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (statusCode == 200 || statusCode == 404) {
                    // Success or definitive error
                    handleResponse(reply);
                } else if (statusCode >= 500) {
                    // Server error - retry
                    if (retryCount < maxRetries - 1) {
                        retryCount++;
                        QTimer::singleShot(2000 * (1 << retryCount), [this, clipId, retryCount, maxRetries]() {
                            fetchSongWithRetry(clipId, maxRetries);
                        });
                    } else {
                        emit errorOccurred("Max retries exceeded");
                    }
                } else if (statusCode == 401 || statusCode == 403) {
                    // Auth error - need new token
                    emit authenticationFailed();
                }
                reply->deleteLater();
            });
        }
    }
};
```

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
        // Load from secure storage - never hardcode!
        m_baseUrl = "https://studio-api.suno.ai";  // Verify current endpoint
        m_sessionToken = loadSecureToken();  // Implement secure storage
    }

    void fetchSong(const QString &clipId) {
        QUrl url(m_baseUrl + "/feed/v2/" + clipId);
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", ("Bearer " + m_sessionToken).toUtf8());
        request.setRawHeader("Content-Type", "application/json");

        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray responseData = reply->readAll();
                QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);

                // Response may be array or object depending on endpoint
                QJsonObject song;
                if (jsonDoc.isArray()) {
                    song = jsonDoc.array().first().toObject();
                } else {
                    song = jsonDoc.object();
                }

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
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (statusCode == 401 || statusCode == 403) {
                    emit authenticationFailed();
                } else {
                    emit errorOccurred(reply->errorString());
                }
            }
            reply->deleteLater();
        });
    }

    void fetchLyrics(const QString &clipId) {
        QUrl url(m_baseUrl + "/lyrics/" + clipId);
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", ("Bearer " + m_sessionToken).toUtf8());
        request.setRawHeader("Content-Type", "application/json");

        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray responseData = reply->readAll();
                QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
                QJsonObject lyricsObj = jsonDoc.object();

                QString lyricsText = lyricsObj["text"].toString();
                QString title = lyricsObj["title"].toString();

                emit lyricsFetched(lyricsText, title);
            } else {
                emit errorOccurred(reply->errorString());
            }
            reply->deleteLater();
        });
    }

    void downloadFile(const QString &url, const QString &savePath) {
        QNetworkRequest request(QUrl(url));

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
    void lyricsFetched(const QString &lyricsText, const QString &title);
    void downloadComplete(const QString &filePath);
    void authenticationFailed();
    void errorOccurred(const QString &errorMessage);

private:
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
    QString m_sessionToken;

    QString loadSecureToken() {
        // Implement secure storage (QtKeychain, OS keyring, etc.)
        // Return stored session token
        return "";
    }
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

### Test Endpoints with curl

```bash
# Set your token (obtain from browser)
TOKEN="<your_bearer_token>"
BASE_URL="https://studio-api.suno.ai"

# Test song retrieval
curl -X GET "$BASE_URL/feed/v2/abc123xyz" \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json"

# Test lyrics retrieval
curl -X GET "$BASE_URL/lyrics/abc123xyz" \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json"

# Test credits
curl -X GET "$BASE_URL/get_credits" \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json"
```

### C++ Test Application

```cpp
// Create a simple test application to verify connectivity
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QNetworkAccessManager manager;

    QString token = "<your_bearer_token>";
    QString clipId = "test_clip_id";  // Replace with valid ID

    QNetworkRequest request(QUrl("https://studio-api.suno.ai/feed/v2/" + clipId));
    request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    request.setRawHeader("Content-Type", "application/json");

    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug() << "Success!";
            qDebug() << reply->readAll();
        } else {
            qDebug() << "Error:" << reply->errorString();
        }
        qApp->quit();
    });

    return app.exec();
}
```

---

## Troubleshooting

### Common Issues

**Issue:** `audio_url` is null
- **Cause:** Song is still processing
- **Solution:** Implement polling with 5-30 second intervals

**Issue:** 401/403 Unauthorized
- **Cause:** Session token expired or invalid
- **Solution:** Extract fresh session token from browser session (see Prerequisites)

**Issue:** 404 Not Found
- **Cause:** Endpoint or song doesn't exist, or Suno updated their API
- **Solution:**
  1. Verify clip_id is correct
  2. Check if Suno updated endpoints (monitor community projects)
  3. Test endpoint with curl to verify it's reachable

**Issue:** 429 Too Many Requests
- **Cause:** Rate limit exceeded
- **Solution:**
  1. Check your account tier (free: ~50 credits/day)
  2. Implement client-side throttling
  3. Wait before retrying

**Issue:** 500 Internal Server Error
- **Cause:** Suno server issue or API changed
- **Solution:**
  1. Retry with exponential backoff
  2. Check community projects for reported issues
  3. Verify endpoints haven't changed

**Issue:** Download failures
- **Cause:** CDN timeout or network issues
- **Solution:** Implement retry logic with exponential backoff

**Issue:** Token keeps expiring every few days
- **Cause:** Clerk session tokens have limited lifetime
- **Solution:**
  1. Implement automatic re-authentication flow
  2. Prompt user to log in via browser when token expires
  3. Store multiple tokens if Suno supports refresh tokens

**Issue:** All requests suddenly start failing
- **Cause:** Suno updated their client or API
- **Solution:**
  1. Check community GitHub repositories for recent issues
  2. Look for authentication method changes
  3. May need to update token extraction method or endpoints

### Logging

Implement logging in your Qt6 application to track API interactions:

```cpp
#include <QDateTime>

class SunoApiClient : public QObject {
    // ... other code ...

private:
    void logRequest(const QString &method, const QString &endpoint) {
        qDebug() << QDateTime::currentDateTime().toString()
                 << "Request:" << method << endpoint;
    }

    void logResponse(int statusCode, const QByteArray &data) {
        qDebug() << QDateTime::currentDateTime().toString()
                 << "Response:" << statusCode
                 << "Size:" << data.size();
    }

    void logError(const QString &error) {
        qWarning() << QDateTime::currentDateTime().toString()
                  << "Error:" << error;
    }
};
```

**What to log:**
- All HTTP requests (method, endpoint, timestamp)
- HTTP response status codes
- Error messages
- Authentication failures
- Rate limit occurrences

**Note:** Don't log sensitive data (session tokens, cookie values)

---

## Security Considerations

### Credential Storage
- **Never hardcode session tokens** in source code
- Use secure storage mechanisms:
  - Linux: Secret Service API / GNOME Keyring / KDE Wallet
  - macOS: Keychain
  - Windows: Credential Manager
- Qt libraries: `qtkeychain` (https://github.com/frankosterfeld/qtkeychain)
- Never commit session tokens to version control (add to `.gitignore`)

### Token Management
- Session tokens from Clerk expire (typically 7-30 days)
- No official token refresh endpoint from Suno
- Implement automatic re-authentication:
  1. Detect 401/403 errors
  2. Prompt user to log in to browser
  3. Extract new session token
  4. Update secure storage
- Store only short-lived tokens in memory

### Network Security
- All requests to Suno use HTTPS by default
- Verify SSL certificates (Qt does this automatically)
- Don't disable SSL verification in production

### Privacy and Terms of Service
- **Important:** Reverse-engineering Suno's internal API may violate their Terms of Service
- This is a community-maintained integration, not officially supported by Suno
- Use at your own risk
- Consider rate limiting to avoid detection

### Data Protection
- Cache downloaded content responsibly
- Respect copyright when using downloaded songs
- Don't redistribute Suno-generated content without proper licensing

---

## API Reference Summary

**Note:** Endpoints are based on reverse-engineering of Suno's internal API. They may change without notice.

| Endpoint | Method | Purpose | Base URL | Auth |
|----------|--------|---------|-----------|------|
| `/feed/v2/{clip_id}` | GET | Get song metadata, audio URL, cover art | `studio-api.suno.ai` | Bearer token |
| `/lyrics/{clip_id}` | GET | Get song lyrics (plain text) | `studio-api.suno.ai` | Bearer token |
| `/get_credits` | GET | Get account credit info | `studio-api.suno.ai` | Bearer token |

**Important Notes:**
- Base URL may vary (monitor community projects for changes)
- Endpoints may change without notice
- Some community projects mention `/feed` (v1) vs `/feed/v2`
- Pagination support varies by endpoint

**Community Projects for Reference:**
- https://github.com/gcui-art/suno-api (monitor issues)
- https://github.com/SunoAI-API/Suno-API (monitor issues)
- These projects track Suno API changes in real-time

**Note on Timing:**
- **Timestamp-aligned lyrics are NOT available** in Suno's internal API
- Karaoke timing must be implemented locally using libraries like Whisper, Gentle, or custom DTW algorithms
- See the "Local Lyric Timing Solutions" section for implementation guidance

---

## Recommended Project Structure

```
src/
├── suno/
│   ├── SunoApiClient.hpp       # API client header
│   ├── SunoApiClient.cpp       # API client implementation
│   ├── SunoTypes.hpp          # Data structures (Song, Lyrics, etc.)
│   └── TokenManager.hpp       # Secure token storage/retrieval
├── audio/
│   ├── AudioEngine.hpp          # Qt6 audio playback
│   └── AudioAnalyzer.hpp       # For timing generation (optional)
├── ui/
│   ├── MainWindow.hpp           # Main Qt window
│   ├── LyricsOverlay.hpp        # Karaoke display widget
│   └── SettingsDialog.hpp      # Configuration dialog
└── util/
    ├── CacheManager.hpp         # File caching
    └── Logger.hpp             # Application logging
tests/
├── SunoApiClientTest.cpp       # API client tests
└── AudioPlaybackTest.cpp       # Audio playback tests
docs/
└── SUNO_API_CPP_QT6_INTEGRATION_GUIDE.md  # This document
CMakeLists.txt                  # CMake build configuration
README.md                      # Project documentation
```

---

## Conclusion

This guide provides complete information for making **direct HTTP requests to Suno's internal API** to retrieve songs, lyrics, and cover art. The key capabilities for your music video creator are:

1. **Complete song metadata** (title, duration, creation date, tags)
2. **Audio file URLs** (for playback)
3. **Cover art images** (for display)
4. **Lyrics** (plain text with section markers)
5. **Account credit information** (to track usage)

### What This Integration Provides

**Retrieval-Only Access:**
- Get existing Suno songs by clip_id
- Fetch lyrics and cover art
- Download audio files for local playback
- Check account credit balance

**Direct Communication:**
- No intermediary server or proxy
- Your Qt6 application communicates directly with `studio-api.suno.ai`
- Uses Clerk session authentication (browser cookies)

### Karaoke Timing Implementation

**Critical:** Suno's internal API does NOT provide timestamp-aligned lyrics. You must implement timing locally:

**Recommended Approach:**
1. Use **whisper.cpp** (C++ port of OpenAI Whisper) for word-level timing
   - GitHub: https://github.com/ggerganov/whisper.cpp
   - Models: `tiny` (fastest), `base` (recommended balance), `small`
   - Integrates easily with Qt6 via C API

2. Cache timing results per song to avoid reprocessing

3. Implement background processing for timing generation

**Alternative Options:**
- **Gentle Forced Alignment** (https://github.com/lowerquality/gentle)
- **Custom DTW implementation** (lightweight but less accurate)

**Last Resort:**
- Use section markers (`[Verse]`, `[Chorus]`) from lyrics for coarse sync only

### Implementation Summary

**Retrieval Flow:**
1. Load session token from secure storage
2. Fetch song metadata from `/feed/v2/{clip_id}`
3. Fetch lyrics from `/lyrics/{clip_id}`
4. Download audio file and cover art
5. Generate timing locally (whisper.cpp)
6. Cache everything for replay

**Playback Flow:**
1. Load cached or newly generated timing
2. Sync lyrics display with audio position
3. Display current + next lyric for smooth karaoke experience

### Important Warnings and Caveats

**No Official API:**
- This is reverse-engineered documentation
- Suno may change endpoints, authentication, or response formats
- Monitor community projects for changes

**Terms of Service:**
- Using Suno's internal API may violate their ToS
- Use at your own risk
- Implement rate limiting to avoid detection

**Authentication Reliability:**
- Session tokens expire (7-30 days typically)
- No official refresh endpoint
- Must re-authenticate via browser when tokens expire

**Endpoint Instability:**
- Endpoints may change without notice
- Always test with curl before implementing
- Implement fallback/error handling gracefully

**Rate Limiting:**
- Free tier: ~50 credits/day (~10 songs)
- Implement client-side throttling
- Respect Suno's limits to avoid IP blocking

### Community Resources

Monitor these projects for Suno API changes:
- https://github.com/gcui-art/suno-api/issues
- https://github.com/SunoAI-API/Suno-API/issues

### What's NOT Covered

This guide focuses on **retrieval** only. The following are not covered:
- Song generation (creating new music)
- Extending/continuing songs
- Remixing or style transfer
- Custom voice/persona creation

If you need generation capabilities, research community projects that have reverse-engineered those endpoints.

---

**Last Updated:** January 13, 2026
**API Status:** Unofficial - Based on reverse-engineering of Suno's internal API
**Base URL:** `https://studio-api.suno.ai` (subject to change)
**Authentication:** Clerk session tokens (extracted from browser)

---

**⚠️ IMPORTANT DISCLAIMER:**

1. **No Official API:** This is reverse-engineered documentation. Suno does not provide an official public API.
2. **ToS Violation Risk:** Using Suno's internal endpoints may violate their Terms of Service. Use at your own risk.
3. **Breaking Changes:** Suno can change endpoints, authentication, or response formats at any time without notice.
4. **Rate Limiting:** Excessive requests may result in IP blocking. Implement respectful rate limiting.
5. **Legal Responsibility:** You are responsible for ensuring compliance with Suno's ToS and applicable laws.
