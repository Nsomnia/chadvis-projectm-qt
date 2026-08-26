# Suno API Recon Archive

Consolidated archive of early reconnaissance notes that previously lived as
standalone files at the repo root of `docs/`:

- `SUNO_API_NOTES.md` (2026-04-21)
- `SUNO_B_SIDE_DISCOVERY.md` (2026-04-21)
- `API_SCRATCH_SUB_AGENT_FINDINGS.md` (undated scratch dump)

These notes are **superseded** by the curated topic documents in this directory
(`auth.md`, `generation.md`, `library.md`, etc.) and `ENDPOINT-INVENTORY.md`.
They are preserved here because they contain raw observations and probe results
not captured elsewhere. Content has been merged with minimal editing; treat
claims as point-in-time observations, not guarantees.

---

## 1. Reconnaissance Notes (from `SUNO_API_NOTES.md`, 2026-04-21)

Based on internal reconnaissance of network traffic and endpoint scans.

### Base URLs
- **Primary API**: `https://studio-api-prod.suno.com`
- **Frontend/Clerk**: `https://api.suno.ai` (Auth handled via Clerk)

### Key Endpoints
- `POST /api/feed/v3`: Likely the main library/feed retrieval endpoint. Supports pagination.
- `GET /api/clips/get_songs_by_ids`: Bulk retrieval of track metadata by ID.
- `POST /api/generate/v2-web/`: Song generation endpoint.
- `GET /api/billing/info/`: Subscription and credit balance.
- `GET /api/personalization/settings`: User preferences and experimental feature flags.

### Auth Strategy
- Authorization headers typically use a Bearer token obtained from Clerk session.
- Cookies `__client_uat` and session tokens are critical for persistent auth.

---

## 2. B-Side & Beta Feature Discovery (from `SUNO_B_SIDE_DISCOVERY.md`, 2026-04-21)

Analysis of experimental flags and hidden endpoints for "Chad" integration.

### Experimental Models & Versions
- **v4**: Improving sound quality, currently in rollout.
- **v4.5 (Chirp-Auk)**: Intelligent prompts, pro-only feature.
- **v4.5-all**: High-quality free model.
- **v5 / v5.5**: Advanced models listed in subscription comparisons.

### Feature Gates & Capabilities
- `create_control_sliders`: Direct manipulation of generation parameters (weirdness, style weight).
- `tag_upsample`: Intelligent tag expansion.
- `persona`: Voice/style consistency features.
- `infill`: Part-specific generation (intro, outro, mid-song).
- `studio`: Advanced multi-track editing workspace.
- `auk`: Native intelligent prompt engine.

### Orchestrator & Conversational Generation
- Endpoints under `/api/statsig/experiment/` and `/api/cms/nudges/` suggest a behavioral steering engine.
- Conversational generation (chat-based) likely utilizes a stateful session on `studio-api-prod.suno.com`.

### Integration Strategy for ChadVis
- **Client-Side Overrides**: Inject `statsig` overrides to force-enable pro/beta UI features.
- **Direct Parameter Access**: Expose `weirdness_constraint` and `style_weight` in the generation UI.
- **Model Forcing**: Allow selecting `chirp-auk` or `chirp-v4` directly via API headers.

---

## 3. Scratch Sub-Agent Findings (from `API_SCRATCH_SUB_AGENT_FINDINGS.md`)

Raw endpoint/auth dump from an automated recon pass.

### Scope Notes
- `burp/` and `NOTE_768-feature-gates-wtf/` were not present at the exact paths.
- `suno-api-and-client-to-database_inital-prompt-template-for-llm-models` and `..._qwen3.6-high` exist as sibling repos/directories; the relevant endpoint intel is in the shared scan docs above.

### Base URLs / Services Found
- https://auth.suno.com/v1
- https://studio-api-prod.suno.com
- https://studio-api.prod.suno.com (alternate spelling in inventory)
- https://suno.com
- https://suno.ai
- https://s.prod.suno.com
- https://statusz.suno.ai
- https://cdn-o.suno.com
- https://goto.suno.com
- https://hcaptcha-endpoint-prod.suno.com
- https://clerk.suno.com
- https://suno-ai--orpheus-prod-web.modal.run

### Auth / Cookies / Tokens

