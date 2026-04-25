# Suno API Endpoint Inventory

Complete catalog of all known Suno API endpoints discovered from reverse-engineering.

## Auth & Session (Clerk)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/v1/client` | Clerk cookie | Get Clerk client session |
| POST | `/v1/client/sessions/{sid}/tokens` | Clerk cookie | Exchange session for JWT |
| POST | `/v1/client/sessions/{sid}/tokens/api` | Clerk cookie | Token exchange variant |
| POST | `/v1/client/sessions/{sid}/touch` | Clerk cookie | Keep session alive |
| POST | `/v1/client/verify` | Clerk cookie | Clerk verification |
| GET | `/v1/client/sync` | Clerk cookie | Sync client state |
| GET/POST | `/v1/event` | Clerk cookie | Event tracking |
| GET | `/v1/logs` | Clerk cookie | Client logs |
| POST | `/v1/tickets/accept` | Clerk cookie | Accept tickets |
| GET | `/v1/verify` | Clerk cookie | Verify client |
| POST | `/v1/client?_method=PATCH` | Clerk cookie | Turnstile/captcha simulation |

Base URL: `https://auth.suno.com/v1`

## Suno API (JWT Bearer Token)

### Billing & Credits

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/billing/info/` | Credits, plan info |
| GET | `/api/billing/usage-plan-descriptions/` | Plan descriptions |
| GET | `/api/billing/usage-plan-faq/` | Plan FAQ |
| GET | `/api/billing/usage-plan-web-table-comparison/` | Plan comparison table |
| GET | `/api/billing/usage-plans` | Available plans |
| POST | `/api/billing/create-session/` | Create checkout session |
| POST | `/api/billing/create-portal/` | Customer portal |
| POST | `/api/billing/change-plan/` | Change subscription |
| POST | `/api/billing/cancel-sub/` | Cancel subscription |
| POST | `/api/billing/unpause-sub/` | Unpause subscription |
| POST | `/api/billing/pause-sub/` | Pause subscription |
| POST | `/api/billing/cancel-sub/undo/` | Undo cancellation |
| GET | `/api/billing/default-currency` | Default currency |
| GET | `/api/billing/eligible-discounts` | Available discounts |
| GET | `/api/billing/get-discount-offer` | Discount offer |
| GET | `/api/billing/get-churn-survey-options` | Churn survey |
| POST | `/api/billing/submit-survey/` | Submit survey |
| GET | `/api/billing/tax-info` | Tax information |
| POST | `/api/billing/accept-sub-coupon/` | Accept coupon |
| POST | `/api/billing/set-default-payment-method/` | Set payment method |
| GET | `/api/billing/clips/{clip_id}/download/` | Download clip |
| GET | `/api/billing/purchase-info/{purchase_id}/` | Purchase info |
| POST | `/api/billing/conversion-tracking` | Conversion tracking |
| GET | `/api/billing/change-plan/preview/` | Plan change preview |

Base URL: `https://studio-api-prod.suno.com`

