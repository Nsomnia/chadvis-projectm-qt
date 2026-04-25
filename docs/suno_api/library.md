# Suno Library & Feed API

Bearer JWT required for all endpoints below.

## Feed Endpoints

### `GET /api/feed/`
**Purpose**: Legacy library/feed listing.
**Auth**: Bearer JWT
**Params**: `ids` (comma-separated clip/song IDs), `page` (integer page cursor).
**Response**: Paginated feed payload containing clips/songs and paging metadata.
**Notes**: Older feed surface. Prefer `v2`/`v3` for newer clients.

### `GET /api/feed/v2`
**Purpose**: Updated library/feed listing with filter flags.
**Auth**: Bearer JWT
**Params**: `ids`, `page`, `is_public`, `liked`, `trashed`, `hide_disliked`, `hide_gen_stems`, `hide_studio_clips`.
**Response**: Feed object with clip list and pagination fields.
**Notes**: Supports server-side filtering for public, liked, trashed, and hidden clip classes.

### `POST /api/feed/v3`
**Purpose**: Latest feed endpoint.
**Auth**: Bearer JWT
**Params**: Cursor-based request body; commonly includes `next_cursor` or an initial cursor selector plus filter fields.
**Response**: `{ items, next_cursor, has_more }`-style paginated payload.
**Notes**: Cursor pagination replaces page numbers; this is the preferred feed API for modern clients.

### `GET /api/unified/feed`
**Purpose**: Unified feed across library and related surfaces.
**Auth**: Bearer JWT
**Params**: Standard feed filters and pagination parameters as supported by the backend.
**Response**: Unified list payload with feed items and paging metadata.
**Notes**: Backend may merge multiple content sources into one normalized response.

### `GET /api/unified/homepage`
**Purpose**: Homepage feed data.
**Auth**: Bearer JWT
**Params**: Homepage filter and personalization parameters, if accepted by the client.
**Response**: Homepage sections/cards/feed payload.
**Notes**: Used to populate the main landing experience.

### `GET /api/unified/homepage/explore`
**Purpose**: Explore-page feed data.
**Auth**: Bearer JWT
**Params**: Explore filters, sort keys, and paging parameters.
**Response**: Explore collection payload with sections or result lists.
**Notes**: Intended for desktop/web explore views.

### `GET /api/unified/homepage/explore/mobile`
**Purpose**: Mobile explore feed data.
**Auth**: Bearer JWT
**Params**: Mobile explore filters and paging parameters.
**Response**: Mobile-optimized explore payload.
**Notes**: Response shape may differ from desktop explore for compact rendering.

### `GET /api/unified/search/omnisearch`
**Purpose**: Unified search across songs, users, and related content.
**Auth**: Bearer JWT
**Params**: Search query and optional scope/filter parameters.
**Response**: Multi-section search results payload.
**Notes**: Omnisearch is typically broader than the dedicated search endpoints.

## Clips

### `GET /api/clips/adjust-speed/`
**Purpose**: Adjust playback speed for a clip.
**Auth**: Bearer JWT
**Params**: Clip identifier and speed-related query fields.
**Response**: Updated clip/speed result or adjustment acknowledgment.
**Notes**: Client-side playback manipulation surface; may return derived media metadata.

### `GET /api/clips/aligned_clip_siblings`
**Purpose**: Get clips aligned as siblings.
**Auth**: Bearer JWT
**Params**: `clip_id` or equivalent clip reference.
**Response**: Array or object of aligned sibling clips.
**Notes**: Useful for remix/variation grouping.

### `GET /api/clips/aligned_clips`
**Purpose**: Get aligned clips.
**Auth**: Bearer JWT
**Params**: Clip reference plus optional alignment filters.
**Response**: Aligned clip collection.
**Notes**: Often used by the client to build related-track views.

### `GET /api/clips/autoplay/`
**Purpose**: Retrieve or reflect autoplay state.
**Auth**: Bearer JWT
**Params**: Optional clip or user context parameters.
**Response**: Autoplay setting or state payload.
**Notes**: Client preference/state endpoint.

### `GET /api/clips/delete/`
**Purpose**: Delete a clip.
**Auth**: Bearer JWT
**Params**: Clip identifier.
**Response**: Deletion acknowledgment.
**Notes**: Destructive operation; often maps to trash/delete semantics in the UI.

### `GET /api/clips/direct_children`
**Purpose**: List direct child clips.
**Auth**: Bearer JWT
**Params**: Parent clip identifier.
**Response**: Child clip array.
**Notes**: Used for continuation trees and derivation chains.

### `GET /api/clips/direct_children_by_user/`
**Purpose**: List direct child clips for a given user.
**Auth**: Bearer JWT
**Params**: User identifier and parent clip reference.
**Response**: Child clip array grouped by user.
**Notes**: User-scoped variant of `direct_children`.