Cookies seen:
- `__session`
- `__client`
- `__client_uat`
- `__client_uat_Jnxw-muT`
- `__client_uat_Jnxw-muTs`
- `__session_Jnxw-muT`
- `__session_Jnxw-muTs`

Headers / auth:
- `Authorization: Bearer <JWT>`
- `Origin: https://suno.com`
- `Device-Id: <uuid>`
- `Browser-Token: <base64 timestamp JSON>`

JWT claims mentioned:
- `user_id`
- `clerk_id`
- `token_type=access`
- `aud=suno-api`
- `sid`
- `email`
- `exp`

Clerk / auth flow:
- GET `https://auth.suno.com/v1/client?_is_native=true&_clerk_js_version={ver}`
- POST `https://auth.suno.com/v1/client/sessions/{sid}/tokens`
- GET `https://auth.suno.com/v1/client/sync`
- GET/POST `https://auth.suno.com/v1/event`
- GET `https://auth.suno.com/v1/logs`
- POST `https://auth.suno.com/v1/tickets/accept`
- GET `https://auth.suno.com/v1/verify`

Public auth-linked routes:
- `/sign-in`, `/sign-up`, `/auth`, `/auth/verify`, `/auth/error`, `/auth/birthday`
- `/client`, `/client/sessions`, `/client/sign_ins`, `/client/sign_ups`, `/client/touch`
- `/sessions`, `/me/sessions`, `/me/sessions/active`

### Main API Endpoints Found

Billing / credits:
- GET `/api/billing/info/`
- GET `/api/billing/usage-plan-descriptions/`
- GET `/api/billing/usage-plan-faq/`
- GET `/api/billing/usage-plan-web-table-comparison/`
- GET `/api/billing/usage-plans`
- POST `/api/billing/create-session/`
- POST `/api/billing/change-plan/`
- POST `/api/billing/cancel-sub/`
- POST `/api/billing/unpause-sub/`
- POST `/api/billing/pause-sub/`
- GET `/api/billing/default-currency`
- GET `/api/billing/eligible-discounts`
- GET `/api/billing/get-discount-offer`
- GET `/api/billing/get-churn-survey-options`
- POST `/api/billing/submit-survey/`
- GET `/api/billing/tax-info`
- POST `/api/billing/accept-sub-coupon/`
- POST `/api/billing/set-default-payment-method/`
- GET `/api/billing/clips/{clip_id}/download/`
- GET `/api/billing/purchase-info/{purchase_id}/`

User / session / config:
- GET `/api/user/me`
- GET `/api/user/get_user_session_id/`
- GET `/api/user/user_config/`
- POST `/api/user/update_user_config/`
- GET `/api/user/tos_acceptance`
- POST `/api/user/reset_onboarding/`
- POST `/api/user/accept_timbaland_terms/`
- DELETE `/api/user/delete-account/`
- GET `/api/session/`
- GET `/api/auth/verify-token`

Projects / workspace:
- GET `/api/project/me`
- GET `/api/project`
- POST `/api/project`
- GET `/api/project/trash`
- GET `/api/project/invites`
- GET `/api/project/{project_id}`
- GET `/api/project/{project_id}/clips`
- GET `/api/project/{project_id}/metadata`
- GET `/api/project/{project_id}/pinned-clips`
- GET `/api/project/{project_id}/collaborators`
- GET `/api/project/{project_id}/collaborators/me`
- GET `/api/project/{project_id}/ably-token`
- GET `/api/project/{project_id}/ably-client-id`
- POST `/api/project/{project_id}/invite`
- POST `/api/project/{project_id}/ably-update`

Generation / clips / media:
- POST `/api/generate/v2-web/`
- GET/POST `/api/gen/{clip_id}/*` family
- GET `/api/gen/{gen_id}/*` family
- GET `/api/clips/*` family
- GET `/api/clip/{clip_id}`
- GET `/api/clip/{clip_id}/stems`
- GET `/api/clip/{clip_id}/stems/pages`
- POST `/api/edit/crop/{clip_id}/`
- POST `/api/edit/stems/{clip_id}/`
- GET `/api/openai-speech/`
- GET `/api/deepgram-token`
- GET `/api/uploads/video/`
- POST `/api/uploads/video/{upload_id}/upload-finish/`
- GET `/api/video/generate/{clip_id}/status/`
- POST `/api/video/hooks/create`
- GET `/api/video/hooks/feed`
- GET `/api/video/hooks/{hook_id}/flag`

