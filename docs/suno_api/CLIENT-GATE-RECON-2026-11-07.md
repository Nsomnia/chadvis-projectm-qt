# Client-side gate recon — suno.com `/me` (2026-11-07)

> **Unofficial, scan-based research — `[LEAD]` throughout unless marked `[T1]`.**
> Suno does not publish a supported API contract for this client. These notes
> describe a point-in-time observation of delivered client code and a cached
> client-side evaluation store, may change without notice, and are not an
> official integration guide. ChadVis is not affiliated with or endorsed by
> Suno.
>
> This document records **what the delivered web client contains and how it
> evaluates flags**. It is not evidence that any backend route is authorized.
> Per [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) §4.9.1: forcing a client-side
> flag is not proof that the backend enabled a route, plan, model, or
> entitlement. The server is the source of truth; client overrides below are
> local evaluation overrides only.
>
> Authorization note: live recon was performed against the account owner's
> own authenticated browser session (`https://suno.com/me`, tab "Library |
> Suno"). No credentials, cookies, session IDs, clip IDs, or private user text
> were captured or are reproduced here. Raw chunk files are retained only in
> the gitignored `scratch/suno-gate-recon-2026-11-07/` directory.

## 1. Method

| Step | Tool | Result |
|---|---|---|
| 1. Confirm the live tab | browser-mcp (`tab list`, `snapshot -i`) | One active tab: "Library \| Suno" at `https://suno.com/me` |
| 2. Enumerate delivered scripts | browser-mcp `eval` over `performance.getEntriesByType('resource')` | 120+ JS resources, mostly `/_next/static/immutable/chunks/*.js` (Turbopack) plus third-party consent/analytics scripts |
| 3. Cache the chunks | `curl` from the public chunk URLs into `scratch/suno-gate-recon-2026-11-07/` (117 chunk files + `urls.txt`, 8.3 MB) | Local grep corpus; no authenticated requests were made to download static files |
| 4. Search the corpus | `grep -i` for `gate`, `experiment`, `staff`, `orpheus`, `feature_flag`, `chat`, `video`, `persona`, `unlock`, `statsig`, `ab.storage`, `/b-side` | Keyword file-hit counts in §2 |
| 5. Inspect local storage state | browser-mcp `eval` over `localStorage` keys | `ab.storage.*` (abermarket/Optimizely-style SDK keys, suffixed with a project UUID) and `statsig.*` keys present; no staff-override keys set for this account |
| 6. Inspect the cached Statsig payload | browser-mcp `eval` parsing `statsig.cached.evaluations.*` | 1,490 feature gates serialized under numeric IDs (see §3.3) |
| 7. Confirm the raw CDP endpoint | `curl http://127.0.0.1:9222/json/version` + `/json/list` | Chrome 157 with the Suno tab and DevTools reachable locally — the user's "remote chrome debug" endpoint; browser-mcp is the preferred operator path, CDP is the fallback |

All findings below cite the chunk basename so they can be re-located in
`scratch/suno-gate-recon-2026-11-07/`. Chunks are minified single-line
Turbopack output; snippets are paraphrased with module-level identifiers
preserved, not pasted verbatim at length.

## 2. Keyword hit counts (chunk files, case-insensitive)

| Term | Files (of 117) |
|---|---|
| `gate` | 53 |
| `video` | 56 |
| `persona` | 35 |
| `experiment` | 20 |
| `statsig` | 20 |
| `chat` | 16 |
| `staff` | 15 |
| `unlock` | 11 |
| `orpheus` / `ORPHEUS` | 5 |
| `featureFlag` | 4 |
| `feature_flag` | 3 |

Orpheus chunks: `0cysh5g9jmh27.js`, `0zm5fgj5ooymk.js`, `1kybzbpei4c9o.js`,
`1rfndc_h8sibu.js`, `3mwecypmgu7sf.js`. Staff/override chunk:
`2_fyg72oe3c-o.js`. Gate-name table chunk: `2rsopodz1gv1r.js`. Visual-gate
chunk: `06qz1vnoojxp9.js`.

## 3. Gate architecture (client side)

### 3.1 One evaluation plane: Statsig

The client evaluates gates, configs, experiments, and layers through
Statsig. Evidence:

- `statsigBootstrap` prop is passed into the client provider in
  `2_fyg72oe3c-o.js`.
- The `StatsigProvider`-adjacent store in `2_fyg72oe3c-o.js` carries a
  `statsigClient` (set via `setStatsigClient`), plus session fields including
  a `flags` envelope and `roles` object.
- `0ok9xm7gygi8j.js` (SDK chunk) maps the endpoint `feature_flags/sync` and
  the legacy `data` sync endpoint into a unified request/sync layer. This
  confirms the earlier inventory finding that `POST /api/statsig/experiment/`
  and `GET /api/statsig/experiment/forked-onboarding` are the experiment
  plane while `feature_flags/sync` is the feature-flag plane — both feed the
  same local cache.
- Live `localStorage` confirms the plane: keys `statsig.cached.evaluations.*`,
  `statsig.last_modified_time.evaluations`, `statsig.session_id.*`, and
  `statsig.stable_id.*` all exist on this account's `/me` session.

Conclusion: there is no second, Suno-owned flag service in the delivered
client. `ab.storage.ff.*` is the A/B SDK's own cache; the decision plane is
Statsig.

### 3.2 Gate, config, and experiment names (string names exist in code)

`2rsopodz1gv1r.js` contains a string-named table of gate/config identifiers
exported as `X` (the gate-name enum consumed by `useGateValue`). Observed
entries include:

| Enum member | String value |
|---|---|
| `SUNO_CHAT` | `suno-chat` |
| `VISUAL_CREATE` | `web-visual-create` |
| `VISUAL_GEMINI_IMAGE_CHECK` | `visual-gemini-image-check` |
| `VISUAL_PRESETS` | `web-visual-presets` |
| `VISUAL_CHARACTER_CREATOR` | `web-visual-character-creator` |
| `VISUAL_ALLOW_UPLOAD_IMAGE` | `visual-allow-upload-image` |
| `VISUAL_BIOMETRIC_CONSENT_EXEMPT` | `tns-visual-biometric-consent-exempt` |
| `UPLOAD_CONSENT_SERVER_RECORD` | `tns-upload-consent` |
| `VISUAL_MODERATION_FEEDBACK` | `tns-visual-moderation-feedback` |
| `VOCAL_TUNE` | `vocal-tune` |
| `VOICES_IN_STUDIO` | `voices-in-studio` |
| `STUDIO_V2_COLLAB` | `studio-v2-collab` |
| `STUDIO_V2_SKILLS` | `studio-v2-skills` |
| `STUDIO_FULL_SESSION` | `studio-full-session` |
| `CUSTOM_MODEL_SHARING` | `custom-model-sharing` |
| `WEB_HOOKS` | `web-hooks-2025` |
| `SEARCH_ARTIST_REF` | `search-artist-ref` |

Plus consent/billing-adjacent values seen in the same table in other chunks
(`generate_song_video`, `get_stems`, `commercial_rights`,
`can_buy_credit_top_ups`). The full table is `[LEAD]` scan output; names
observed in a bundle are not contracts and must not be called as routes.

### 3.3 The cached evaluation store is numeric — names do not round-trip

The live `statsig.cached.evaluations.*` payload holds 1,490 feature-gate
evaluations keyed by **numeric IDs** (`"1084832": {"name": "1084832", …,
"value": false, "rule_id": "default", "id_type": "userID"}`). String gate
names from §3.2 do not appear as keys; the name→numeric-ID map lives in the
SDK's internal bootstrap, not in the cache.

Practical consequences for a researcher:

1. Reading `localStorage` tells you *how many* gates exist and which numeric
   IDs are `true` for the account — it does not tell you *what they mean*
   without the bootstrap map.
2. Hand-editing a cached value `false → true` is overwritten on the next
   `feature_flags/sync` / experiment refresh (`ab.storage.lastSdkReq` and
   `ab.storage.ffLastRefreshAt` show the refresh cadence) and, per §3.1, the
   cache is a copy of a server decision — §4.9.1's rule applies.
3. The persona "local override" that reportedly worked in the past most
   plausibly operated *below* this plane — a local UI/localStorage switch
   independent of Statsig (see §5.2). When the revamped surface moved behind
   server-side `feature_disabled` responses, the local trick stopped having a
   consequence. That is consistent with, and predicted by, the §4.9.1 rule.

### 3.4 How a gate is consumed

`3gldjhkz0vsrz.js` shows the canonical consumer pattern for chat:

```js
const enabled = useGateValue(Gate.SUNO_CHAT);   // e3.X.SUNO_CHAT === "suno-chat"
const warmed  = useWarmup();                    // prefetch side effect
return enabled && warmed && isSignedIn
  ? <ChatSurface/>          // lazy chunk 456399 (824012)
  : null;
```

`06qz1vnoojxp9.js` shows the Visuals equivalent for `GET /api/visual/models`:

- `queryFn` calls `GET /api/visual/models`; on success the catalog is cached
  as `{status: "enabled", catalog}`; on the `(0, i.j)(r.status, t)` failure
  branch the query is hard-cached as `{status: "disabled"}` with infinite
  `staleTime` until that branch resets it.
- This is the client mirror of the inventory's `[T1]` row: the endpoint
  answers `403 feature_disabled` for this account. A client gate that reads
  from this query cannot be force-enabled by rewriting the query cache;
  the next refetch re-reads the server verdict.

## 4. Staff override surface (present in code, gated at runtime)

### 4.1 The two storage keys

`2_fyg72oe3c-o.js` contains both keys as string literals:

| Key | Purpose | Default |
|---|---|---|
| `suno-staff-statsig-overrides` | Staff gate/config/experiment/layer/param override state | `{"gates": {}, "configs": {}, "experiments": {}, "layers": {}, "params": {}}` |
| `suno-staff-flag-toolbar-config` | Toolbar UI state (open/minimized, favorites, open sections) | `{"isOpen": false, "isMinimized": false, "areOverridesActive": true, "frame": null, "favorites": [], "openSections": {}}` |

The override store is a small class with mutators `setGateOverride`,
`setConfigOverride`, `setExperimentOverride`, `setLayerOverride`,
`setObjectOverride`, `setParamOverride`, each calling `commit()` to persist.
Overrides are applied with a synthetic evaluation that carries
`reason: "LocalOverride"` — visibly distinct from server evaluation reasons.

This contradicts the earlier Explorer survey finding that these keys had "no
on-disk evidence": the chunk that holds them (`2_fyg72oe3c-o.js`) was fetched
in this session and they are present. The earlier negative result was a
corpus gap, not an absence in the live client.

### 4.2 The runtime guard — why seeding localStorage is not enough

Three independent guards sit between the store and the UI:

1. **Staff role.** The toolbar component mounts only when
   `session.isStaff || <devTools flag>` is true *and* the session is loaded
   *and* the path does not start with `/embed`. `isStaff` derives from
   `roles?.staff` in the session envelope.
2. **Application toggle.** The override engine's `staffEnabled` flag gates
   `setGateOverride` / `setObjectOverride` / `setParamOverride`; the toolbar's
   `areOverridesActive` toggle is the user-facing master switch.
3. **Freshness.** Both keys are read via `getItem` at module init and cached
   in-memory (`useSyncExternalStore` subscribers). Seeding a key in a second
   tab does not update an already-initialized store without a reload.

On this account, `localStorage` contains **neither** key (verified live,
§1 step 5), so the surface is dormant. Copying the keys into the account's
storage by hand is expected to have no visible effect: the mount guard
requires `roles.staff`, which a client cannot mint. What a researcher *can*
do is (a) confirm the guard by profile-reading which branch blocks their
account, and (b) treat any flag name enumerated through this surface as a
`[LEAD]` naming the gate, not as access to it.

### 4.3 Direct HTTP / DevTools evaluation

For a researcher in their own DevTools console (not a shipped bypass):

1. `localStorage.getItem("suno-staff-statsig-overrides")` →
   observed `null` on this account (surface dormant).
2. `localStorage.getItem("suno-staff-flag-toolbar-config")` → observed
   `null` on this account.
3. Parsing `statsig.cached.evaluations.*` (double-JSON) yields the 1,490
   numeric gates (§3.3); `statsig.stable_id.*` and `statsig.session_id.*`
   bind evaluations to this session's user ID.

There is no `window.__suno`, `window.__STATSIG__`, or documented console
handle for force-evaluation in the delivered chunks; the store is module
scoped. Global handle hunts (`window.`, `globalThis.`) in the corpus hit
only framework internals. The realistic researcher path stays through the
cached evaluation payload and the string-name table — both `[LEAD]`.

## 5. Per-surface findings

### 5.1 Chat (`suno-chat`)

- Gate consumer in `3gldjhkz0vsrz.js` (pattern in §3.4); lazy surface chunk
  `456399`.
- The user's goal of re-enabling chat is blocked at the account plane, not the
  client: captured `POST /api/suno_chat/rooms` answers `403
  feature_disabled` `[T1]` (inventory §4.9/§4.11). Even a locally forced
  `suno-chat` gate would render a surface whose first real request is
  rejected. The "major revamp" the user noticed matches this shape: the
  revamped client still reads the same server verdict.
- Research step that *is* possible: watch the request/response body of the
  existing 403 through DevTools Network (endpoint + status + top-level keys
  only) and record it as `[T1]` per the sanitized-evidence rules. Do not
  synthesize chat requests from the gate name.

### 5.2 Personas

- 35 chunk files mention personas (voice/persona creation and library
  surfaces); the desktop gate is `Personas` in `src/suno/FeatureFlags.hpp`
  ([`FeatureFlags.hpp`](../../src/suno/FeatureFlags.hpp)), graded Lead.
- The historical persona unlock was a **local override feature**, not a
  Statsig override: no `persona.*unlock` / `unlock.*persona` / local-override
  string joins a persona and an unlock in the current corpus. The unlock
  filters found in `2bwyuw3yn4v1l.js` / `2al-si5rchcfi.js` are download/flow
  unlocks (`is_download_unlocked`, `filters.unlocked`), not persona gates.
- Inference (labelled as such): the old trick flipped a *client-local*
  persona switch that the old surface consulted directly. The revamped
  surface evidently consults the server persona/voice state instead — hence
  "hasn't found anything that worked in some time". Re-proving this needs a
  captured persona request showing where the verdict now comes from, not
  another localStorage seed.

### 5.3 Video / Visuals (`web-visual-create` family)

- Product client gate: `GET /api/visual/models` → `403 feature_disabled`
  `[T1]` for this account (§3.4). The workspace surface is additionally
  gated on `video-to-song` (inventory §4.7 note).
- The b-side lab route `/b-side/music-video` exists in the router (87
  `/b-side/*` routes total, §5.5) but is a page route — §4.9.2's warning
  applies: it does not imply a `/api/...` contract.
- Delivered client contains ElevenLabs-style "describe clip" deep links
  (`/b-side/describe-clip?clipId=…&autoSubmit=1` in `0zm5fgj5ooymk.js`) gated
  by an internal `tg.L`-style flag and the session's staff/presence check —
  another local-only affordance with no server contract implied.

### 5.4 Orpheus

- Base URL is computed in `1kybzbpei4c9o.js`:
  `NEXT_PUBLIC_ORPHEUS_ENV` (default `prod`) interpolated into
  `https://suno-ai--orpheus-{n}-web.modal.run`. This is the only live Orpheus
  host string in the corpus.
- Unverified knobs: `orpheus_group` is read as a Statsig parameter of
  `layerName: "creation-layer"`, `parameterType: "string"`,
  `parameterDefault: CONTROL` (`3mwecypmgu7sf.js`). Experiment IDs are
  applied via `setExperiment(t, !!_)` behind the same creation-layer hook —
  i.e., Orpheus enablement is expressed as *experiment assignment*, not as a
  boolean gate, in the current client.
- The remaining flags (`orpheus_is_enabled`, `orpheus_is_auto_mode`,
  `orpheus_is_canvas_enabled`, `orpheus_default_to_chat`,
  `orpheus_mobile_web_enabled`) are legacy scan names (§4.9.1) with no
  consumer in the current corpus — treat as retired, not as switches waiting
  to be flipped. No Orpheus request/response contract exists (see
  [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) §4.10); the page route is
  `/b-side/orpheus`. Enabling "the full Orpheus testing feature set" from
  the client is not supported by any evidence in this session.

### 5.5 B-Side (87 page routes in the router)

The router chunk (`1rfndc_h8sibu.js`) contains 87 `/b-side/*` page routes,
including `/b-side/feature-flags`, `/b-side/impersonate`,
`/b-side/bucket-viewer`, `/b-side/api-explorer`, `/b-side/orpheus`,
`/b-side/music-video`, `/b-side/visual-art`, and `/b-side/studio-access`.
Per §4.9.2 these are frontend page routes (browser navigation targets), not
API paths, and their presence is not authorization. They are listed here to
close the loop with TODO T0146/T0147, which keep `/b-side/*` excluded from
implementation.

## 6. Endpoint evidence (server plane, `[T1]`, carried over)

| Method | Path | Verdict for this account |
|---|---|---|
| POST | `/api/suno_chat/rooms` | `403 feature_disabled` |
| GET | `/api/visual/models` | `403 feature_disabled` |
| GET | `/api/feature-flags` (legacy base) | `401` for anonymous access |
| POST | `/api/statsig/experiment/` | declared unused in code |
| GET | `/api/statsig/experiment/forked-onboarding` | returns the default onboarding group via a Statsig parameter store |

These rows are owned by [`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md)
§4.9/§4.11 and are restated here only to show the asymmetry this session
confirmed: **every gate that matters answers `feature_disabled` at the
account plane**, so client-side overrides change what renders, not what
the server permits.

## 7. What was not found

- No general-purpose localStorage/console force-gate handle in the delivered
  client.
- No working persona-unlock local override in the current corpus; the
  download-unlock strings are a different feature.
- No Orpheus client-side boolean switch; enablement is experiment-shaped.
- No additional flag server beyond Statsig; `ab.storage` is the A/B SDK's
  cache, `feature_flags/sync`/`data` are its sync endpoints.
- No evidence that any numeric gate ID maps to a named gate without the SDK
  bootstrap; the cache alone is opaque.

## 8. Reproduction notes (researcher-local)

1. Open `https://suno.com/me` in the CDP-attached Chrome
   (`--remote-debugging-port=9222`) or via browser-mcp; confirm the tab is
   "Library | Suno".
2. In DevTools, run:
   `performance.getEntriesByType('resource').filter(r=>r.name.includes('.js'))`
   to re-enumerate the Turbopack chunks in this session.
3. Fetch the chunk URLs into `scratch/suno-gate-recon-2026-11-07/` and rerun
   the keyword counts in §2.
4. Inspect `Object.keys(localStorage)` for `statsig.*`, `ab.storage.*`,
   `suno-staff-*` (expect the last to be absent for non-staff accounts).
5. Double-parse `statsig.cached.evaluations.*` and count
   `data.feature_gates` (1,490 this session).

Chunks rotate per deploy; basenames and counts above are point-in-time.

## 9. Alignment with project doctrine

- Findings stay `[LEAD]` and do not promote any row in
  [`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md).
- `src/suno/FeatureFlags.hpp` resolution order (evidence → tier → absent
  flag → local switch, fail-closed below `Captured`) is unchanged: a
  client-side override may not promote a `ServerGated` verdict, and nothing
  in this document asks it to.
- The staff keys resolve the Explorer survey's "unverified claim" gap in
  favour of *present in code, dormant at runtime* — the correct posture is
  "recorded, not wired", matching T0370's read-only boundary.
