# Suno B-Side & Experimental Features

This document is the power-user map for Suno's hidden, gated, and experimental surfaces.

## 1) All B-Side Routes

### `/b-side`
**Purpose**: Main B-side index.
**Status**: Active
**Access**: Direct URL; typically from internal B-side navigation.
**Notes**: Entry point for the experimental surface catalog.

### `/b-side/account-moderation`
**Purpose**: Account moderation tools.
**Status**: Active
**Access**: Internal/admin access.
**Notes**: Moderation and enforcement workflows.

### `/b-side/agentic-transcript`
**Purpose**: Agentic transcript feature.
**Status**: Active
**Access**: B-side route; entitlement/flag-gated.
**Notes**: Transcript tooling with agent-style interactions.

### `/b-side/agentic-transcript/:clipId`
**Purpose**: Per-clip agentic transcript.
**Status**: Active
**Access**: B-side route with clip ID.
**Notes**: Detail view for a single clip transcript session.

### `/b-side/api-explorer`
**Purpose**: API explorer (dev tool).
**Status**: Active
**Access**: Internal dev surface.
**Notes**: Useful for endpoint discovery and payload inspection.

### `/b-side/audible-magic`
**Purpose**: Audible Magic integration (copyright).
**Status**: Active
**Access**: B-side/admin access.
**Notes**: Copyright scanning and detection tooling.

### `/b-side/banner`
**Purpose**: Banner management.
**Status**: Active
**Access**: Internal/admin access.
**Notes**: Site/banner campaign controls.

### `/b-side/because-you-like`
**Purpose**: "Because You Like" recommendations.
**Status**: Active
**Access**: B-side route.
**Notes**: Recommendation experimentation surface.

### `/b-side/billing/revcat`
**Purpose**: RevenueCat billing integration.
**Status**: Active
**Access**: Internal/billing access.
**Notes**: Subscription and entitlement plumbing.

### `/b-side/contests`
**Purpose**: Contest management.
**Status**: Active
**Access**: Internal/admin access.
**Notes**: Contest creation, review, and operations.

### `/b-side/cover-art-eval`
**Purpose**: Cover art evaluation.
**Status**: Active
**Access**: B-side route.
**Notes**: Visual asset scoring and QA.

### `/b-side/creators`
**Purpose**: Creator tools.
**Status**: Active
**Access**: B-side route.
**Notes**: Creator-focused utilities and controls.

### `/b-side/describe-clip`
**Purpose**: Clip description tool.
**Status**: Active
**Access**: B-side route.
**Notes**: Generates or inspects clip metadata summaries.

### `/b-side/dsp-diag`
**Purpose**: DSP diagnostics.
**Status**: Active
**Access**: Internal diagnostics access.
**Notes**: Low-level audio pipeline troubleshooting.

### `/b-side/dsp-engine-talk`
**Purpose**: DSP engine talk.
**Status**: Active
**Access**: Internal diagnostics access.
**Notes**: Engine communication/debug surface.

### `/b-side/explore`
**Purpose**: Explore B-side features.
**Status**: Active
**Access**: B-side route.
**Notes**: Discovery hub for experimental tools.

### `/b-side/feature-flags`
**Purpose**: Feature flag control panel.
**Status**: Active
**Access**: B-side route; highest-value override surface.
**Notes**: Key panel for inspecting, toggling, and forcing gates.

### `/b-side/hipster`
**Purpose**: Hipster feature.
**Status**: Active
**Access**: B-side route.
**Notes**: Experimental UI/behavior variant.

### `/b-side/hook-song-gen`
**Purpose**: Hook song generation.
**Status**: Active
**Access**: B-side route.
**Notes**: Short-form song hook generation workflows.

### `/b-side/hooks-explorer/:slug*?`
**Purpose**: Hooks explorer.
**Status**: Active
**Access**: B-side route with optional slug.
**Notes**: Exploratory view for hook assets and states.

### `/b-side/impersonate`
**Purpose**: Impersonation (admin).
**Status**: Active
**Access**: Admin-only.
**Notes**: High-risk tool for support/moderation workflows.

### `/b-side/isthisus`
**Purpose**: "Is This Us" tool.
**Status**: Active
**Access**: B-side route.
**Notes**: Identity/style similarity or attribution check surface.

### `/b-side/labs-control`
**Purpose**: Labs feature management.
**Status**: Active
**Access**: Internal feature management.
**Notes**: Controls experimental labs availability.

### `/b-side/lyrics-eval`
**Purpose**: Lyrics evaluation.
**Status**: Active
**Access**: B-side route.
**Notes**: Scoring/QA for generated lyrics.

### `/b-side/lyrics-eval-reports/:slug*?`
**Purpose**: Lyrics eval reports.
**Status**: Active
**Access**: B-side route with optional slug.
**Notes**: Report browsing for evaluation runs.

### `/b-side/lyrics-eval/:slug*?`
**Purpose**: Lyrics evaluation detail.
**Status**: Active
**Access**: B-side route with optional slug.
**Notes**: Drill-down for a specific evaluation instance.