Social / feed / profiles / comments:
- GET `/api/feed/v3`
- GET `/api/feed/v3/offset`
- GET `/api/unified/feed`
- GET `/api/unified/homepage`
- GET `/api/unified/homepage/explore`
- GET `/api/search/`
- GET `/api/search/users`
- GET `/api/profiles/`
- GET `/api/profiles/{handle}`
- GET `/api/profiles/{handle}/info`
- GET `/api/profiles/follow`
- GET `/api/profiles/pinned-clips`
- GET `/api/comment/{comment_id}`
- POST `/api/comment/block-user`
- POST `/api/comment/unblock-user`
- POST `/api/comment/{comment_id}/reaction`
- POST `/api/comment/{comment_id}/replies`
- POST `/api/comment/{comment_id}/report`

Misc:
- GET `/api/modals`
- POST `/api/statsig/experiment/`
- GET `/api/statsig/experiment/forked-onboarding`
- GET `/api/notification/v2`
- GET `/api/notification/v2/badge-count`
- GET `/api/notification/v2/clear-badge`
- GET `/api/notification/v2/read`
- GET `/api/mango/rights`
- POST `/api/moderation/ack-copyright-warning`
- GET `/api/song_copy/send-song`
- POST `/api/recommend/hide-creator`
- GET `/api/tags/recommend`
- GET `/api/trending/metaplaylist/`

### Hidden / Beta / B-Side / Experimental

`/b-side/*` routes:
- `/b-side`
- `/b-side/account-moderation`
- `/b-side/agentic-transcript`
- `/b-side/agentic-transcript/:clipId`
- `/b-side/api-explorer`
- `/b-side/audible-magic`
- `/b-side/banner`
- `/b-side/because-you-like`
- `/b-side/billing/revcat`
- `/b-side/contests`
- `/b-side/cover-art-eval`
- `/b-side/creators`
- `/b-side/describe-clip`
- `/b-side/dsp-diag`
- `/b-side/dsp-engine-talk`
- `/b-side/explore`
- `/b-side/feature-flags`
- `/b-side/hipster`
- `/b-side/hook-song-gen`
- `/b-side/hooks-explorer/:slug*?`
- `/b-side/impersonate`
- `/b-side/istthisus` *(transcribed as `/b-side/isthisus` in source)*
- `/b-side/labs-control`
- `/b-side/lyrics-eval`
- `/b-side/lyrics-eval-reports/:slug*?`
- `/b-side/lyrics-eval/:slug*?`
- `/b-side/lyrics-gen`
- `/b-side/lyrics-viewer`
- `/b-side/milo`
- `/b-side/music-soulmate`
- `/b-side/music-soulmate-talk`
- `/b-side/nux`
- `/b-side/on-repeat`
- `/b-side/onboarding-survey`
- `/b-side/orpheus`
- `/b-side/personalization`
- `/b-side/playlist-copier`
- `/b-side/project-state-tour`
- `/b-side/search-lens/*`
- `/b-side/simple-remix/:slug*?`
- `/b-side/song-moderation`
- `/b-side/sse-demo`
- `/b-side/studio-access`
- `/b-side/trending-moderation`
- `/b-side/user-activity`
- `/b-side/user-mix`
- `/b-side/user-similarity`
- `/b-side/video-gen`
- `/b-side/vip`
- `/b-side/visual-art`
- `/b-side/visual-art/:type/:id`
- `/b-side/voice-verification`

`/labs/*` routes:
- `/labs/canvas`
- `/labs/divisi`
- `/labs/genre-wheel`
- `/labs/listen-and-rank`
- `/labs/live-radio`
- `/labs/marketplace`
- `/labs/milo`
- `/labs/pedalboard`
- `/labs/suno-mania`
- `/labs/turntable`
- `/labs/turntable/:roomId`
- `/labs/verse`

Statsig / feature flags observed:
- `orpheus_is_enabled`
- `orpheus_is_auto_mode`
- `orpheus_is_canvas_enabled`
- `orpheus_default_to_chat`
- `orpheus_mobile_web_enabled`
- `orpheus_group`
- `marketplace_enabled`
- `marketplace_access`
- `labs_marketplace`
- `gen-video-covers`
- `statsig.cached.evaluations.*`
- `statsig::gate_exposure`
- `statsig::config_exposure`
- `statsig::layer_exposure`
- `statsig::non_exposed_checks`

