# Suno Feature Flags & Statsig

## Statsig Integration

Suno uses Statsig for feature gating and A/B testing.

## Statsig Endpoints

- `POST /api/statsig/experiment/` — Query experiment params
  - Body: `{"layer_name":"onboarding-layer"|"creation-layer","parameter_name":"...","parameter_type":"boolean"|"string","parameter_default":...}`
- `GET /api/statsig/experiment/forked-onboarding` — Get forked onboarding variant

## Statsig Events (tracked)

- `statsig::gate_exposure` — Gate exposure event
- `statsig::config_exposure` — Config exposure event
- `statsig::layer_exposure` — Layer exposure event
- `statsig::non_exposed_checks` — Non-exposed checks

## localStorage Cache

- Key format: `statsig.cached.evaluations.{hash}`
- Contains: `feature_gates` and `layer_configs`
- Can be poisoned for client-side overrides

## Known Feature Gates (from scan)

### Orpheus Chat

- `orpheus_is_enabled` (`boolean`, default: `false`) — Master gate
- `orpheus_is_auto_mode` (`boolean`, default: `false`) — Auto-generation
- `orpheus_is_canvas_enabled` (`boolean`, default: `false`) — Canvas feature
- `orpheus_default_to_chat` (`boolean`, default: `false`) — Default to chat UI
- `orpheus_mobile_web_enabled` (`boolean`, default: `false`) — Mobile web
- `orpheus_group` (`string`, default: `"CONTROL"`) — Experiment group

### Marketplace

- `marketplace_enabled` — Marketplace access
- `marketplace_access` — Marketplace access (alt)
- `labs_marketplace` — Labs marketplace toggle

### General Features

- `gen-video-covers` — Video cover generation
- `labs_*` — Various labs features

### Credits/Billing Gates (from scan)

- `hide-credits-enabled`
- `hide-credits-for-subscribers-enabled`
- `out-of-credits-banner*`
- `free_*` — Various free credit flags
- `can_buy_credit_top_up`
- `reward_credit`
- `refund_credit`
- `golden_ticket_reward_credit`
- `bypass_hook_feed_caches`
- `bypass_unified_feed_caches`
- `jail-fraudulent-accounts-tenure-limit-days`

## Override Script (`orpheus-flag-override.js`)

5-layer defense-in-depth:

1. Fetch interceptor for `/api/statsig/experiment/`
2. `session.setExperiment()` interception via React fiber
3. `ExperimentsContext` override via React fiber
4. localStorage cache poisoning
5. MutationObserver for re-poisoning

Usage: paste in browser console on `suno.com`, then navigate to `/chat` or `/b-side/orpheus`.

## Bookmarklet

Generate with: `node scripts/generate-bookmarklet.js`

Creates one-click activation bookmark.