### `/b-side/lyrics-gen`
**Purpose**: Lyrics generation (B-side).
**Status**: Active
**Access**: B-side route.
**Notes**: Experimental lyrics-only generation surface.

### `/b-side/lyrics-viewer`
**Purpose**: Lyrics viewer.
**Status**: Active
**Access**: B-side route.
**Notes**: Display and inspection of lyrics output.

### `/b-side/milo`
**Purpose**: Milo feature.
**Status**: Active
**Access**: B-side route; also shown at `/labs/milo`.
**Notes**: Shared experimental feature surface.

### `/b-side/music-soulmate`
**Purpose**: Music soulmate feature.
**Status**: Active
**Access**: B-side route.
**Notes**: Matching/recommendation-style experimental feature.

### `/b-side/music-soulmate-talk`
**Purpose**: Music soulmate chat.
**Status**: Active
**Access**: B-side route.
**Notes**: Conversational companion to the soulmate feature.

### `/b-side/nux`
**Purpose**: New user experience.
**Status**: Active
**Access**: B-side route.
**Notes**: Onboarding/first-run experimentation.

### `/b-side/on-repeat`
**Purpose**: On Repeat feature.
**Status**: Active
**Access**: B-side route.
**Notes**: Replay-loop and retention experiment surface.

### `/b-side/onboarding-survey`
**Purpose**: Onboarding survey.
**Status**: Active
**Access**: B-side route.
**Notes**: Preference capture for personalization.

### `/b-side/orpheus`
**Purpose**: Orpheus chat.
**Status**: Hidden
**Access**: Requires `orpheus_is_enabled` and related experiment flags.
**Notes**: Core experimental chat surface; can become partially functional when flags are forced on.

### `/b-side/personalization`
**Purpose**: Personalization settings.
**Status**: Active
**Access**: B-side route.
**Notes**: Preference tuning and feature tailoring.

### `/b-side/playlist-copier`
**Purpose**: Playlist copier tool.
**Status**: Active
**Access**: B-side route.
**Notes**: Copy/export playlist workflows.

### `/b-side/project-state-tour`
**Purpose**: Project state tour.
**Status**: Active
**Access**: B-side route.
**Notes**: Guided inspection of project lifecycle state.

### `/b-side/search-lens/*`
**Purpose**: Search lens feature.
**Status**: Active
**Access**: B-side route; wildcard path.
**Notes**: Specialized search views and filters.

### `/b-side/simple-remix/:slug*?`
**Purpose**: Simple remix tool.
**Status**: Active
**Access**: B-side route with optional slug.
**Notes**: Lightweight remixing flow.

### `/b-side/song-moderation`
**Purpose**: Song moderation (admin).
**Status**: Active
**Access**: Admin-only.
**Notes**: Moderation queue and enforcement.

### `/b-side/sse-demo`
**Purpose**: SSE demo.
**Status**: Active
**Access**: B-side route.
**Notes**: Server-sent event testbed.

### `/b-side/studio-access`
**Purpose**: Studio access control.
**Status**: Active
**Access**: B-side route.
**Notes**: Entitlement and studio-access gating.

### `/b-side/trending-moderation`
**Purpose**: Trending moderation (admin).
**Status**: Active
**Access**: Admin-only.
**Notes**: Controls trending surfaces and ranking moderation.

### `/b-side/user-activity`
**Purpose**: User activity viewer.
**Status**: Active
**Access**: Internal/admin access.
**Notes**: Audit and behavior inspection.

### `/b-side/user-mix`
**Purpose**: User mix feature.
**Status**: Active
**Access**: B-side route.
**Notes**: Mixed personalization and content blending.

### `/b-side/user-similarity`
**Purpose**: User similarity.
**Status**: Active
**Access**: B-side route.
**Notes**: Audience clustering / similarity analysis.

### `/b-side/video-gen`
**Purpose**: Video generation (B-side).
**Status**: Active
**Access**: Hidden/flagged B-side route.
**Notes**: Experimental video creation surface.

### `/b-side/vip`
**Purpose**: VIP features.
**Status**: Hidden
**Access**: Gated by account entitlement or special subscription.
**Notes**: Usually the best place to look for premium-only experiments.

### `/b-side/visual-art`
**Purpose**: Visual art feature.
**Status**: Active
**Access**: B-side route.
**Notes**: Visual asset generation and inspection.

### `/b-side/visual-art/:type/:id`
**Purpose**: Visual art detail.
**Status**: Active
**Access**: B-side route with type and id.
**Notes**: Detail view for a specific visual artifact.

### `/b-side/voice-verification`
**Purpose**: Voice verification (V5.5).
**Status**: Active
**Access**: B-side route; likely gated by model/entitlement.
**Notes**: Voice identity / verification workflow.

## 2) Labs Routes

### `/labs/canvas`
**Purpose**: Canvas feature.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Experimental creation surface.

### `/labs/divisi`
**Purpose**: Divisi feature.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Experimental feature surface.