### Credit Abuse / Exploit / Override Mentions

> Note: these are documented as *observed during recon*. ChadVis does not ship
> abuse tooling; see `feature-flags.md` for the defensive analysis.

- `scripts/orpheus-flag-override.js` forcibly sets Orpheus + marketplace gates to true by:
  - intercepting fetch to `/api/statsig/experiment/`
  - poisoning localStorage keys beginning with `statsig.cached.evaluations`
  - reapplying periodically
- Marketplace investigation explicitly frames the feature as credit-bounty based.
- Scan word lists include abuse-ish / bypass-ish flags:
  - `bypass_hook_feed_caches`
  - `bypass_unified_feed_caches`
  - `captcha_bypasses`
  - `captcha_oauth_bypasses`
  - `hide-credits-enabled`
  - `hide-credits-for-subscribers-enabled`
  - `out-of-credits-banner*`
  - `free_*`
  - `can_buy_credit_top_up`
  - `reward_credit`
  - `refund_credit`
  - `golden_ticket_reward_credit`
  - `jail-fraudulent-accounts-tenure-limit-days`
- `MARKETPLACE-INVESTIGATION.md` notes the marketplace backend is not deployed; all `/api/marketplace/*` return 404.

### Marketplace Probe Results

- Auth base: `https://studio-api-prod.suno.com`
- `/api/marketplace`, `/api/marketplace/listings`, `/api/marketplace/featured`, `/api/marketplace/categories`, `/api/labs/marketplace` => 404
- `/api/statsig/experiment/`:
  - onboarding layer returns Orpheus flags all false
  - creation layer returns `orpheus_group: "CONTROL"`
- `/api/modals` => 200, empty array
- `/api/project/me` => 200, workspace/project list
- `/api/billing/usage-plan-descriptions/` => 200, reveals Free/Pro/Premier plan text and credit limits

### Notes from HTML / Asset Files

- `Suno _ AI Music.html` is a saved copy of `https://suno.com/marketplace`.
- It includes GTM (GTM-NQ9L4VGG), OneTrust, and Cloudflare analytics references.
- `clerk.browser.js` is Clerk SDK internals; no Suno API routes beyond Clerk auth plumbing.
- `saved_resource` is GTM payload; it contains analytics events, not Suno API endpoints.

### Extra suno.ai Subdomains (to investigate)

| Subdomain | IP Address |
| :--- | :--- |
| cdn1.suno.ai | 3.167.99.88 |
| cdn2.suno.ai | 13.249.8.94 |
| audiopipe-dev.suno.ai | 44.217.60.1 |
| audiopipe.suno.ai | 54.156.152.125 |
| app.suno.ai | 104.20.18.158 |
| radio.suno.ai | 172.64.80.1 |
| accounts.suno.ai | 172.64.80.1 |
| dc.suno.ai | 172.64.80.1 |
| v-day.suno.ai | 172.64.80.1 |
| www.suno.ai | 172.64.80.1 |
| dev.suno.ai | 172.66.161.186 |
| staging-chopin.suno.ai | 172.66.161.186 |
| login.suno.ai | 172.66.161.186 |
| api.suno.ai | 172.66.161.186 |
| margu.suno.ai | 188.114.96.3 |
| console.suno.ai | 188.114.96.3 |
| datasets-api.suno.ai | 188.114.96.3 |
| chopin.suno.ai | 188.114.96.3 |
| datasets.suno.ai | 188.114.96.4 |
| podcasts.suno.ai | 188.114.97.3 |
| staging.suno.ai | 188.114.97.3 |
| data.suno.ai | 188.114.97.3 |
| alpha.suno.ai | 188.114.97.3 |
| clerk.suno.ai | 188.114.97.3 |
| ninja.suno.ai | 188.114.97.3 |
| demo.suno.ai | 188.114.97.3 |
| studio-api.suno.ai | 216.24.57.7 |
| api-staging.suno.ai | 216.24.57.7 |
| statusz.suno.ai | 216.24.57.251 |
| edu.suno.ai | N/A |
| dc-aa8e722993._spfm.suno.ai | N/A |
| studio.suno.ai | N/A |

---

*Originals archived to `.backup_graveyard/docs_20260825_175904/docs/`. Raw scan
data lives in [`raw/endpoints_sniffed.list`](raw/endpoints_sniffed.list).*