### User & Session

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/user/me` | Current user info |
| GET | `/api/user/get_user_session_id/` | Session ID |
| GET | `/api/user/user_config/` | User configuration |
| POST | `/api/user/update_user_config/` | Update config |
| GET | `/api/user/tos_acceptance` | TOS status |
| POST | `/api/user/reset_onboarding/` | Reset onboarding |
| POST | `/api/user/accept_timbaland_terms/` | Accept terms |
| DELETE | `/api/user/delete-account/` | Delete account |
| GET | `/api/session/` | Session info |
| GET | `/api/auth/verify-token` | Verify JWT token |
| GET | `/api/me/` | User profile (v1) |
| GET | `/api/me/v2` | User profile (v2) |
| GET | `/api/me/v2/cover-art` | User cover art |
| GET | `/api/me/v2/history` | User history |
| GET | `/api/me/v2/hooks` | User hooks |
| GET | `/api/me/v2/personas` | User personas |
| GET | `/api/me/v2/playlists` | User playlists |
| GET | `/api/me/v2/studio-projects` | Studio projects |
| GET | `/api/me/v2/trash` | Trash items |
| GET | `/api/me/v2/workspaces` | Workspaces |
| GET | `/api/me/cover-art` | Cover art (v1) |
| GET | `/api/me/external_accounts` | External accounts |
| GET | `/api/me/followers` | Followers |
| GET | `/api/me/following` | Following |
| GET | `/api/me/history` | History (v1) |
| GET | `/api/me/hooks` | Hooks (v1) |
| GET | `/api/me/liked-hooks` | Liked hooks |
| GET | `/api/me/liked-playlists` | Liked playlists |
| GET | `/api/me/lyrics` | User lyrics |
| GET | `/api/me/organization_invitations` | Org invites |
| GET | `/api/me/organization_memberships` | Org memberships |
| GET | `/api/me/organization_suggestions` | Org suggestions |
| GET | `/api/me/passkeys` | Passkeys |
| GET | `/api/me/personas` | Personas (v1) |
| GET | `/api/me/playlists` | Playlists (v1) |
| GET | `/api/me/sessions` | Sessions |
| GET | `/api/me/sessions/active` | Active sessions |
| GET | `/api/me/styles` | User styles |
| GET | `/api/me/totp` | TOTP settings |
| GET | `/api/me/totp/attempt_verification` | TOTP verify |
| GET | `/api/me/trash` | Trash (v1) |
| GET | `/api/me/workspaces` | Workspaces (v1) |
| GET | `/api/me/studio-projects` | Studio projects (v1) |

### Projects & Studio

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/project/me` | My projects |
| GET | `/api/project` | List projects |
| POST | `/api/project` | Create project |
| GET | `/api/project/trash` | Trash |
| GET | `/api/project/invites` | Invites |
| GET | `/api/project/{project_id}` | Get project |
| GET | `/api/project/{project_id}/clips` | Project clips |
| GET | `/api/project/{project_id}/metadata` | Project metadata |
| GET | `/api/project/{project_id}/pinned-clips` | Pinned clips |
| GET | `/api/project/{project_id}/collaborators` | Collaborators |
| GET | `/api/project/{project_id}/collaborators/me` | My collaborator info |
| GET | `/api/project/{project_id}/ably-token` | Ably token |
| GET | `/api/project/{project_id}/ably-client-id` | Ably client ID |
| POST | `/api/project/{project_id}/invite` | Invite collaborator |
| POST | `/api/project/{project_id}/ably-update` | Update Ably |
| GET | `/api/project/:projectId` | Project by ID (alt) |
| GET | `/api/studio/` | Studio access |
| GET | `/api/studio/:slug` | Studio by slug |

