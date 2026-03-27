# Suno API Endpoint Reference

> **For:** AI agents implementing Suno API integration on `refactor/qml-modern-gui`
> **Version:** 1.0.0 - 2026-03-27
> **Source:** User-provided sniffed endpoints + community reverse-engineering

---

## Authentication

Suno uses **Clerk.dev** for authentication. Two mechanisms:

1. **Session Cookies**: `__session` and `__client` extracted from browser
2. **JWT Bearer Token**: Extracted from `__session` cookie (starts with `eyJ`) or obtained via Clerk token endpoint

### Token Refresh Flow
```
1. GET clerk.suno.com/v1/client  (with cookies)  → Session ID (sid)
2. POST clerk.suno.com/v1/client/sessions/${sid}/tokens  → Fresh JWT
3. JWT used as: Authorization: Bearer <token>
```

### Keep-Alive
- Tokens expire after ~24 hours
- The existing `SunoClient` emits `tokenChanged` signal → auto-saved to `config.toml`
- A background worker should refresh every 5 minutes during active use

---

## Core Endpoints

### Feed / Library

| Endpoint | Method | Purpose | Auth |
|----------|--------|---------|------|
| `https://studio-api.prod.suno.com/api/feed/v2` | GET | Paginated library feed | Bearer |
| `https://studio-api.suno.ai/api/feed/{aid}` | GET | Single song by audio ID | Bearer |

**Parameters:**
- `page` (integer, starts at 0 or 1)

**Response:** Array of clip objects containing `id`, `audio_url`, `image_url`, `display_name`, metadata

### Lyrics

| Endpoint | Method | Purpose | Auth |
|----------|--------|---------|------|
| `https://studio-api.suno.ai/api/lyrics/{lid}` | GET | Get lyrics by lyrics ID | Bearer |

**Response:** `{ id, text, title, format: "plain_text", metadata: { line_count, word_count, structure } }`

**Note:** Plain text only — no word-level timestamps. For karaoke sync, use whisper.cpp forced alignment.

### Credits

| Endpoint | Method | Purpose | Auth |
|----------|--------|---------|------|
| `https://studio-api.suno.ai/api/get_credits` | GET | Account credit balance | Bearer |

**Response:** `{ credits_left, period, monthly_limit, monthly_usage }`

---

## Extended Endpoints (From Sniffed Data)

> **NOTE:** The user will provide locally sniffed endpoints in an agent coding desktop program. Below are the known categories to implement. Parse the user's data and populate exact URLs as they become available.

### Generation (Text-to-Music)

| Endpoint | Method | Purpose | Notes |
|----------|--------|---------|-------|
| `/api/generate/v2/` | POST | Generate new song from prompt | Returns task ID, must poll for completion |
| `/api/generate/lyrics/v2/` | POST | Generate lyrics from prompt | Returns lyrics ID |

### Extension / Variation

| Endpoint | Method | Purpose | Notes |
|----------|--------|---------|-------|
| `/api/extend/v2/` | POST | Extend an existing clip | Requires source clip ID |
| `/api/generate/clip/v2/` | POST | Generate variation of clip | B-side style variation |

### B-Side & Experimental (Undocumented)

| Endpoint | Method | Purpose | Notes |
|----------|--------|---------|-------|
| `/api/generate/b-side/` | POST | B-side generation | Undocumented, parse from sniffed data |
| `/api/generate/experimental/` | POST | Experimental features | Undocumented, parse from sniffed data |

### Task Polling

| Endpoint | Method | Purpose | Notes |
|----------|--------|---------|-------|
| `/api/feed/{task_id}` | GET | Poll generation status | `audio_url` is null until complete |
| SSE/WebSocket endpoint | WS | Real-time generation updates | TBD from sniffed data |

---

## Response Structures

### Clip Object (from Feed)
```json
{
  "id": "uuid-string",
  "audio_url": "https://cdn.suno.ai/audio/xxx.mp3",
  "video_url": "https://cdn.suno.ai/video/xxx.mp4",
  "image_url": "https://cdn.suno.ai/images/xxx.jpg",
  "image_large_url": "https://cdn.suno.ai/images/xxx_large.jpg",
  "metadata": {
    "title": "Song Title",
    "tags": "pop, upbeat, female vocals",
    "description": "Prompt text",
    "duration": 184.5,
    "created_at": "2025-01-13T10:30:00Z",
    "play_count": 1234,
    "like_count": 56,
    "is_instrumental": false,
    "model_name": "chirp-v3-5"
  }
}
```

### Generation Response
```json
{
  "id": "task-uuid",
  "clips": [/* array of clip objects, initially with null audio_url */]
}
```

**Polling:** Check `GET /api/feed/{clip_id}` until `audio_url` is non-null. Recommended interval: 5-30 seconds, max wait: 120 seconds.

---

## Rate Limits

| Resource | Limit | Notes |
|----------|-------|-------|
| Library fetch | 1 req/5sec | Implemented in existing queue |
| Lyrics fetch | 1 req/2sec | Implemented in existing queue |
| Generation | Credit-based | Free tier: 50 credits/day (~10 songs) |
| General API | Varies | Suno backend enforces, no local limit needed |

---

## Error Handling

All errors return:
```json
{
  "code": 500,
  "msg": "An error occurred: <error_message>",
  "data": null
}
```

| Scenario | HTTP Status | Cause |
|----------|-------------|-------|
| Invalid Song ID | 500 | Song does not exist |
| Auth Failed | 500 | Expired/invalid cookie or token |
| Song Processing | 200 | `audio_url` is null — poll again |
| Rate Limited | 429 | Too many requests, back off |

---

## C++ Integration Notes

- Use `QNetworkAccessManager` (existing) or evaluate `libcpr/cpr` replacement
- Token management: existing `SunoClient` pattern with `tokenChanged` signal → config save
- Database: existing `SunoDatabase` with auto-migration
- Rate limiting: existing queue system with concurrency limits

---

## TODO for Agents

- [ ] Parse user's locally sniffed endpoints when provided
- [ ] Map exact URLs for `/b-side` and `/experimental` endpoints
- [ ] Implement generation polling with task ID tracking
- [ ] Implement SSE/WebSocket listener if available
- [ ] Update this document with discovered endpoints
