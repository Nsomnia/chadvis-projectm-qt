Here is the consolidated and categorized list of all **Suno endpoints** extracted from the provided dump. 

Sensitive data (such as user tracking hashes, JWT placeholders, and specific UUID identifiers) has been sanitized and replaced with `[SANITIZED_ID]` or `[SANITIZED_TOKEN]`. External domains (e.g., Stripe, Google, Klarna, Datadog) have been excluded to strictly focus on Suno-owned endpoints.

(gnore /marketplace/ leading paths)
### 🌐 Core Domains & API Hosts
*   `https://suno.com`
*   `https://suno.ai`
*   `https://studio-api-prod.suno.com`
*   `https://studio-api.prod.suno.com`
*   `https://auth.suno.com`
*   `https://clerk.suno.com`
*   `https://s.prod.suno.com`
*   `https://help.suno.com`
*   `https://statusz.suno.ai`
*   `https://instrument-runner.suno.run`

### ⚙️ Studio & Generation APIs
*   `/marketplace/api/generate/concat/v2/`
*   `/marketplace/api/generate/sum/`
*   `/marketplace/api/generate/upsample`
*   `/marketplace/api/generate/cowrite-lyrics`
*   `/marketplace/api/generate/cowrite-lyrics/models/`
*   `/marketplace/api/generate/lyrics-mashup`
*   `/marketplace/api/generate/lyrics-infill/`
*   `/marketplace/api/generate/v2-web/`
*   `/marketplace/api/gen/trash`
*   `/marketplace/api/gen/bulk_increment_play_counts/v2`
*   `/marketplace/api/gen/increment_action_counts/`
*   `/marketplace/api/video_gen/pending_batches`
*   `/marketplace/api/video_gen/poll_batches`
*   `/marketplace/api/video/hooks/create`
*   `/marketplace/api/video/hooks/fetch_hook_lyrics`
*   `/marketplace/api/prompts/upsample`
*   `/marketplace/api/prompts/suggestions`
*   `/marketplace/api/prompts/v2`
*   `/marketplace/api/custom-model/create/`
*   `/marketplace/api/custom-model/archive/`
*   `/marketplace/api/custom-model/pending/`
*   `/marketplace/api/lyrics-projects`
*   `/marketplace/api/lyricists`
*   `/marketplace/api/instruments`
*   `/marketplace/api/instrument/describe-doodle`
*   `/marketplace/api/studio/render-state`
*   `/marketplace/api/studio/render-state-multitrack`
*   `/marketplace/api/studio/create-project`
*   `/marketplace/api/studio/save-project`
*   `/marketplace/api/openai-speech/`
*   `/marketplace/api/deepgram-token`
*   `https://studio-api-prod.suno.com/api/gen/[SANITIZED_ID]`

### 🎵 Clip & Playlist Management APIs
*   `/marketplace/api/clips/delete/`
*   `/marketplace/api/clips/parent`
*   `/marketplace/api/clips/adjust-speed/`
*   `/marketplace/api/clips/reverse-clip/`
*   `/marketplace/api/clips/aligned_clip_siblings`
*   `/marketplace/api/clips/aligned_clips`
*   `/marketplace/api/clips/autoplay/`
*   `/marketplace/api/clips/get_songs_by_ids`
*   `/marketplace/api/clips/get_similar/`
*   `/marketplace/api/clips/clip_roots`
*   `/marketplace/api/clips/remixes`
*   `/marketplace/api/clips/remixes/count`
*   `/marketplace/api/playlist/create/`
*   `/marketplace/api/playlist/me`
*   `/marketplace/api/playlist/update_clips/`
*   `/marketplace/api/playlist/set_metadata`
*   `/marketplace/api/playlist/trash/`
*   `/marketplace/api/download/clips/zip/prepare`

### 👤 User, Auth & Profile APIs
*   `/marketplace/api/user/me`
*   `/marketplace/api/user/metadata`
*   `/marketplace/api/user/user_config/`
*   `/marketplace/api/user/update_user_config/`
*   `/marketplace/api/user/delete-account/`
*   `/marketplace/api/user/reset_onboarding/`
*   `/marketplace/api/user/tos_acceptance`
*   `/marketplace/api/user/vip_program_acceptance`
*   `/marketplace/api/user/get_user_session_id/`
*   `/marketplace/api/auth/verify-token`
*   `/marketplace/api/signout/`
*   `/marketplace/api/session/`
*   `/marketplace/api/profiles/`
*   `/marketplace/api/profiles/pinned-clips`
*   `/marketplace/api/profiles/follow`
*   `/marketplace/api/profiles/mutual-followers`
*   `/marketplace/api/persona/create/`
*   `/marketplace/api/persona/get-followed-personas/`
*   `/marketplace/api/persona/get-loved-personas/`
*   `/marketplace/api/persona/get-personas/`
*   `/marketplace/api/onboarding/start`
*   `/marketplace/api/onboarding/current`
*   `/marketplace/api/onboarding/submit`
*   `/marketplace/api/onboarding/complete`
*   `/marketplace/api/onboarding/skip`
*   `/marketplace/api/onboarding/back`
*   `/marketplace/api/onboarding/audio-upload/abort`
*   `/marketplace/api/onboarding/audio-upload/remove`
*   `https://auth.suno.com/v1/client`
*   `https://clerk.suno.com/v1/client`
*   `https://suno.ai/claims/clerk_id`
*   `https://suno.ai/claims/email`

