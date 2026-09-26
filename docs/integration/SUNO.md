# Suno Integration Runbook

This document describes the user-facing integration workflow and its operating
gates. It is not an API specification. Route, schema, and evidence status belong
solely in [`../suno_api/ENDPOINT-INVENTORY.md`](../suno_api/ENDPOINT-INVENTORY.md).

## Current supported path

1. Open **Settings → Account**.
2. Supply a credential only through the current Account UI. Credentials are
   stored through the platform secure store; never paste them into logs, docs,
   tests, or issue reports.
3. Wait for the client to validate the session and refresh account/billing state.
4. Open **Library** and allow cursor pagination to settle. Use **Explore** for the captured explore feed and **Notifications** for the captured notification list.
5. Play or download only from a playable captured media entry. Do not use a
   legacy forbidden sentinel or a constructed CDN path.
6. Use **Create** for the captured `.m4a` upload transport and only for generation
   request shapes implemented and verified by the current client. A visible form
   is not proof of a complete generation flow.
7. Use **Video** for projectM, karaoke, overlays, presets, and recording. These
   remain secondary to the Suno client workflow.

## Authentication boundary

- Manual session establishment is the implemented path.
- Google sign-in is disabled. No reviewed capture proves that Clerk accepts a
  loopback or custom-scheme desktop callback; follow
  [`OAUTH_REDIRECT_ANALYSIS.md`](../suno_api/OAUTH_REDIRECT_ANALYSIS.md).
- Both captured Clerk session-token routes must be handled defensively. Their
  fallback order and universal requiredness remain capture-gated.
- A Google ID token is not a Suno Studio bearer and must never be placed in
  `SunoClient`.
- Disconnect must clear client/account state and must not silently discard the
  stored credential without user-visible state.

## Library behavior

- Feed pagination is cursor-based. Preserve the opaque response cursor and stop
  when the captured terminal condition is reached.
- Search is local until a direct request/response capture proves a server-side
  search field. UI state must not be described as remote search.
- SQLite is a local cache, not the source of remote truth. Refresh, merge, and
  error states must make stale or partial data visible.
- Model names, numeric limits, credits, and entitlements are runtime account
  data. Parse by JSON field type and do not hardcode historical values.

## Media and downloads

- Prefer the captured media array and its content type/delivery metadata.
- A non-empty legacy audio field can still be a forbidden sentinel; test for
  unplayable values before transport.
- The download queue exists, but range resume, pause/resume, local/remote parity,
  and playback handoff require end-to-end verification.
- Never synthesize a media URL from an account, clip, or session identifier.
- Never log or document complete private media URLs.

## Generation and uploads

- Generation requires the captured captcha decision and runtime model catalog.
  Missing fields must block submission rather than be replaced with guesses.
- A submitted batch is not complete until a directly observed read surface shows
  playable output.
- The captured audio-upload transport is initialize → direct multipart storage
  upload → finish. Processing, clip initialization, and generation linkage are
  separate gates.
- Temporary upload URLs and policy fields are secrets. Use them once, do not log
  or persist them, and never hard-code the storage host as a substitute for the
  returned URL.

## Lyrics and karaoke

- Aligned lyrics must flow from the client into the active lyrics/sync pipeline;
  fetching and caching alone do not make karaoke work.
- Preserve source lyrics and store edits as versions.
- Export, search, context, and upcoming-line UI remain unfinished until their
  bridge methods have real implementations and tests.
- A `complete` media/video status without a playable URL is not success.

## Failure handling

- Separate authentication, protocol/parser, rate/quota, network, and
  feature-unsupported failures. Do not label every 401 response “expired.”
- Do not assign stable meanings to 429/430 or other statuses without a direct
  capture of headers and redacted body.
- Clear per-request loading state on every terminal path and keep a queued
  request from becoming permanently pending.
- Lead-only or declaration-only routes must fail closed and must never receive
  a bearer or user cookie.

## Verification checklist

- Configure and build the current tree; do not rely on stale binaries.
- Run the correct unit/CTest targets and standalone auth tests.
- Exercise manual session restore/reauth/disconnect with an authorized account.
- Verify feed pagination, local search, account/model parsing, media selection,
  and download resume.
- Verify generation submission only after its current request contract is
  implemented and covered by fake-request tests.
- Confirm the Google action remains disabled with the native-callback gate.
- Perform a GUI smoke across Library, Explore, Create, Listen, Notifications, Video, and Settings.
- Update [`../PIVOT_PLAN.md`](../PIVOT_PLAN.md), `TODO.md`, and
  `CHANGELOG.md` to match observed behavior.