### `GET /api/clips/direct_children_count`
**Purpose**: Count direct children for a clip.
**Auth**: Bearer JWT
**Params**: Parent clip identifier.
**Response**: Count payload.
**Notes**: Lightweight count endpoint for UI badges.

### `GET /api/clips/displayable_remixes`
**Purpose**: List remixes that should be shown to the user.
**Auth**: Bearer JWT
**Params**: Clip identifier and visibility filters.
**Response**: Remix list payload.
**Notes**: Filters out hidden/non-displayable remix records.

### `GET /api/clips/displayable_remixes_by_user/`
**Purpose**: List displayable remixes scoped to a user.
**Auth**: Bearer JWT
**Params**: User identifier plus clip reference.
**Response**: Remix list payload.
**Notes**: User-scoped displayable remix endpoint.

### `GET /api/clips/displayable_remixes_count`
**Purpose**: Count displayable remixes.
**Auth**: Bearer JWT
**Params**: Clip identifier.
**Response**: Count payload.
**Notes**: Used for summary counts in feed cards.

### `GET /api/clips/displayable_user_remixes_for_clip/`
**Purpose**: Get displayable user remixes for a clip.
**Auth**: Bearer JWT
**Params**: Clip identifier and user scope.
**Response**: Remix array.
**Notes**: Variant that constrains the result set to user-visible entries.

### `GET /api/clips/get_similar/`
**Purpose**: Fetch similar clips.
**Auth**: Bearer JWT
**Params**: Clip identifier and similarity filters.
**Response**: Similar clip list.
**Notes**: Used for discovery and recommendation rails.

### `GET /api/clips/get_songs_by_ids`
**Purpose**: Bulk fetch songs by ID.
**Auth**: Bearer JWT
**Params**: `ids` (comma-separated IDs or repeated parameters).
**Response**: Clip/song array keyed by requested IDs.
**Notes**: Core bulk lookup endpoint for library hydration.

### `GET /api/clips/parent`
**Purpose**: Get a clip’s parent.
**Auth**: Bearer JWT
**Params**: Clip identifier.
**Response**: Parent clip object or null-equivalent payload.
**Notes**: Resolves derivation ancestry for remix trees.

### `GET /api/clips/user_remixes_for_clip/`
**Purpose**: Get user remixes for a clip.
**Auth**: Bearer JWT
**Params**: Clip identifier plus user scope.
**Response**: Remix array.
**Notes**: Broader remix surface than `displayable_*` variants.

### `GET /api/clip/{clip_id}`
**Purpose**: Fetch a single clip’s details.
**Auth**: Bearer JWT
**Params**: `clip_id` path parameter.
**Response**: Clip object with metadata, status, and media URLs.
**Notes**: Primary detail endpoint for clip cards and deep links.

### `GET /api/clip/{clip_id}/stems`
**Purpose**: Get stem metadata for a clip.
**Auth**: Bearer JWT
**Params**: `clip_id` path parameter.
**Response**: Stem list/object for the clip.
**Notes**: Returns available stem separation data when the clip supports it.

### `GET /api/clip/{clip_id}/stems/pages`
**Purpose**: Get paged stem data for a clip.
**Auth**: Bearer JWT
**Params**: `clip_id` path parameter and optional paging parameters.
**Response**: Stem page payload.
**Notes**: Used when stem data is split across pages or segments.

### `POST /api/edit/crop/{clip_id}/`
**Purpose**: Crop a clip.
**Auth**: Bearer JWT
**Params**: `clip_id` path parameter; crop payload with timing/range fields.
**Response**: Crop job/result payload.
**Notes**: Mutates clip playback range or generates a cropped derivative.

### `POST /api/edit/stems/{clip_id}/`
**Purpose**: Edit stems for a clip.
**Auth**: Bearer JWT
**Params**: `clip_id` path parameter; stem edit payload.
**Response**: Stem edit job/result payload.
**Notes**: Advanced post-processing endpoint for stem manipulation.

## Generation Status

### `GET /api/gen/{id}`
**Purpose**: Get generation status.
**Auth**: Bearer JWT
**Params**: `id` path parameter.
**Response**: Generation object with status and media fields when complete.
**Notes**: Canonical polling endpoint for async generation jobs.

### `GET /api/gen/{id}/aligned_lyrics/v2/`
**Purpose**: Get word-timed lyrics for a generation.
**Auth**: Bearer JWT
**Params**: `id` path parameter.
**Response**: Aligned lyrics payload with word timing metadata.
**Notes**: Use for karaoke-style lyric rendering.

