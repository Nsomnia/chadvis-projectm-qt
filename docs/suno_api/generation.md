# Suno Generation API

Bearer JWT required for all endpoints below unless otherwise noted.

## Music Generation

### `POST /api/generate/v2/`
**Purpose**: Main song generation endpoint for all supported models.
**Auth**: Bearer JWT
**Body**: `{ make_instrumental, mv, prompt, style, title, tags, negative_tags, continue_clip_id, continue_at, cover_clip_id, image_url, wait_audio, task_id, style_weight, weirdness_constraint, audio_weight, vocal_gender, persona_id, persona_model }`
**Response**: `{ task_id, clip_id?, status, created_at?, message? }`
**Notes**: Async by default. `mv` selects model version; supported values include `chirp-v3`, `chirp-v3-5`, `chirp-v4`, `chirp-v4-5`, `chirp-v5`, `chirp-v5-5`, and `chirp-crow`. Use `continue_clip_id`/`continue_at` to extend, `cover_clip_id` to generate a cover, and `wait_audio=true` to hold the request until audio is available when supported.

### `POST /api/generate/v2-web/`
**Purpose**: Web-flavored variant of v2 generation.
**Auth**: Bearer JWT
**Body**: `{ make_instrumental, mv, prompt, style, title, tags, negative_tags, continue_clip_id, continue_at, cover_clip_id, image_url, wait_audio, task_id, style_weight, weirdness_constraint, audio_weight, vocal_gender, persona_id, persona_model }`
**Response**: `{ task_id, clip_id?, status, created_at?, message? }`
**Notes**: Same core payload as `/api/generate/v2/`; web clients typically use this path. Treat it as async and poll the returned id.

## Lyrics Generation

### `POST /api/generate/lyrics/`
**Purpose**: Start lyrics generation.
**Auth**: Bearer JWT
**Body**: `{ prompt, style, title, mv, task_id, make_instrumental, tags, negative_tags }`
**Response**: `{ id, status }`
**Notes**: Returns a lyrics job id. Poll `GET /api/generate/lyrics/{id}` until completion.

### `GET /api/generate/lyrics/{id}`
**Purpose**: Poll lyrics generation job status.
**Auth**: Bearer JWT
**Body**: `{}`
**Response**: `{ id, status, lyrics?, error? }`
**Notes**: `status` is typically `pending`, `complete`, or `error`.

### `POST /api/generate/lyrics-infill`
**Purpose**: Fill missing lyrics within an existing structure.
**Auth**: Bearer JWT
**Body**: `{ lyrics, prompt, style, title, mv, task_id }`
**Response**: `{ id, status, lyrics? }`
**Notes**: Use when you have partial lyrics and want Suno to complete them.

### `POST /api/generate/lyrics-mashup`
**Purpose**: Generate mixed/combined lyrics from source material.
**Auth**: Bearer JWT
**Body**: `{ lyrics, prompt, style, title, mv, task_id }`
**Response**: `{ id, status, lyrics? }`
**Notes**: Intended for remix-style lyric synthesis.

### `POST /api/generate/lyrics-pair`
**Purpose**: Generate paired lyrics (two-part or dual-voice structure).
**Auth**: Bearer JWT
**Body**: `{ prompt, style, title, mv, task_id }`
**Response**: `{ id, status, pair_lyrics? }`
**Notes**: Output format depends on the active model and prompt constraints.

### `POST /api/generate/lyrics-pair/rate`
**Purpose**: Rate a generated lyrics pair.
**Auth**: Bearer JWT
**Body**: `{ id, rating, feedback? }`
**Response**: `{ success, id }`
**Notes**: Used for feedback loops and model tuning.

## Concat / Merge

### `POST /api/generate/concat/v2/`
**Purpose**: Merge multiple extensions into a full song.
**Auth**: Bearer JWT
**Body**: `{ clip_ids, task_id, mv, prompt, style }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Used after segmented generation or chained extensions.

### `POST /api/generate/merge/`
**Purpose**: Alternative merge endpoint for combining generated segments.
**Auth**: Bearer JWT
**Body**: `{ clip_ids, task_id, mv, prompt, style }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Functionally overlaps with concat; backend behavior may differ by model/version.

## Extend / Continue

### `POST /api/extend_audio`
**Purpose**: Third-party style audio extension endpoint.
**Auth**: Bearer JWT
**Body**: `{ continue_clip_id, continue_at, prompt, style, mv, task_id }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Alternative extension path. `continue_clip_id` is the source UUID; `continue_at` is a timestamp such as `2:30`.

### `POST /api/generate/v2/`
**Purpose**: Continue an existing clip via generation.
**Auth**: Bearer JWT
**Body**: `{ continue_clip_id, continue_at, prompt, style, title, tags, negative_tags, mv, task_id }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Preferred native path for extension. `continue_at` selects the splice point.

