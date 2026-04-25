# Suno Upload & Processing API

Bearer JWT required for all endpoints below unless otherwise noted.

## Audio Upload

### `POST /api/uploads/audio/`
**Purpose**: Initialize an audio upload.
**Auth**: Bearer JWT
**Body**: Upload initialization payload.
**Response**: Upload session object.
**Notes**: Starts the audio upload flow before file transfer completes.

### `POST /api/uploads/audio/{id}/initialize-clip/`
**Purpose**: Initialize a clip from an uploaded audio asset.
**Auth**: Bearer JWT
**Body**: Initialization payload.
**Response**: Clip initialization object.
**Notes**: Bridges upload state to generation/editing state.

### `POST /api/uploads/audio/{id}/upload-finish/`
**Purpose**: Mark an audio upload complete.
**Auth**: Bearer JWT
**Body**: Completion payload.
**Response**: Upload completion acknowledgment.
**Notes**: Finalization step for multipart or chunked audio uploads.

### `GET /api/uploads/audio/{id}/`
**Purpose**: Poll audio upload processing status.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Upload status payload.
**Notes**: Use for async conversion/processing progress.

### `POST /api/uploads/audio/{id}/convert_wav/`
**Purpose**: Convert an uploaded audio file to WAV.
**Auth**: Bearer JWT
**Body**: None or conversion options.
**Response**: Conversion result payload.
**Notes**: Post-upload audio format conversion.

**Limit**: Maximum upload length is 2 minutes.

## Image Upload

### `POST /api/uploads/image`
**Purpose**: Upload cover art or another image asset.
**Auth**: Bearer JWT
**Body**: Multipart image upload.
**Response**: Image upload result.
**Notes**: Main image upload path.

### `POST /api/uploads/image/`
**Purpose**: Alternative image upload endpoint.
**Auth**: Bearer JWT
**Body**: Multipart image upload.
**Response**: Image upload result.
**Notes**: Trailing-slash variant of the same surface.

## Video Upload

### `GET /api/uploads/video/`
**Purpose**: Fetch video upload info.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Video upload metadata.
**Notes**: Bootstrap/info endpoint for video upload flows.

### `POST /api/uploads/video/{upload_id}/upload-finish/`
**Purpose**: Finalize a video upload.
**Auth**: Bearer JWT
**Body**: Completion payload.
**Response**: Upload completion acknowledgment.
**Notes**: Marks the video asset as fully uploaded.

## Stem Processing

### `POST /api/edit/stems/{clip_id}/`
**Purpose**: Edit stems for a clip.
**Auth**: Bearer JWT
**Body**: Stem edit payload.
**Response**: Stem edit job/result payload.
**Notes**: Advanced post-processing endpoint.

### `GET /api/clip/{clip_id}/stems`
**Purpose**: Fetch stem data for a clip.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Stem metadata payload.
**Notes**: Returns stem separation data when available.

### `GET /api/clip/{clip_id}/stems/pages`
**Purpose**: Fetch paged stem data for a clip.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Stem page payload.
**Notes**: Supports page/segment-based stem retrieval.

### `POST /api/generate/stems`
**Purpose**: Generate stems via third-party-style processing.
**Auth**: Bearer JWT
**Body**: Stem generation payload.
**Response**: Stem generation job/result payload.
**Notes**: External/third-party style surface.

**Capability**: 12-stem separation available in V5.

## Video Generation

### `POST /api/video/hooks/create`
**Purpose**: Create a video hook.
**Auth**: Bearer JWT
**Body**: Hook creation payload.
**Response**: Hook object.
**Notes**: Video generation orchestration surface.

### `GET /api/video/hooks/feed`
**Purpose**: Fetch the video hooks feed.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Hook feed payload.
**Notes**: List-style endpoint for available hooks.

### `GET /api/video/hooks/{hook_id}/flag`
**Purpose**: Flag a video hook.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Flag acknowledgment.
**Notes**: Moderation or reporting action.

### `GET /api/video/generate/{clip_id}/status/`
**Purpose**: Get video generation status.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Status payload.
**Notes**: Polling endpoint for async video jobs.

### `POST /api/video/hooks/create`
**Purpose**: Create a video hook (third-party style).
**Auth**: Bearer JWT
**Body**: Hook creation payload.
**Response**: Hook object.
**Notes**: Same endpoint as above; documented separately because of alternate client usage.

### `GET /api/video_gen/pending_batches`
**Purpose**: List pending video batches.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Pending batch list.
**Notes**: Batch queue visibility endpoint.

### `GET /api/video_gen/poll_batches`
**Purpose**: Poll video batches.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Batch status payload.
**Notes**: Used to track batch completion.

## OpenAI Speech

### `GET /api/openai-speech/`
**Purpose**: OpenAI-compatible speech endpoint.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Speech payload.
**Notes**: Compatibility surface; exact request/response shape follows the implemented speech backend.

## Deepgram Token

### `GET /api/deepgram-token`
**Purpose**: Retrieve a Deepgram token for transcription.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Token payload.
**Notes**: Used for transcription and speech-processing integrations.
