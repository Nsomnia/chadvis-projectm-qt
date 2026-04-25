# Suno Persona & Voice API

Bearer JWT required for all endpoints below unless otherwise noted.

## Persona

### `POST /api/persona/create/`
**Purpose**: Create a voice persona.
**Auth**: Bearer JWT
**Body**: Persona creation payload.
**Response**: Persona object.
**Notes**: Voice persona creation surface.

### `GET /api/persona/get-personas/`
**Purpose**: List the user's personas.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Persona list payload.
**Notes**: Primary persona inventory endpoint.

### `GET /api/persona/get-followed-personas/`
**Purpose**: List followed personas.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Followed persona list.
**Notes**: Social/follow graph surface.

### `GET /api/persona/get-loved-personas/`
**Purpose**: List loved personas.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Loved persona list.
**Notes**: Favorites/likes-style persona collection.

### `GET /api/persona/get-persona-paginated/{id}/`
**Purpose**: Retrieve persona data with pagination.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Paginated persona payload.
**Notes**: Used for larger persona result sets.

### `POST /api/custom-model/create/`
**Purpose**: Create a custom model.
**Auth**: Bearer JWT
**Body**: Custom model creation payload.
**Response**: Custom model object.
**Notes**: V5.5 feature; documented limit is up to 3 per Pro/Premier account.

### `GET /api/custom-model/pending/`
**Purpose**: List pending custom models.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Pending custom model list.
**Notes**: Tracks training or review state.

### `POST /api/custom-model/archive/`
**Purpose**: Archive a custom model.
**Auth**: Bearer JWT
**Body**: Archive payload.
**Response**: Archive acknowledgment.
**Notes**: Removes the model from active use without deleting history.

## Voice

### `POST /api/processed_clip/voice-vox-stem`
**Purpose**: Process voice/stem data for personas.
**Auth**: Bearer JWT
**Body**: Processing payload.
**Response**: Processing job/result payload.
**Notes**: Persona voice pipeline surface.

### `GET /api/gen/{clip_id}/voice-*`
**Purpose**: Access voice-related generation endpoints.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Voice-related clip/generation payload.
**Notes**: Wildcard denotes multiple clip-specific voice endpoints.

## Voice Cloning

- V5.5 feature
- Requires verification
- Available on Pro/Premier

## Generation Parameter

- `vocal_gender`: `"m"` or `"f"`
- Used in generation requests where vocal routing is supported