### 💳 Billing & Payment APIs
*   `/marketplace/api/billing/info/`
*   `/marketplace/api/billing/usage-plans`
*   `/marketplace/api/billing/usage-plan-descriptions/`
*   `/marketplace/api/billing/usage-plan-faq/`
*   `/marketplace/api/billing/usage-plan-web-table-comparison/`
*   `/marketplace/api/billing/tax-info`
*   `/marketplace/api/billing/eligible-discounts`
*   `/marketplace/api/billing/change-plan/`
*   `/marketplace/api/billing/change-plan/preview/`
*   `/marketplace/api/billing/cancel-sub/`
*   `/marketplace/api/billing/cancel-sub/undo/`
*   `/marketplace/api/billing/pause-sub/`
*   `/marketplace/api/billing/unpause-sub/`
*   `/marketplace/api/billing/auto-reload`
*   `/marketplace/api/billing/auto-reload/enable`
*   `/marketplace/api/billing/auto-reload/disable`
*   `/marketplace/api/billing/auto-reload/nudge-check`
*   `/marketplace/api/billing/create-portal/`
*   `/marketplace/api/billing/create-session/`
*   `/marketplace/api/billing/set-default-payment-method/`
*   `/marketplace/api/billing/conversion-tracking`
*   `/marketplace/api/billing/get-churn-survey-options`
*   `/marketplace/api/cms/paywall/plan-options`

### 🌐 Feed, Search & App Data APIs
*   `/marketplace/api/feed/v3`
*   `/marketplace/api/feed/v3/offset`
*   `/marketplace/api/unified/feed`
*   `/marketplace/api/unified/homepage`
*   `/marketplace/api/unified/explore`
*   `/marketplace/api/unified/homepage/explore`
*   `/marketplace/api/unified/homepage/explore/mobile`
*   `/marketplace/api/unified/search/omnisearch`
*   `/marketplace/api/unified/search/suggest`
*   `/marketplace/api/unified/search/suggest/history`
*   `/marketplace/api/search/`
*   `/marketplace/api/search/users`
*   `/marketplace/api/search/history`
*   `/marketplace/api/project`
*   `/marketplace/api/project/feed`
*   `/marketplace/api/project/me`
*   `/marketplace/api/project/invites`
*   `/marketplace/api/project/trash`
*   `/marketplace/api/project/library/images`
*   `/marketplace/api/project/library/videos`

### 📢 Engagement, Sharing & Notifications APIs
*   `/marketplace/api/notification/v2`
*   `/marketplace/api/notification/v2/read`
*   `/marketplace/api/notification/v2/badge-count`
*   `/marketplace/api/notification/v2/clear-badge`
*   `/marketplace/api/share/stats`
*   `/marketplace/api/share/event`
*   `/marketplace/api/share/link`
*   `/marketplace/api/share/attribute/`
*   `/marketplace/api/comment/block-user`
*   `/marketplace/api/comment/unblock-user`
*   `/marketplace/api/song_copy/send-song`
*   `/marketplace/api/contests/`
*   `/marketplace/api/invite/`
*   `/marketplace/api/survey/survey-responses`
*   `/marketplace/api/preferences/clip-review/pending`
*   `/marketplace/api/preferences/clip-review/submit`
*   `/marketplace/api/preferences/clip-review/opt-out`

### 🧪 Infrastructure, Feature Flags & Uploads APIs
*   `/marketplace/api/uploads/image/`
*   `/marketplace/api/uploads/video/`
*   `/marketplace/api/uploads/audio/`
*   `/marketplace/api/personalization/memory`
*   `/marketplace/api/personalization/settings`
*   `/marketplace/api/statsig/experiment/`
*   `/marketplace/api/statsig/experiment/forked-onboarding`
*   `/marketplace/api/mango/rights`
*   `/marketplace/api/music_player/playbar_state`
*   `/marketplace/api/realtime/discover`