### `POST /api/gen/bulk_increment_play_counts/v2`
**Purpose**: Increment play counts in bulk.
**Auth**: Bearer JWT
**Params**: Clip/generation IDs and count payload.
**Response**: Success acknowledgment.
**Notes**: Analytics/engagement accounting endpoint.

### `POST /api/gen/increment_action_counts/`
**Purpose**: Increment action counters.
**Auth**: Bearer JWT
**Params**: Action count payload keyed by clip/generation/action type.
**Response**: Success acknowledgment.
**Notes**: Tracks user interactions such as plays, likes, shares, or similar events.

### `POST /api/gen/prompt_image/`
**Purpose**: Generate an image from a prompt.
**Auth**: Bearer JWT
**Params**: Prompt/image-generation payload.
**Response**: Prompt image job or image URL payload.
**Notes**: Async image generation helper used by some creation flows.

### `POST /api/gen/trash`
**Purpose**: Trash generations.
**Auth**: Bearer JWT
**Params**: Generation or clip IDs to trash.
**Response**: Trash acknowledgment.
**Notes**: Moves generated items to trash state rather than hard delete in many flows.

## Playlists

### `GET /api/playlist/me`
**Purpose**: List the authenticated user’s playlists.
**Auth**: Bearer JWT
**Params**: Optional paging/filter parameters.
**Response**: Playlist array plus paging metadata.
**Notes**: Primary playlist inventory endpoint.

### `POST /api/playlist/create/`
**Purpose**: Create a playlist.
**Auth**: Bearer JWT
**Params**: Playlist metadata and initial clip list.
**Response**: Created playlist object.
**Notes**: Returns the new playlist identifier and metadata.

### `POST /api/playlist/set_metadata`
**Purpose**: Update playlist metadata.
**Auth**: Bearer JWT
**Params**: Playlist ID plus metadata fields such as title, description, and visibility.
**Response**: Updated playlist object or success acknowledgment.
**Notes**: Use after creation to rename or reconfigure a playlist.

### `POST /api/playlist/trash/`
**Purpose**: Trash a playlist.
**Auth**: Bearer JWT
**Params**: Playlist ID.
**Response**: Trash acknowledgment.
**Notes**: Destructive/soft-delete style action depending on backend retention policy.

### `POST /api/playlist/update_clips/`
**Purpose**: Update clips in a playlist.
**Auth**: Bearer JWT
**Params**: Playlist ID and clip ordering/add-remove payload.
**Response**: Updated playlist object or success acknowledgment.
**Notes**: Used for reordering and membership changes.

### `GET /api/playlist/{playlist_id}/`
**Purpose**: Fetch a public playlist.
**Auth**: Bearer JWT
**Params**: `playlist_id` path parameter.
**Response**: Playlist object with metadata and visibility state.
**Notes**: Public API path; may still require auth for full detail.

### `GET /api/playlist/{playlist_id}/tracks`
**Purpose**: Fetch tracks for a playlist.
**Auth**: Bearer JWT
**Params**: `playlist_id` path parameter and optional paging parameters.
**Response**: Track list payload.
**Notes**: May be absent or gated depending on playlist type or backend rollout.

## Search

### `GET /api/search/`
**Purpose**: Search songs, users, and related entities.
**Auth**: Bearer JWT
**Params**: Query string plus optional type/sort/filter parameters.
**Response**: Search result sections or unified result list.
**Notes**: General-purpose search surface; scope depends on query and backend configuration.

### `GET /api/search/history`
**Purpose**: Fetch search history.
**Auth**: Bearer JWT
**Params**: Optional paging or history-scope parameters.
**Response**: Recent search history entries.
**Notes**: Used to populate autocomplete/history UI.

### `GET /api/search/users`
**Purpose**: Search users.
**Auth**: Bearer JWT
**Params**: Query string and optional user filters.
**Response**: User result list.
**Notes**: Dedicated user-only search endpoint.

### `GET /api/discover/shortcuts_songs`
**Purpose**: Retrieve shortcut/discover songs.
**Auth**: Bearer JWT
**Params**: Optional discover filters.
**Response**: Song suggestion list.
**Notes**: Discovery surface for curated or shortcut recommendations.

## Trending / Meta

### `GET /api/trending/metaplaylist/`
**Purpose**: Fetch trending metaplaylist data.
**Auth**: Bearer JWT
**Params**: Optional trending scope or paging parameters.
**Response**: Trending playlist/collection payload.
**Notes**: Used for top-level discovery and trend rails.

### `GET /api/tags/recommend`
**Purpose**: Return recommended tags.
**Auth**: Bearer JWT
**Params**: Prompt/context parameters when supported.
**Response**: Tag recommendation list.
**Notes**: Useful for prompt enrichment and library classification.
