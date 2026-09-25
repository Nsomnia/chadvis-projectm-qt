# ChadVis Pivot Plan: Suno Client First, projectM Second

> **Status:** adopted 2026-08-26; documentation and 2026-09-24 capture audit
> consolidated 2026-09-24.

## Product identity

ChadVis is a native desktop client for browsing, listening, generating, and
managing Suno content, with projectM as the secondary listening and music-video
engine. The primary product is the complete Suno client shell; projectM supports
reactive visuals, karaoke, scene composition, deterministic rendering, and later
batch automation.

This roadmap does not define an API. The sole live API authority is
[`suno_api/ENDPOINT-INVENTORY.md`](suno_api/ENDPOINT-INVENTORY.md).

## Binding rules

1. Use `[T1]`, `[LEAD]`, and `[VERIFY]` exactly as defined by the canonical
   inventory. Direct evidence is point-in-time; a listed route is not a
   supported or guaranteed Suno API.
2. Do not copy or infer API shapes from historical topic prose, external rewrite
   repositories, frontend bundle strings, or client constants.
3. Keep native Google sign-in disabled. A loopback or custom-scheme callback
   remains unproven; follow the gate in
   [`suno_api/OAUTH_REDIRECT_ANALYSIS.md`](suno_api/OAUTH_REDIRECT_ANALYSIS.md).
4. Never log or document secrets, personal data, identifiers, private text,
   temporary upload fields, or complete media URLs.
5. Use returned media URLs and captured media arrays. Never synthesize a CDN
   path or treat a non-empty forbidden sentinel as playable media.
6. Do not describe planned, declaration-only, or unverified behavior as shipped.
   Current branch state must pass a fresh configure/build/test/runtime gate.

## Current state

- The Suno-first shell, standalone Settings window, and Library/Create/Listen/
  Video navigation are implemented on the active client-shell branch, but the
  merged working tree must be reconfigured and rebuilt before they count as
  verified.
- Library feed, account/billing parsing, aligned-lyrics retrieval, download queue,
  and projectM/recorder composition exist at different maturity levels. Their
  presence is not proof of end-to-end behavior.
- The 2026-09-24 capture audit is complete. It promoted only directly observed
  contracts and left leads/conflicts explicitly gated.
- Native Google sign-in is disabled. Manual credential/session handling remains
  the implemented path.
- Server-side feed search, direct generation completeness, playlist mutations,
  Orpheus/Modal chat, WAV conversion, and several upload lifecycle steps remain
  capture- or implementation-gated.

## Roadmap

### P0 — Build and runtime foundation

- Reconfigure and build the current tree; do not rely on older build artifacts.
- Run the correct CTest directory plus standalone auth tests.
- Run QML lint on touched files and perform a manual window/navigation smoke.
- Keep the standalone QWindow/WindowContainer projectM embedding path unless a
  measured replacement is proven.

### P1 — Suno client correctness

- Keep one authenticated request queue with bounded concurrency, uniform 401
  handling, retry-once semantics, and explicit reauthentication state.
- Parse all directly captured Clerk token shapes defensively while leaving route
  preference and fallback order capture-gated.
- Normalize credential input and never persist or log secrets outside secure
  storage.
- Fail closed for unverified hosts, `[LEAD]` routes, and declaration-only client
  methods.
- Add fake-request contract tests for every enabled route and explicit deny tests
  for lead-only routes.

### P2 — Library, account, and downloads

- Verify cursor pagination, loading/error states, local database seeding, and
  refresh against a real authorized account.
- Use local search until a direct feed capture proves a server search field.
- Parse numeric model/account fields according to their JSON types; use the
  runtime model catalog rather than hardcoded generations or limits.
- Select playable media from captured media arrays and reject forbidden
  sentinels; remove all construct-from-ID URL behavior.
- Verify download resume, queue controls, local/remote parity, and playback
  fallback without blocking the UI thread.

### P3 — Create surface

- Use the captured captcha decision and generation fields as typed inputs.
- Populate models and limits from the runtime account/session response.
- Show queued, processing, completed, and failed states; do not clear a prompt
  without a durable result.
- Poll only through directly captured read surfaces. Do not activate
  declaration-only polling or unverified conversion routes.
- Add request/response contract tests before enabling upload/cover flows beyond
  the captured initialize → direct upload → finish sequence.

### P4 — Lyrics and karaoke

- Load aligned word timing into the active lyrics/sync pipeline.
- Keep source lyrics immutable; represent edits as versioned rows.
- Implement LRC/SRT export, search, context, and upcoming-line behavior.
- Display a clear fallback when aligned lyrics are absent or low-confidence.

### P5 — Recording and deterministic video export

- Prove live recording with GUI, real audio, encoder output, and playback review.
- Make recorder start/stop/cancellation, audio attachment, codec selection, null
  checks, and output naming deterministic.
- Decouple export from realtime using a master frame clock and exact PCM slices.
- Add hardware-encoder probing with a software fallback and named output presets.
- Pin projectM only after the required frame-time/render API is available and
  tested.

### P6 — Scene composition and keyframes

- Define a versioned scene schema for text, image, shape, visualizer, and karaoke
  elements with deterministic z-order.
- Use one renderer for preview and export to guarantee frame parity.
- Add per-property keyframe tracks, interpolation/easing, and parameterized
  effects.
- Resolve karaoke text at render time from the selected lyrics document.

### P7 — Automation and deferred work

- Fan batch rendering out from the single-track export service with durable,
  resumable, isolated run items.
- Defer creative-assist adapters, plugins, and lightweight DAW features until the
  client, lyrics, and deterministic export pipelines are stable.

## Exit criteria

A phase is complete only when the current tree configures/builds cleanly, relevant
tests pass, touched files satisfy formatting/lint checks, a real runtime smoke
covers the changed surface, and documentation/changelog state matches observed
behavior. A historical commit or passing stale binary is not completion evidence.