### Generation

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/generate/v2/` | Main generation (v2) |
| POST | `/api/generate/v2-web/` | Web generation variant |
| POST | `/api/generate/lyrics/` | Generate lyrics |
| GET | `/api/generate/lyrics/{id}` | Poll lyrics job |
| POST | `/api/generate/lyrics-infill` | Lyrics infill |
| POST | `/api/generate/lyrics-mashup` | Lyrics mashup |
| POST | `/api/generate/lyrics-pair` | Lyrics pair |
| POST | `/api/generate/lyrics-pair/rate` | Rate lyrics pair |
| POST | `/api/generate/concat/v2/` | Merge/concat clips |
| POST | `/api/generate/merge/` | Merge clips |
| POST | `/api/generate/upsample` | Upsample audio |
| POST | `/api/generate/get_recommend_styles` | Recommend styles |
| POST | `/api/generate/matrix` | Generation matrix |
| POST | `/api/generate/sum/` | Sum generation |
| GET | `/api/gen/{id}` | Get generation status |
| GET | `/api/gen/{id}/aligned_lyrics/v2/` | Aligned lyrics |
| POST | `/api/gen/{id}/convert_wav/` | Convert to WAV |
| POST | `/api/gen/bulk_increment_play_counts/v2` | Increment play counts |
| POST | `/api/gen/increment_action_counts/` | Increment action counts |
| POST | `/api/gen/prompt_image/` | Set prompt image |
| POST | `/api/gen/trash` | Trash generation |
| POST | `/api/gen/set_metadata/` | Set metadata |
| GET | `/api/clip/{clip_id}` | Get clip |
| GET | `/api/clips/adjust-speed/` | Adjust playback speed |
| GET | `/api/clips/aligned_clips` | Aligned clips |
| GET | `/api/clips/aligned_clip_siblings` | Aligned clip siblings |
| GET | `/api/clips/autoplay/` | Autoplay clips |
| GET | `/api/clips/delete/` | Delete clip |
| GET | `/api/clips/direct_children` | Direct children |
| GET | `/api/clips/direct_children_by_user/` | Children by user |
| GET | `/api/clips/direct_children_count` | Children count |
| GET | `/api/clips/displayable_remixes` | Displayable remixes |
| GET | `/api/clips/displayable_remixes_by_user/` | Remixes by user |
| GET | `/api/clips/displayable_remixes_count` | Remix count |
| GET | `/api/clips/displayable_user_remixes_for_clip/` | User remixes |
| GET | `/api/clips/get_similar/` | Similar clips |
| GET | `/api/clips/get_songs_by_ids` | Get songs by IDs |
| GET | `/api/clips/parent` | Parent clip |
| GET | `/api/clips/user_remixes_for_clip/` | User remixes for clip |

### Library & Feed

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/feed/` | Library feed (v1) |
| GET | `/api/feed/v2` | Feed (v2) |
| POST | `/api/feed/v3` | Feed (v3, cursor-based) |
| GET | `/api/feed/v3/offset` | Feed offset-based |
| GET | `/api/unified/feed` | Unified feed |
| GET | `/api/unified/homepage` | Homepage feed |
| GET | `/api/unified/homepage/explore` | Explore feed |
| GET | `/api/search/` | Search |
| GET | `/api/search/users` | Search users |
| GET | `/api/playlist/me` | My playlists |
| POST | `/api/playlist/create/` | Create playlist |
| POST | `/api/playlist/set_metadata` | Set playlist metadata |
| POST | `/api/playlist/trash/` | Trash playlist |
| POST | `/api/playlist/update_clips/` | Update playlist clips |
| GET | `/api/playlist/{playlist_id}/` | Get playlist |
| GET | `/api/playlist/{playlist_id}/tracks` | Playlist tracks |
| GET | `/api/trending/metaplaylist/` | Trending metaplaylist |
| GET | `/api/tags/recommend` | Recommended tags |
| POST | `/api/recommend/hide-creator` | Hide creator |

### Persona & Voice

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/persona/create/` | Create persona |
| GET | `/api/persona/get-persona-paginated/{id}/` | Get personas (paginated) |
| GET | `/api/persona/get-followed-personas/` | Followed personas |
| GET | `/api/persona/get-loved-personas/` | Loved personas |
| GET | `/api/persona/get-personas/` | Get personas |
| POST | `/api/custom-model/create/` | Create custom model |
| POST | `/api/custom-model/archive/` | Archive custom model |
| GET | `/api/custom-model/pending/` | Pending custom models |
| POST | `/api/processed_clip/voice-vox-stem` | Voice/stem processing |
| GET | `/api/me/personas` | User personas |
| GET | `/api/me/v2/personas` | User personas (v2) |

### Upload & Processing

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/uploads/audio/{id}/initialize-clip/` | Initialize audio clip |
| POST | `/api/uploads/audio/{id}/upload-finish/` | Mark upload complete |
| GET | `/api/uploads/audio/{id}/` | Poll upload status |
| GET | `/api/uploads/video/` | Video uploads |
| POST | `/api/uploads/video/{upload_id}/upload-finish/` | Video upload finish |
| GET | `/api/clip/{clip_id}/stems` | Get clip stems |
| GET | `/api/clip/{clip_id}/stems/pages` | Stems pages |
| POST | `/api/edit/crop/{clip_id}/` | Crop clip |
| POST | `/api/edit/stems/{clip_id}/` | Edit stems |
| GET | `/api/openai-speech/` | OpenAI speech |
| GET | `/api/deepgram-token` | Deepgram token |
| GET | `/api/video/generate/{clip_id}/status/` | Video generation status |
| POST | `/api/video/hooks/create` | Create video hook |
| GET | `/api/video/hooks/feed` | Video hooks feed |
| GET | `/api/video/hooks/{hook_id}/flag` | Flag video hook |