### 🖥️ End-User Frontend Routes (Pages)
*   `/marketplace/home`
*   `/marketplace/explore`
*   `/marketplace/create`
*   `/marketplace/create/v2`
*   `/marketplace/create/voice`
*   `/marketplace/chat`
*   `/marketplace/studio`
*   `/marketplace/studio-legacy`
*   `/marketplace/studio-welcome`
*   `/marketplace/studio-waitlist`
*   `/marketplace/edit`
*   `/marketplace/edit-v3`
*   `/marketplace/edit-legacy`
*   `/marketplace/me` *(And sub-paths: `/albums`, `/cover-art`, `/followers`, `/following`, `/history`, `/hooks`, `/liked-hooks`, `/liked-playlists`, `/lyrics`, `/personas`, `/playlists`, `/studio-projects`, `/styles`, `/trash`, `/v2/*`, `/workspaces`)*
*   `/marketplace/account`
*   `/marketplace/account/restrictions`
*   `/marketplace/account/data-recovery`
*   `/marketplace/settings`
*   `/marketplace/song/`
*   `/marketplace/playlist/`
*   `/marketplace/profile/`
*   `/marketplace/Remix/Edit`
*   `/marketplace/stems`
*   `/marketplace/trending`
*   `/marketplace/discover`
*   `/marketplace/library`
*   `/marketplace/search`
*   `/marketplace/notifications`
*   `/marketplace/invites`
*   `/marketplace/listen-and-rank`
*   `/marketplace/registered-catalog`
*   `/marketplace/audio-search`
*   `/marketplace/activity`
*   `/marketplace/text-message-song`
*   `/marketplace/spark`
*   `/marketplace/songify`
*   `/marketplace/on_repeat`

### 🛠️ Labs & B-Side Routes (Internal / Experimental Features)
*   `/marketplace/labs/` *(Sub-paths: `canvas`, `divisi`, `draw`, `genre-type`, `genre-wheel`, `listen-and-rank`, `live-radio`, `marketplace`, `milo`, `multi-camera-lip-sync`, `pedalboard`, `rhythm-hero`, `riff-rider`, `serenity`, `splashpad`, `suno-jr`, `suno-mania`, `turntable`, `verse`, `vocal-isolator`, `beats`, `ia-next`)*
*   `/marketplace/b-side/` *(Sub-paths: `account-moderation`, `account-provisioning`, `agentic-transcript`, `api-explorer`, `audible-magic`, `autoplay`, `billing/revcat`, `bot-score`, `bucket-viewer`, `captioning`, `contests`, `cover-art-eval`, `creators`, `decks`, `describe-clip`, `drm-audio-preview`, `dsp-diag`, `dsp-engine-talk`, `explore`, `feature-flags`, `genre-charts`, `hf`, `hipster`, `hook-song-gen`, `i2i-cover-art-eval`, `impersonate`, `isthisus`, `labs-control`, `lip-sync`, `lyrics-eval`, `lyrics-gen`, `lyrics-viewer`, `milo`, `mse-quota-exceeded-recovery`, `music-soulmate`, `music-video`, `offline-feed-lab`, `on-repeat`, `onboarding-survey`, `orpheus`, `personalization`, `playlist-copier`, `project-state-tour`, `reward-model`, `search-lens`, `sf-vibe`, `shader-gallery`, `song-moderation`, `sse-demo`, `stem-extract-test`, `studio-access`, `style-synth`, `sunshine-list`, `timeline-primitives`, `trending-moderation`, `two-tower-rerank`, `user-activity`, `user-mix`, `user-similarity`, `vibe-blend`, `vip`, `visual-art`, `voice-verification`, `step-sequencer`, `video-gen`)*

### 🔒 Clerk / Authentication / Enterprise Routes
*   `/marketplace/sign-in`
*   `/marketplace/sign-up`
*   `/marketplace/login`
*   `/marketplace/auth`
*   `/marketplace/auth/birthday`
*   `/marketplace/auth/verify`
*   `/marketplace/auth/error`
*   `/marketplace/auth/session-recovery`
*   `/marketplace/sso-callback`
*   `/marketplace/oauth-redirect`
*   `/marketplace/oauth/authorize`
*   `/marketplace/oauth/end_session`
*   `/marketplace/link-account`
*   `/marketplace/reset-password`
*   `/marketplace/verify-email-address`
*   `/marketplace/verify-phone-number`
*   `/marketplace/payment_methods`
*   `/marketplace/billing`
*   `/marketplace/subscription`
*   `/marketplace/statements`
*   `/marketplace/payment_attempts`
*   `/marketplace/checkouts`

### 🗄️ CDNs & Media
*   `https://cdn1.suno.ai`
*   `https://cdn2.suno.ai`
*   `https://cdn-o.suno.com`
*   `/marketplace/s3/multipart`
*   `/marketplace/s3/sts`
*   *Static/Media specific found endpoints:*
    *   `https://cdn-o.suno.com/meta-preview.jpg`
    *   `https://cdn-o.suno.com/favicon.ico`
    *   `https://cdn1.suno.ai/sil-100.mp3`
    *   `https://cdn-o.suno.com/auras-v2/aura_vip.jpg`
    *   `https://cdn-o.suno.com/orpheus_default_bkg.png`
    *   `https://cdn-o.suno.com/broken-record.png`

### 📊 Tracking & Telemetry (Sanitized)
*   `/marketplace/9i3s/[SANITIZED_TRACKING_ID]` *(Includes sub-paths like `/g`, `/as`, `/gs`, `/td`, `/analytics.js`, `/ccm/conversion`)*
*   `/marketplace/api/share/stats`