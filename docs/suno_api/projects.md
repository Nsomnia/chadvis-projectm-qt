# Suno Projects & Studio API

Bearer JWT required for all endpoints below unless otherwise noted.

## Projects

### `GET /api/project/me`
**Purpose**: List the authenticated user's projects.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Project list payload.
**Notes**: User-scoped listing; primary entry point for personal project inventory.

### `GET /api/project`
**Purpose**: List all projects.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Project list payload.
**Notes**: Broader collection view than `/me`; may include shared or discoverable projects.

### `POST /api/project`
**Purpose**: Create a project.
**Auth**: Bearer JWT
**Body**: Project creation payload.
**Response**: Created project object.
**Notes**: Used for new project records before clips/studio content are attached.

### `GET /api/project/trash`
**Purpose**: List trashed projects.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Trashed project list.
**Notes**: Soft-delete container; project recovery may be handled elsewhere.

### `GET /api/project/invites`
**Purpose**: List project invites.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Invite list payload.
**Notes**: Shows pending and/or accepted collaboration invitations.

### `GET /api/project/{project_id}`
**Purpose**: Fetch project details.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Project object.
**Notes**: Canonical project detail endpoint.

### `GET /api/project/{project_id}/clips`
**Purpose**: List clips belonging to a project.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Clip collection payload.
**Notes**: Project-to-clip relationship surface.

### `GET /api/project/{project_id}/metadata`
**Purpose**: Fetch project metadata.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Metadata payload.
**Notes**: Typically contains descriptive fields, timestamps, and versioning info.

### `GET /api/project/{project_id}/pinned-clips`
**Purpose**: List pinned clips for a project.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Pinned clip list.
**Notes**: Used for highlighted or sticky project clips in UI.

### `GET /api/project/{project_id}/collaborators`
**Purpose**: List project collaborators.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Collaborator list payload.
**Notes**: Includes collaborator roles and membership state when available.

### `GET /api/project/{project_id}/collaborators/me`
**Purpose**: Fetch the authenticated user's collaborator status for the project.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Collaborator membership object.
**Notes**: Useful for permission checks and UI state.

### `GET /api/project/{project_id}/ably-token`
**Purpose**: Get an Ably token for realtime collaboration.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Ably token payload.
**Notes**: Realtime transport credential for project synchronization.

### `GET /api/project/{project_id}/ably-client-id`
**Purpose**: Get the Ably client identifier.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Client ID payload.
**Notes**: Used to identify the local participant in realtime sessions.

### `POST /api/project/{project_id}/invite`
**Purpose**: Invite a collaborator.
**Auth**: Bearer JWT
**Body**: Invite payload.
**Response**: Invite acknowledgment.
**Notes**: Collaboration mutation endpoint; may require permission to invite.

### `POST /api/project/{project_id}/ably-update`
**Purpose**: Push a realtime update via Ably.
**Auth**: Bearer JWT
**Body**: Update payload.
**Response**: Update acknowledgment.
**Notes**: Used for live project state propagation.

## Studio

### `POST /api/studio/create-project`
**Purpose**: Create a Studio project.
**Auth**: Bearer JWT
**Body**: Studio project creation payload.
**Response**: Studio project object.
**Notes**: Studio-specific project bootstrap endpoint.

### `POST /api/studio/save-project`
**Purpose**: Persist Studio project state.
**Auth**: Bearer JWT
**Body**: Studio save payload.
**Response**: Save acknowledgment or updated project object.
**Notes**: Canonical save path for timeline and multitrack state.

### `GET /api/studio/render-state`
**Purpose**: Fetch current render state.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Render state payload.
**Notes**: Used to restore or inspect Studio rendering progress.

### `GET /api/studio/render-state-multitrack`
**Purpose**: Fetch multitrack render state.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Multitrack render state payload.
**Notes**: Multitrack-specific render tracking surface.

### `GET /api/studio/project-version/{id}`
**Purpose**: Fetch a Studio project version.
**Auth**: Bearer JWT
**Body**: None.
**Response**: Project version object.
**Notes**: Documented from `studio?project_version_id=...` URL usage.

## Studio Features

- Multitrack timeline editing
- Stem generation: bass, drums, synth, etc.
- BPM control and pitch adjustment
- Time signature support (`v1.2`)
- Warp Markers (`v1.2`)
- Remove FX (`v1.2`)
- Alternates lane (`v1.2`)
- Export: Full Song, Selected Time Range, Multitracks
- Premier plan only