## Cover

### `POST /api/generate/v2/`
**Purpose**: Generate a cover of an existing track.
**Auth**: Bearer JWT
**Body**: `{ cover_clip_id, prompt, style, title, tags, negative_tags, mv, task_id, image_url }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Set `cover_clip_id` to the source clip UUID. `image_url` overrides cover art when accepted.

### `POST /api/upload-and-cover/`
**Purpose**: Upload audio and generate a cover from it.
**Auth**: Bearer JWT
**Body**: `{ file, cover_clip_id?, prompt, style, mv, task_id }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Third-party style path; exact multipart field names may vary by client implementation.

## Remix

### `POST /api/generate/v2/`
**Purpose**: Remix an existing clip using modified generation parameters.
**Auth**: Bearer JWT
**Body**: `{ clip_id, prompt, style, title, tags, negative_tags, mv, style_weight, weirdness_constraint, audio_weight, vocal_gender, task_id }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Remix behavior is driven by prompt plus control parameters. Often paired with `audio_weight` and `style_weight`.

## Aligned Lyrics

### `GET /api/gen/{id}/aligned_lyrics/v2/`
**Purpose**: Fetch word-timed aligned lyrics for a generated clip.
**Auth**: Bearer JWT
**Body**: `{}`
**Response**: `{ aligned_lyrics: [{ word, start_time, end_time, line_index? }], language?, status }`
**Notes**: Used for karaoke-style rendering. If unavailable, the clip may return a partial or empty alignment payload.

## Upsample

### `POST /api/generate/upsample`
**Purpose**: Increase audio quality / regenerate at higher fidelity.
**Auth**: Bearer JWT
**Body**: `{ clip_id, task_id, mv, prompt? }`
**Response**: `{ task_id, clip_id?, status }`
**Notes**: Typically run on an existing clip id. Output is asynchronous.

## WAV Conversion

### `POST /api/gen/{id}/convert_wav/`
**Purpose**: Convert a generated clip to WAV.
**Auth**: Bearer JWT
**Body**: `{}`
**Response**: `{ status, wav_url?, clip_id? }`
**Notes**: Usually a post-processing action on an existing generation id.

## Recommendations

### `POST /api/generate/get_recommend_styles`
**Purpose**: Return style recommendations for a prompt or track context.
**Auth**: Bearer JWT
**Body**: `{ prompt, title, tags, mv, clip_id? }`
**Response**: `{ styles: [string], tags: [string], prompt_suggestions?: [string] }`
**Notes**: Useful for prompt refinement and style expansion.

## Generation Status / Polling

### `GET /api/gen/{id}`
**Purpose**: Poll generation status by generation id.
**Auth**: Bearer JWT
**Body**: `{}`
**Response**: `{ id, status, audio_url?, stream_audio_url?, image_url?, prompt?, title?, tags?, duration?, model_name?, create_time?, error? }`
**Notes**: Common statuses: `pending`, `text_complete`, `first_complete`, `complete`, `error`.

### `GET /api/clip/{id}`
**Purpose**: Poll clip status by clip id.
**Auth**: Bearer JWT
**Body**: `{}`
**Response**: `{ id, status, audio_url?, stream_audio_url?, image_url?, prompt?, title?, tags?, duration?, model_name?, create_time?, error? }`
**Notes**: Equivalent polling shape for clip-centric flows.

## Key Parameters

- `mv`: `chirp-v3`, `chirp-v3-5`, `chirp-v4`, `chirp-v4-5`, `chirp-v5`, `chirp-v5-5`, `chirp-crow`
- `prompt`: max 3000 chars on V4; max 5000 chars on V4.5+
- `style`: max 200 chars
- `title`: max 80 chars
- `tags`: comma-separated style tags
- `negative_tags`: tags to suppress or avoid
- `style_weight`: `0.00`-`1.00`
- `weirdness_constraint`: `0.00`-`1.00`
- `audio_weight`: `0.00`-`1.00`
- `vocal_gender`: `"m"` or `"f"`
- `make_instrumental`: boolean
- `continue_clip_id`: source clip UUID for continuation
- `continue_at`: timestamp such as `"2:30"`
- `cover_clip_id`: source clip UUID for cover/remix flows
- `image_url`: custom cover art URL
- `persona_id`: persona UUID
- `persona_model`: `"style_persona"` or `"voice_persona"` (V5 only)

## Common Async Pattern

1. `POST` returns `task_id` or `clip_id`.
2. Poll `GET /api/gen/{id}` or `GET /api/clip/{id}`.
3. Final payload includes `audio_url`, `stream_audio_url`, `image_url`, `title`, `tags`, `duration`, and `model_name`.
4. Handle rate limiting: `430` = too frequent, `429` = insufficient credits.