### Social & Profiles

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/profiles/` | List profiles |
| GET | `/api/profiles/{handle}` | Get profile |
| GET | `/api/profiles/{handle}/info` | Profile info |
| GET | `/api/profiles/follow` | Follow profile |
| GET | `/api/profiles/pinned-clips` | Pinned clips |
| GET | `/api/profiles/mutual-followers` | Mutual followers |
| GET | `/api/comment/{comment_id}` | Get comment |
| POST | `/api/comment/block-user` | Block user |
| POST | `/api/comment/unblock-user` | Unblock user |
| POST | `/api/comment/{comment_id}/reaction` | React to comment |
| POST | `/api/comment/{comment_id}/replies` | Reply to comment |
| POST | `/api/comment/{comment_id}/report` | Report comment |
| GET | `/api/notification/v2` | Notifications (v2) |
| GET | `/api/notification/v2/badge-count` | Badge count |
| GET | `/api/notification/v2/clear-badge` | Clear badge |
| GET | `/api/notification/v2/read` | Mark as read |
| POST | `/api/song_copy/send-song` | Send song |
| GET | `/api/invite/` | Get invites |

### Misc & Feature Gates

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/modals` | Get modals |
| POST | `/api/statsig/experiment/` | Statsig experiment |
| GET | `/api/statsig/experiment/forked-onboarding` | Forked onboarding |
| GET | `/api/mango/rights` | Mango rights |
| POST | `/api/moderation/ack-copyright-warning` | Ack copyright |
| GET | `/api/music_player/playbar_state` | Playbar state |
| GET | `/api/discover/shortcuts_songs` | Shortcut songs |
| GET | `/api/cms` | CMS content |
| GET | `/api/ably/simple-mode-token` | Ably simple token |
| POST | `/api/preferences/clip-review/opt-out` | Opt out clip review |
| GET | `/api/preferences/clip-review/pending` | Pending clip review |
| POST | `/api/preferences/clip-review/submit` | Submit clip review |
| GET | `/api/personalization/memory` | Personalization memory |
| GET | `/api/personalization/settings` | Personalization settings |

### B-Side & Labs (Hidden/Experimental)

See `b-side.md` for full documentation of 50+ B-Side routes and Labs features.

## Orpheus Chat API

Base URL: `https://suno-ai--orpheus-prod-web.modal.run`

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/chat/completions` | Chat completions (OpenAI-compatible) |
| GET | `/api/v1/models` | List available models |

Models: `orpheus-0.1`, `orpheus-0.2`, `orpheus-0.3`, `orpheus-0.4`, `orpheus-0.5`

## Known Response Codes

- `200`: Success
- `401`: Unauthorized (invalid/expired JWT)
- `403`: Forbidden (insufficient permissions)
- `404`: Not found (endpoint not deployed)
- `405`: Method not allowed
- `429`: Rate limited
- `500`: Internal server error

## Authentication Flow

1. Extract `__session` cookie from browser
2. GET `https://auth.suno.com/v1/client?_is_native=true` with cookie
3. Parse `last_active_session_id` or `sessions[0].id`
4. POST `https://auth.suno.com/v1/client/sessions/{sid}/tokens`
5. Extract `jwt` from response
6. Use `Authorization: Bearer {jwt}` for API calls

JWT Claims:
- `user_id`: Suno user ID
- `clerk_id`: Clerk user ID
- `token_type`: "access"
- `aud`: "suno-api"
- `exp`: Expiration timestamp
- `fva`: [0, -1] (feature vector array)

## Notes

- All endpoints require valid JWT except Clerk auth endpoints
- JWT expires after ~1 hour
- Some endpoints require specific feature flags (Statsig)
- B-Side/Labs endpoints may return 404 if feature not enabled
- Rate limiting applies to generation endpoints
- Credits are deducted per generation based on plan

Generated from reference repo scan and web research.