### `/labs/genre-wheel`
**Purpose**: Genre wheel.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Discovery and genre selection toy/tool.

### `/labs/listen-and-rank`
**Purpose**: Listen and rank.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Ranking/evaluation workflow.

### `/labs/live-radio`
**Purpose**: Live radio.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Streaming/radio-style experiment.

### `/labs/marketplace`
**Purpose**: Marketplace.
**Status**: Stub
**Access**: Visible in frontend only; backend currently 404.
**Notes**: Pre-staged commissioning platform; frontend routes exist but `/api/marketplace/*` does not respond.

### `/labs/milo`
**Purpose**: Milo.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Shared route with `/b-side/milo`.

### `/labs/pedalboard`
**Purpose**: Pedalboard.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Audio-effect or workflow experiment.

### `/labs/suno-mania`
**Purpose**: Suno Mania.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Game-like or showcase experimental feature.

### `/labs/turntable`
**Purpose**: Turntable.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Experimental playback or room-based feature.

### `/labs/turntable/:roomId`
**Purpose**: Turntable room.
**Status**: Active
**Access**: User-accessible via `/labs/` with room ID.
**Notes**: Specific room/session view.

### `/labs/verse`
**Purpose**: Verse feature.
**Status**: Active
**Access**: User-accessible via `/labs/`.
**Notes**: Experimental lyric/song-writing surface.

## 3) Orpheus Chat

### `/b-side/orpheus`
**Purpose**: Orpheus chat (big experimental surface).
**Status**: Hidden
**Access**: Master gate `orpheus_is_enabled` plus supporting experiment flags.
**Notes**: Backend is `https://suno-ai--orpheus-prod-web.modal.run`; `GET /session-history` is the key Modal endpoint.

### Orpheus flags
**Purpose**: Control chat mode, canvas, default UI, and experiment grouping.
**Status**: Active
**Access**: Statsig experiment gates.
**Notes**: Key flags are `orpheus_is_auto_mode`, `orpheus_is_canvas_enabled`, `orpheus_default_to_chat`, `orpheus_mobile_web_enabled`, and `orpheus_group` (`CONTROL`/`TREATMENT`).

### `orpheus-flag-override.js`
**Purpose**: Client-side override script.
**Status**: Active
**Access**: Inject in the browser before app boot.
**Notes**: Uses a 5-layer defense: fetch interception, `session.setExperiment()` interception, `ExperimentsContext` override, localStorage cache poisoning, and MutationObserver re-poisoning.

## 4) Marketplace

### `/labs/marketplace`
**Purpose**: Project-based commissioning platform.
**Status**: Stub
**Access**: Frontend stub only.
**Notes**: Mutations observed: `MARKETPLACE_CREATE_PROJECT`, `MARKETPLACE_FULFILLER_PROFILE`, `MARKETPLACE_UPLOAD_MEDIA`, `MARKETPLACE_PARTICIPANT_REVIEWS`, `MARKETPLACE_EDIT_LISTING`.

### `/api/marketplace/*`
**Purpose**: Marketplace backend surface.
**Status**: Hidden
**Access**: Not available; returns 404.
**Notes**: Do not depend on these endpoints for production flows yet.

## 5) VIP Features

### `/b-side/vip`
**Purpose**: VIP feature hub.
**Status**: Hidden
**Access**: Account-level gating or special subscription.
**Notes**: Likely early access, higher limits, or premium experiments.

## 6) Feature Flag Override for Power Users

### Statsig gates
**Purpose**: Feature access control.
**Status**: Active
**Access**: Client-side or server-side experiment evaluation.
**Notes**: The most important override targets are `orpheus_*`, `marketplace_*`, `labs_*`, `gen-video-covers`, `hide-credits-*`, and `free_*`.

### LocalStorage poisoning
**Purpose**: Force gate values in cached evaluations.
**Status**: Active
**Access**: Browser devtools, bookmarklet, or injected script.
**Notes**: Overwrite `statsig.cached.evaluations.*` to keep flags sticky across reloads.

### Bookmarklet / override script
**Purpose**: One-click activation for flag forcing.
**Status**: Active
**Access**: Run `orpheus-flag-override.js` or bookmarklet equivalent.
**Notes**: Best used with the feature-flag panel to verify the override took effect.

## 7) High-Value Implementation Targets

### `/b-side/feature-flags`
**Purpose**: Inspect and force the exact gates needed for experimental features.
**Status**: Active
**Access**: B-side control panel.
**Notes**: First stop for bringing hidden surfaces online in a controlled way.

### `/b-side/orpheus`
**Purpose**: Chat-driven generation and orchestration.
**Status**: Hidden
**Access**: `orpheus_is_enabled` + supporting flags.
**Notes**: The most important standout feature surface for power users.

### `/labs/marketplace`
**Purpose**: Future commissioning and bounty workflows.
**Status**: Stub
**Access**: Frontend-only today.
**Notes**: Ideal target for differentiating the client once backend support lands.
