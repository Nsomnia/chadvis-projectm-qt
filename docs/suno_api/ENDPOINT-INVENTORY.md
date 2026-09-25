# Suno API Canonical Endpoint Inventory

**Status:** Canonical API-spec master, consolidated 2026-09-24
**Current evidence baseline:** the 2026-09-24 Burp export reviewed on 2026-09-24, reconciled with the 2026-08-25 Burp corpus, 2026-09-22 sanitized OAuth recon, and 2026-09-23 browser HAR
**Scope:** Suno web authentication, Studio API, library/feed, generation leads, media processing, account surfaces, social surfaces, and experimental leads

> **Unofficial and reverse-engineered.** Suno does not publish this API as a
> supported public API. This inventory records behavior observed from the
> production web application, sanitized request recon, and static/endpoint
> scans. A listed route is not an endorsement, availability guarantee, or
> permission to bypass access controls. Use only accounts and actions you are
> authorized to use, and accept that private interfaces can change without
> notice.

> **Canonical-source rule:** this file is the sole API-spec master for this
> repository. Where it conflicts with older topic prose, scans, implementation
> comments, or code constants, this file wins. `[T1]` means a behavior was
> documented from a real historical capture; it does **not** mean the endpoint
> was independently revalidated against Suno's live service while preparing
> this document. Do not implement a `[LEAD]` or `[VERIFY]` route as though it
> were a stable contract.

## 1. Evidence, precedence, and conflicts

### 1.1 Confidence labels

| Label | Meaning | Permitted use |
|---|---|---|
| `[T1]` | Directly present in a cited request/response capture, with secrets redacted. For the 2026-09-22 OAuth recon, request presence only is T1. | Historical contract, subject to drift and client-side defensive parsing. |
| `[LEAD]` | Found in a JS/HTML scan, raw endpoint dump, old inventory, reconstructed path, or prose without a direct capture. An unlabeled old row is always a lead. | Research and capture planning only. |
| `[VERIFY]` | Sources conflict, or one part of the route contract is not captured. | Do not call until a new capture resolves it. |

Additional notation used in the catalog:

- A trailing `?` on a method, such as `GET?`, means the method came from prose
  or reconstruction rather than a direct capture.
- `Bearer?` means a bearer is plausible for a Studio route but the individual
  route's auth requirement was not directly established.
- `Clerk cookies` means only the Clerk cookie names in section 2.4 are
  relevant. Analytics and advertising cookies are not authentication inputs.
- `GET/POST` means both methods were directly captured; it does not mean other
  methods are impossible.
- Path spellings, trailing slashes, query-key names, and body-field names are
  significant. Do not silently normalize them from a scan.

### 1.2 Precedence

1. The 2026-09-24 Burp request/response export is strongest for every contract
   it directly contains. Its directory is labeled `sept-09-2026`, but the XML
   export and item timestamps are dated 2026-09-24; use the timestamps, not the
   directory label, when dating evidence. The base64 XML is **not sanitized**.
2. The direct sanitized request capture `raw/sanitized-recon-2026-09-22.json`
   is strongest for the **observed Google OAuth web-request sequence**. It has
   no response status or response body, so this document assigns no status/body
   semantics to that sequence.
3. The 2026-08-25 Burp request/response exports and their sanitized extracts
   govern captured feed, generation, billing, project, media-analysis, and
   other Studio behavior not directly re-observed in the 2026-09-24 export.
4. The 2026-09-23 browser HAR governs captured web/Studio traffic and the
   progressive `media_urls` observations.
5. Corrective commit `678be76` and the current endpoint map are implementation
   references, not evidence that can override a newer direct capture.
6. Raw scans, decoded frontend bundles, and old topic prose supply `[LEAD]`
   inventory only. External SDK folklore, constructed media paths, and
   client-side flag names cannot promote a route above `[LEAD]`.

### 1.3 Explicit conflict register

| Subject | Conflicting claims | Canonical resolution | State |
|---|---|---|---|
| Clerk session-token exchange | Older material rejected `/tokens`; the 2026-09-24 export directly captured both `POST /v1/client/sessions/{sid}/tokens` and `POST .../touch`. | Both are `[T1]` observed same-host session routes. `/tokens` returned a top-level `jwt`; `touch` returned client/session envelopes. `/tokens/api` remains unobserved. Which route a client should prefer, and whether either is universally required, remains `[VERIFY]`. | **Resolved coexistence; selection `[VERIFY]`** |
| `/v1/client/verify` | Older auth material disagreed between GET and POST. | No reviewed capture selects a method or establishes a generic verification contract. Do not use it as bearer refresh or a generic preflight. | **Unresolved `[VERIFY]`** |
| `/v1/verify` | Older auth material disagreed between POST and GET. | No reviewed capture establishes either method or response contract. | **Unresolved `[VERIFY]`** |
| `client?_method=PATCH` | Older auth material disagreed between GET and POST. | `_method=PATCH` suggests an override form, but neither transport method is captured. Do not synthesize a request. | **Unresolved `[VERIFY]`** |
| `/api/song_copy/send-song` | Older social material and inventory disagreed between GET and POST. | Route exists only as a lead in the available corpus; method and payload are unproved. | **Unresolved `[VERIFY]`** |
| `/api/openai-speech/` | Old material and scans describe a GET surface, but no reviewed request/response capture establishes its method or compatibility contract. | Do not promote the GET claim. Preserve the route only as `[VERIFY]`. | **Unresolved `[VERIFY]`** |
| `/api/user/user_config/` | Older material used GET. | `POST` with an empty JSON object is `[T1]`; the update/patch schema is not established by that read. | **Resolved for read method** |
| `/api/unified/feed` and `/api/unified/homepage` | Older material used GET. | Both are captured as `POST`. | **Resolved** |
| `/api/mango/rights` | Older inventory used GET. | `POST` is `[T1]`. | **Resolved** |
| `/api/billing/conversion-tracking` | Older inventory used POST. | `GET` is `[T1]`. | **Resolved** |
| `/api/notification/v2/clear-badge` | Older inventory used GET. | The 2026-08-25 capture observed `POST` with an empty 204 response. | **Resolved** |
| Generation polling | Old prose presented `GET /api/gen/{id}` as the canonical poll. | The captured generation flow observed completion through `POST /api/feed/v3` and `GET /api/clips/get_songs_by_ids`. `/api/gen/{id}` remains a `[LEAD]`. | **Resolved for captured flow** |
| `/api/uploads/video` vs `/api/uploads/video/` | Both spellings occur in scans/docs. | No reviewed capture selects one. Preserve separate path leads; their methods remain `[VERIFY]`, and clients must not alias the spellings. | **Unresolved `[VERIFY]` at use time** |
| `/api/generate/lyrics-infill` vs `/api/generate/lyrics-infill/` | Both spellings occur in the raw scan. | Both are `[LEAD]`; neither may be rewritten into the other. | **Unresolved `[VERIFY]` at use time** |
| Orpheus paths/models | Claims range from `/session-history` to orchestrator paths and OpenAI-compatible `/api/v1/...` paths; model names were listed without a direct contract capture. | All are `[LEAD]` or `[VERIFY]`; no supported Orpheus API is canonical. | **Unresolved** |

### 1.3 Current client implementation boundary

The current client has capture-backed service boundaries for same-host Clerk
refresh, `/api/feed/v3`, account/billing reads, Explore, notifications,
media-backed download/playback, and the three-leg `.m4a` upload transport.
Generation submission remains intentionally disabled because the captured
CAPTCHA token flow and durable processing contract are not implemented. The
B-Side/Orpheus, WAV-conversion, constructed-media, and bundle-only routes remain
disabled regardless of local UI flags. See
[`../integration/SUNO.md`](../integration/SUNO.md) for the operational runbook.

## 2. Base URLs and request conventions

### 2.1 Hosts

| Host | Role | Evidence |
|---|---|---|
| `https://suno.com` | Web application and OAuth final destination. | `[T1]` Burp/HAR/recon |
| `https://auth.suno.com` | Current Clerk host; catalog auth paths are host-relative. | `[T1]` 2026-08-25 and 2026-09-24 Burp; 2026-09-22 recon |
| `https://studio-api-prod.suno.com` | Primary Studio API host. | `[T1]` 2026-08-25 and 2026-09-24 Burp; 2026-09-23 HAR |
| `https://suno-uploads.s3.amazonaws.com` | Temporary direct multipart upload target returned by the audio-upload initializer. | `[T1]` 2026-09-24 request/response |
| `https://cdn1.suno.ai`, `https://cdn2.suno.ai` | Captured image asset hosts. | `[T1]` response fields and direct requests, 2026-09-24 |
| `https://d2lwuy8qc234o3.cloudfront.net` | Captured progressive-media host. | `[T1]` response values in captured clip objects |
| `https://audiopipe.suno.ai` | Captured streaming-audio host. | `[T1]` response values in 2026-09-24 clip objects |
| `https://studio-api.prod.suno.com` | Alternate host observed only in returned media fields, including the forbidden sentinel; not an API base. | `[T1]` response value only |
| `https://suno-ai--orpheus-prod-web.modal.run` | Claimed Orpheus service. | `[LEAD]`; no reviewed Orpheus request/response contract |
| `https://clerk.suno.com` | Prototype-era/legacy Clerk host. | `[LEAD]`; not the canonical current web host |
| `https://studio-api.sky.suno.com` | Alternate/staging name found in scans. | `[LEAD]`; do not substitute for the captured production host |

All Studio routes in this document are host-relative paths beginning with
`/api/`, for example `https://studio-api-prod.suno.com/api/feed/v3`. Raw scan
artifacts such as `/marketplace/api/...` are frontend-prefix contamination, not
literal Studio endpoints. Normalize those artifacts back to `/api/...` before
comparison, while preserving the remainder of the path and trailing slash.

### 2.2 Studio request conventions

- Authentication is normally `Authorization: Bearer {suno_api_jwt}`.
- Captured Studio requests also used `Device-Id`, `Browser-Token`, `Origin`,
  `Referer`, and a browser `User-Agent`. Treat the exact required subset as
  route/capture-specific; do not claim every route requires every header.
- `Device-Id` was a UUID-shaped value in capture. `Browser-Token` encoded a
  timestamp-shaped JSON value. Neither value is reproduced here.
- Captured cross-origin Studio request families were preceded by successful
  `OPTIONS` preflights. Native clients do not gain browser CORS behavior, but
  cookie-jar and header handling still need capture-backed testing.
- JSON, form encoding, and multipart are not interchangeable. The feed and
  Studio audio-upload initializer/finisher use JSON; Clerk session POSTs use
  form encoding; the returned storage URL receives a direct multipart upload.
  For all other routes, use only a directly captured content type.
- The direct storage upload does not use the Suno bearer. It uses the temporary
  URL and policy fields returned by the initializer. Treat those fields as
  short-lived secrets: never log, persist, or construct them independently.
- IDs are opaque. Do not assume a UUID, numeric ID, or session ID can be
  substituted for another merely because their textual forms differ.
- Do not automatically add or remove a trailing slash. Both forms can occur in
  scans, and the distinction is sometimes meaningful.
- Do not infer required fields from JavaScript property names, UI labels, or
  old prose. Only captured field names belong in a request contract.

### 2.3 Response handling

- Parse defensively: a field not required by a captured request may be absent,
  null, empty, or added later.
- Distinguish an empty string from a missing field. Captured generation clips
  begin with empty media URL strings while processing.
- Do not log bearer tokens, Clerk cookies, OAuth state/code values, user IDs,
  email addresses, or complete media URLs when they identify private content.
- The 2026-09-22 recon is request-only. It establishes no response status,
  redirect status, response body, or cookie-setting behavior for that run.

### 2.4 Clerk cookies relevant to the observed flow

Only these Clerk-related name families are relevant to the captured flow:

- `__session` and its instance-key-suffixed variant
- `__client` and its instance-key-suffixed variant
- `__client_uat` and its instance-key-suffixed variant

The exact instance-key suffix is intentionally omitted. Do not assume a fixed
Clerk frontend key is universal. Analytics, advertising, payment, and RUM
cookies seen alongside the flow are not authentication requirements and are
intentionally omitted.

## 3. Authentication and OAuth

### 3.1 Canonical bearer acquisition and session-token routes

1. Send `GET /v1/client` on the Clerk host with the captured cookie context.
   The 2026-09-24 request had no query keys; the 2026-08-25 request included
   version query keys. Both are `[T1]` point-in-time variants.
2. Read the active session identifier from
   `response.last_active_session_id` and the bearer from the matching
   `response.sessions[].last_active_token.jwt`. Do not assume array position.
   `[T1]`
3. Use that bearer as `Authorization: Bearer {jwt}` for Studio API calls.
   `[T1]`
4. `POST /v1/client/sessions/{sid}/touch` is directly observed. The 2026-09-24
   request had no query keys and used form body `intent=focus`; its 200 response
   contained both `client` and `response` session envelopes. The 2026-08-25
   capture used version query keys and an empty form body. Both are `[T1]`
   point-in-time variants; a client must not synthesize a hybrid or assume one
   variant is universally required.
5. `POST /v1/client/sessions/{sid}/tokens` is also directly observed. The
   2026-09-24 request had no query keys and an empty form body; its 200 response
   was a top-level object containing `jwt`. `[T1]`

`POST /v1/client/sessions/{sid}/tokens/api` remains unobserved. Because both
`tokens` and `touch` are now captured, neither may be described as universally
replacing the other. Route preference, fallback order, and universal
requiredness remain `[VERIFY]`. A prototype-era `clerk.suno.com` client-suffix
route is not part of the current observed flow.

### 3.2 Captured Google web flow (request evidence only)

The 2026-09-22 sanitized recon directly supports this sequence of **requests**.
It has no response statuses or bodies; none are inferred below.

| Order | Method | Host and path | Captured query-key names | Evidence |
|---:|---|---|---|---|
| 1 | POST | `auth.suno.com/v1/client/sign_ins` | None captured in this request record | `[T1]` request-only recon |
| 2 | GET | `auth.suno.com/social/login/google-oauth2/` | `next`, `__client` | `[T1]` request-only recon |
| 3 | GET | `accounts.google.com/o/oauth2/auth` | `client_id`, `redirect_uri`, `state`, `response_type`, `scope`, `prompt` | `[T1]` request-only recon |
| 4 | GET | `accounts.google.com/v3/signin/accountchooser` | `client_id`, `prompt`, `redirect_uri`, `response_type`, `scope`, `state`, plus Google-internal keys | `[T1]` request-only recon |
| 5 | GET | `accounts.google.com/signin/oauth/consent` | `authuser`, `client_id`, `state`, plus Google-internal keys | `[T1]` request-only recon |
| 6 | GET | `auth.suno.com/social/complete/google-oauth2/` | `state`, `iss`, `code`, `scope`, `authuser`, `prompt` | `[T1]` request-only recon |
| 7 | GET | `suno.com/create` | `signup_source`, `referrer`, `pre_signup_origin`, `pre_signup_external_referrer`, `redirected_from` | `[T1]` request-only recon |

The separate 2026-08-25 Burp auth capture also observed
`GET /v1/client/handshake`, `GET /v1/client`, and the touch flow. The
2026-09-24 export re-observed `GET /v1/client`, `tokens`, and `touch`, but did
not exercise Google sign-in or any callback. Session-token calls do not prove a
provider redirect contract, and the handshake must not be assumed to have
appeared in the 2026-09-22 sequence merely because both captures concern auth.

### 3.3 Native desktop callback gate

**Native Google sign-in remains disabled and capture-gated.** Every reviewed
callback and final redirect target is Suno-owned HTTPS. The provider
authorization hop is Google-owned HTTPS. There is no reviewed proof that Clerk
accepts a loopback redirect, desktop custom scheme, or other native callback.

Before implementation, a human capture must prove all of the following:

- Clerk is configured to accept the proposed native redirect.
- The provider sends the callback to that redirect rather than only to
  `https://auth.suno.com/social/complete/google-oauth2/`.
- The callback and handshake can be completed without scraping browser
  cookies from an unrelated web session.
- The final session can be persisted securely and refreshed through one of the
  directly captured `GET /v1/client` + `touch`/`tokens` flows without relying
  on an unrelated browser cookie jar.

Do not enable native login, hand-roll a Clerk handshake, or guess a
`localhost`/custom-scheme target. A Suno-owned HTTPS callback in a capture is
not a loopback/custom-scheme callback.

### 3.4 Auth and OAuth route catalog

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/v1/client` | Clerk cookies | Fetch client/session state and initial bearer | `[T1]` Burp 2026-08-25 and 2026-09-24 |
| POST | `/v1/client/sessions/{sid}/touch` | Clerk cookies | Touch active session and obtain a fresh bearer from client/session envelopes | `[T1]` Burp 2026-08-25 and 2026-09-24; request variants coexist |
| POST | `/v1/client/sessions/{sid}/tokens` | Clerk cookies | Mint/return a session bearer as top-level `jwt` | `[T1]` Burp 2026-09-24 |
| POST | `/v1/client/sign_ins` | Clerk/browser context | Begin Clerk sign-in attempt | `[T1]` request-only recon 2026-09-22; form flow also in Burp 2026-08-25 |
| GET | `/social/login/google-oauth2/` | Browser/Clerk context | Provider redirect initiation | `[T1]` request-only recon 2026-09-22 |
| GET | `/social/complete/google-oauth2/` | Provider callback context | Suno-owned Google callback | `[T1]` request-only recon 2026-09-22 |
| GET | `/v1/client/handshake` | Clerk cookies | Browser cookie/session handshake | `[T1]` Burp 2026-08-25 |
| `GET?` or `POST?` | `/v1/client/verify` | Clerk cookies? | Method and purpose conflict; do not use as generic session verification | `[VERIFY]` |
| GET | `/v1/environment` | Clerk/public instance context | Clerk instance/environment configuration | `[T1]` Burp 2026-08-25 |
| GET? | `/v1/client/sync` | Clerk cookies? | Claimed client-state synchronization | `[LEAD]` old auth prose/scan |
| GET? or POST? | `/v1/event` | Clerk cookies? | Claimed client telemetry/event surface | `[LEAD]` old auth prose/scan |
| GET? | `/v1/logs` | Clerk cookies? | Claimed client log retrieval | `[LEAD]` old auth prose/scan |
| POST? | `/v1/tickets/accept` | Clerk cookies? | Claimed ticket/policy acceptance | `[LEAD]` old auth prose/scan |
| `GET?` or `POST?` | `/v1/verify` | Clerk cookies? | Method and semantics conflict; not observed | `[VERIFY]` |
| `GET?` or `POST?` with `?_method=PATCH` | `/v1/client` | Clerk cookies? | Claimed client mutation via method override; transport method not captured | `[VERIFY]` |
| GET? | `/sso-callback` | Web session | Frontend SSO completion target seen in reconstructed sign-in data | `[LEAD]` |
| GET? | `/oauth-redirect` | Web session | Generic OAuth redirect page | `[LEAD]` scan |
| GET? | `/oauth-redirect-custom` | Web session | Custom OAuth redirect page | `[LEAD]` scan |
| GET? | `/oauth-redirect-staff` | Web session/admin? | Staff OAuth redirect page | `[LEAD]` scan |
| GET? | `/oauth-redirect-v2` | Web session | OAuth redirect v2 page | `[LEAD]` scan |
| GET? | `/link-account` | Clerk session | Account-linking page | `[LEAD]` scan |
| GET? | `/auth/session-recovery` | Clerk context | Session-recovery page | `[LEAD]` scan |
| GET? | `/auth/birthday` | Clerk session | Birthday/age gate page | `[LEAD]` scan |
| GET? | `/auth/error` | Clerk context | Auth error page | `[LEAD]` scan |
| GET? | `/auth/verify` | Clerk context | Auth verification page | `[LEAD]` scan |

## 4. Complete endpoint catalog

### 4.1 Billing and account commerce

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/api/billing/info/` | Bearer | Current billing/credit/account information | `[T1]` Burp 2026-08-25 |
| GET | `/api/billing/usage-plans` | Bearer | Available usage-plan list | `[T1]` Burp 2026-08-25 |
| GET | `/api/billing/usage-plan-descriptions/` | Bearer | Plan descriptions | `[T1]` Burp 2026-08-25 |
| GET | `/api/billing/usage-plan-faq/` | Bearer | Plan FAQ content | `[T1]` Burp 2026-08-25 |
| GET | `/api/billing/usage-plan-web-table-comparison/` | Bearer | Web plan-comparison data | `[T1]` Burp 2026-08-25 |
| GET | `/api/billing/eligible-discounts` | Bearer | Account-eligible discounts | `[T1]` Burp 2026-08-25 |
| GET | `/api/billing/conversion-tracking` | Bearer | Conversion-tracking surface | `[T1]` Burp 2026-08-25 |
| POST | `/api/billing/auto-reload/nudge-check` | Bearer | Auto-reload nudge check | `[T1]` Burp 2026-08-25 |
| GET? | `/api/billing/default-currency` | Bearer? | Default/account currency | `[LEAD]` old inventory/raw scan |
| GET? | `/api/billing/get-discount-offer` | Bearer? | Personalized discount offer | `[LEAD]` old inventory/scan |
| GET? | `/api/billing/get-churn-survey-options` | Bearer? | Cancellation survey options | `[LEAD]` old inventory/scan |
| GET? | `/api/billing/tax-info` | Bearer? | Tax metadata | `[LEAD]` old inventory/scan |
| GET? | `/api/billing/change-plan/preview/` | Bearer? | Plan-change preview | `[LEAD]` old inventory/scan |
| GET? | `/api/billing/purchase-info/{purchase_id}/` | Bearer? | Purchase/checkout information | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/create-session/` | Bearer? | Create checkout/subscription session | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/create-portal/` | Bearer? | Open customer billing portal | `[LEAD]` raw scan |
| POST? | `/api/billing/change-plan/` | Bearer? | Change subscription plan | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/cancel-sub/` | Bearer? | Cancel subscription | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/cancel-sub/undo/` | Bearer? | Undo pending cancellation | `[LEAD]` raw scan |
| POST? | `/api/billing/pause-sub/` | Bearer? | Pause subscription | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/unpause-sub/` | Bearer? | Resume subscription | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/submit-survey/` | Bearer? | Submit churn/cancellation survey | `[LEAD]` old inventory/topic prose |
| POST? | `/api/billing/accept-sub-coupon/` | Bearer? | Accept/apply subscription coupon | `[LEAD]` raw scan |
| POST? | `/api/billing/set-default-payment-method/` | Bearer? | Set default payment method | `[LEAD]` old inventory/topic prose |
| `?` | `/api/billing/auto-reload` | Bearer? | Auto-reload root surface | `[LEAD]` loose normalized scan |
| `?` | `/api/billing/auto-reload/enable` | Bearer? | Enable auto-reload | `[LEAD]` loose normalized scan |
| `?` | `/api/billing/auto-reload/disable` | Bearer? | Disable auto-reload | `[LEAD]` loose normalized scan |

### 4.2 User, backend session, onboarding, and account catalog

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/api/session/` | Bearer | Bootstrap user/session plus runtime model catalog | `[T1]` Burp 2026-08-25 |
| GET | `/api/user/metadata` | Bearer | User metadata/plan summary | `[T1]` Burp 2026-08-25 |
| GET | `/api/user/get_user_session_id/` | Bearer | Suno-side session identifier | `[T1]` Burp 2026-08-25 |
| GET | `/api/user/tos_acceptance` | Bearer | Terms-acceptance state | `[T1]` Burp 2026-08-25 |
| POST | `/api/user/user_config/` | Bearer | Read current config using an empty JSON object | `[T1]` Burp 2026-08-25 |
| GET | `/api/auth/verify-token` | Bearer? | Backend token verification | `[LEAD]` old inventory/raw scan |
| GET? | `/api/user/me` | Bearer? | Current user profile | `[LEAD]` old inventory/scan |
| POST? | `/api/user/update_user_config/` | Bearer? | Update user config | `[LEAD]` old inventory/topic prose; mutation schema unproved |
| POST? | `/api/user/reset_onboarding/` | Bearer? | Reset onboarding | `[LEAD]` old inventory/topic prose |
| POST? | `/api/user/accept_timbaland_terms/` | Bearer? | Accept feature-specific terms | `[LEAD]` old inventory/topic prose |
| DELETE? | `/api/user/delete-account/` | Bearer? | Destructive account deletion | `[LEAD]` old inventory/topic prose; do not call accidentally |
| `?` | `/api/user/vip_program_acceptance` | Bearer? | VIP-program acceptance surface | `[LEAD]` loose normalized scan |
| `?` | `/api/signout/` | Bearer?/session? | Sign-out surface | `[LEAD]` raw/loose scan |
| GET | `/api/onboarding/current` | Bearer | Current onboarding state | `[T1]` Burp 2026-08-25 |
| POST | `/api/onboarding/start` | Bearer | Start onboarding flow | `[T1]` Burp 2026-08-25 |
| `?` | `/api/onboarding/submit` | Bearer? | Onboarding submission | `[LEAD]` loose normalized scan |
| `?` | `/api/onboarding/complete` | Bearer? | Onboarding completion | `[LEAD]` loose normalized scan |
| `?` | `/api/onboarding/skip` | Bearer? | Onboarding skip | `[LEAD]` loose normalized scan |
| `?` | `/api/onboarding/back` | Bearer? | Onboarding back navigation/state | `[LEAD]` loose normalized scan |
| `?` | `/api/onboarding/audio-upload/abort` | Bearer? | Abort onboarding upload | `[LEAD]` loose normalized scan |
| `?` | `/api/onboarding/audio-upload/remove` | Bearer? | Remove onboarding upload | `[LEAD]` loose normalized scan |
| GET? | `/api/me/` | Bearer? | Profile v1 collection | `[LEAD]` old inventory |
| GET? | `/api/me/v2` | Bearer? | Profile v2 | `[LEAD]` old inventory |
| GET? | `/api/me/v2/cover-art` | Bearer? | User cover-art surface | `[LEAD]` old inventory |
| GET? | `/api/me/v2/history` | Bearer? | User history | `[LEAD]` old inventory |
| GET? | `/api/me/v2/hooks` | Bearer? | User hooks | `[LEAD]` old inventory |
| GET? | `/api/me/v2/personas` | Bearer? | User personas v2 | `[LEAD]` old inventory |
| GET? | `/api/me/v2/playlists` | Bearer? | User playlists v2 | `[LEAD]` old inventory |
| GET? | `/api/me/v2/studio-projects` | Bearer? | User Studio projects v2 | `[LEAD]` old inventory |
| GET? | `/api/me/v2/trash` | Bearer? | User trash v2 | `[LEAD]` old inventory |
| GET? | `/api/me/v2/workspaces` | Bearer? | User workspaces v2 | `[LEAD]` old inventory |
| GET? | `/api/me/cover-art` | Bearer? | User cover-art v1 | `[LEAD]` old inventory |
| GET? | `/api/me/external_accounts` | Bearer? | Linked external accounts | `[LEAD]` old inventory |
| GET? | `/api/me/followers` | Bearer? | Followers | `[LEAD]` old inventory |
| GET? | `/api/me/following` | Bearer? | Following | `[LEAD]` old inventory |
| GET? | `/api/me/history` | Bearer? | History v1 | `[LEAD]` old inventory |
| GET? | `/api/me/hooks` | Bearer? | Hooks v1 | `[LEAD]` old inventory |
| GET? | `/api/me/liked-hooks` | Bearer? | Liked hooks | `[LEAD]` old inventory |
| GET? | `/api/me/liked-playlists` | Bearer? | Liked playlists | `[LEAD]` old inventory |
| GET? | `/api/me/lyrics` | Bearer? | User lyrics | `[LEAD]` old inventory |
| GET? | `/api/me/organization_invitations` | Bearer? | Organization invitations | `[LEAD]` old inventory |
| GET? | `/api/me/organization_memberships` | Bearer? | Organization memberships | `[LEAD]` old inventory |
| GET? | `/api/me/organization_suggestions` | Bearer? | Organization suggestions | `[LEAD]` old inventory |
| GET? | `/api/me/passkeys` | Bearer? | Passkeys | `[LEAD]` old inventory |
| GET? | `/api/me/personas` | Bearer? | Personas v1 | `[LEAD]` old inventory |
| GET? | `/api/me/playlists` | Bearer? | Playlists v1 | `[LEAD]` old inventory |
| GET? | `/api/me/sessions` | Bearer? | Sessions | `[LEAD]` old inventory |
| GET? | `/api/me/sessions/active` | Bearer? | Active sessions | `[LEAD]` old inventory |
| GET? | `/api/me/styles` | Bearer? | User styles | `[LEAD]` old inventory |
| GET? | `/api/me/totp` | Bearer? | TOTP settings | `[LEAD]` old inventory |
| GET? | `/api/me/totp/attempt_verification` | Bearer? | TOTP verification attempt | `[LEAD]` old inventory; mutation-by-GET is not trusted |
| GET? | `/api/me/trash` | Bearer? | Trash v1 | `[LEAD]` old inventory |
| GET? | `/api/me/workspaces` | Bearer? | Workspaces v1 | `[LEAD]` old inventory |
| GET? | `/api/me/studio-projects` | Bearer? | Studio projects v1 | `[LEAD]` old inventory |

### 4.3 Projects and Studio

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/api/project/me` | Bearer | Workspace/project listing | `[T1]` Burp 2026-08-25 |
| GET | `/api/project/default` | Bearer | Default workspace and project clips | `[T1]` Burp 2026-08-25 |
| GET | `/api/project/{project_id}` | Bearer | Project detail | `[T1]` HAR 2026-09-23 |
| GET | `/api/project/default/pinned-clips` | Bearer | Pinned default-workspace clips | `[T1]` HAR 2026-09-23 |
| GET? | `/api/project` | Bearer? | Project listing/creation-family surface | `[LEAD]` old inventory/raw scan |
| POST? | `/api/project` | Bearer? | Create project | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/trash` | Bearer? | Trashed projects | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/invites` | Bearer? | Project invitations | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/clips` | Bearer? | Project clips | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/metadata` | Bearer? | Project metadata | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/pinned-clips` | Bearer? | Project pinned clips | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/collaborators` | Bearer? | Collaborator list | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/collaborators/me` | Bearer? | Current user's collaborator state | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/ably-token` | Bearer? | Realtime collaboration token | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/{project_id}/ably-client-id` | Bearer? | Realtime client identifier | `[LEAD]` old inventory/topic prose |
| POST? | `/api/project/{project_id}/invite` | Bearer? | Invite collaborator | `[LEAD]` old inventory/topic prose |
| POST? | `/api/project/{project_id}/ably-update` | Bearer? | Publish collaboration update | `[LEAD]` old inventory/topic prose |
| GET? | `/api/project/feed` | Bearer? | Project feed | `[LEAD]` loose normalized scan |
| GET? | `/api/project/library/images` | Bearer? | Project image library | `[LEAD]` loose normalized scan |
| GET? | `/api/project/library/videos` | Bearer? | Project video library | `[LEAD]` loose normalized scan |
| POST? | `/api/studio/create-project` | Bearer?/entitlement? | Create Studio project | `[LEAD]` old inventory/raw scan |
| POST? | `/api/studio/save-project` | Bearer?/entitlement? | Save Studio project state | `[LEAD]` old inventory/topic prose |
| GET? | `/api/studio/render-state` | Bearer?/entitlement? | Studio render state | `[LEAD]` old inventory/raw scan |
| GET? | `/api/studio/render-state-multitrack` | Bearer?/entitlement? | Multitrack render state | `[LEAD]` old inventory/raw scan |
| GET? | `/api/studio/project-version/{id}` | Bearer?/entitlement? | Studio project version | `[LEAD]` old topic prose |
| `?` | `/api/studio/` | Web session? | Claimed Studio access/page surface on API host | `[VERIFY]`; may be a page route, not a stable API call |
| `?` | `/api/studio/{slug}` | Web session? | Claimed Studio-by-slug surface | `[VERIFY]`; colon-form and slash-form scan artifacts are not canonical |

### 4.4 Generation, lyrics, prompts, and analysis

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| POST | `/api/c/check` | Bearer | Captcha requirement check for generation | `[T1]` Burp 2026-08-25 |
| POST | `/api/generate/v2-web/` | Bearer | Captured web music-generation submission | `[T1]` Burp 2026-08-25 |
| POST | `/api/generate/cowrite-lyrics/` | Bearer | Single-shot lyrics co-writing/editing | `[T1]` Burp 2026-08-25 |
| GET | `/api/lyricists` | Bearer | Lyricist/persona selection list | `[T1]` Burp 2026-08-25 |
| GET | `/api/lyrics-projects` | Bearer | List/read lyrics projects | `[T1]` Burp 2026-08-25 |
| POST | `/api/lyrics-projects` | Bearer | Create/update lyrics-project surface | `[T1]` Burp 2026-08-25; exact mutation schema not canonicalized here |
| POST | `/api/lyrics-projects/{id}/flush` | Bearer | Persist/finalize lyrics-project state | `[T1]` Burp 2026-08-25 |
| GET | `/api/prompts/v2` | Bearer | Prompt/style data | `[T1]` Burp 2026-08-25 |
| POST | `/api/prompts/v2` | Bearer | Prompt/style mutation surface | `[T1]` Burp 2026-08-25; exact mutation schema not canonicalized here |
| GET | `/api/prompts/suggestions` | Bearer | Prompt suggestions | `[T1]` Burp 2026-08-25 |
| POST | `/api/prompts/upsample` | Bearer | Prompt/tag upsampling | `[T1]` Burp 2026-08-25 |
| GET | `/api/gen/{id}/aligned_lyrics/v2/` | Bearer | Timed word alignment | `[T1]` Burp 2026-08-25 |
| POST | `/api/gen/{id}/downbeats_streaming/v2` | Bearer | Complete downbeat analysis response | `[T1]` Burp 2026-08-25 |
| GET | `/api/gen/{id}/waveform-aggregates` | Bearer | Multi-resolution waveform aggregates | `[T1]` Burp 2026-08-25 |
| POST | `/api/gen/{id}/increment_play_count/v2` | Bearer | Play-count telemetry | `[T1]` Burp 2026-08-25 |
| POST | `/api/gen/{id}/listen_milestone` | Bearer | Listen-milestone telemetry | `[T1]` Burp 2026-08-25 |
| GET | `/api/gen/{id}/comments` | Bearer | Generation/clip comments | `[T1]` Burp 2026-08-25 |
| POST? | `/api/generate/v2/` | Bearer? | Non-web generation variant | `[LEAD]` old inventory/topic prose |
| POST? | `/api/generate/cowrite-lyrics/models/` | Bearer? | Co-writing model discovery | `[LEAD]` loose normalized scan |
| POST? | `/api/generate/lyrics/` | Bearer? | Start lyrics-generation job | `[LEAD]` old topic prose; not exercised in reviewed capture |
| GET? | `/api/generate/lyrics/{id}` | Bearer? | Poll lyrics-generation job | `[LEAD]` old topic prose; not exercised in reviewed capture |
| POST? | `/api/generate/lyrics-infill` | Bearer? | Lyrics infill, no trailing slash | `[LEAD]` raw scan |
| POST? | `/api/generate/lyrics-infill/` | Bearer? | Lyrics infill, trailing slash | `[LEAD]` raw scan; do not alias automatically |
| POST? | `/api/generate/lyrics-mashup` | Bearer? | Lyrics mashup | `[LEAD]` raw scan/topic prose |
| POST? | `/api/generate/lyrics-pair` | Bearer? | Paired lyrics | `[LEAD]` raw scan/topic prose |
| POST? | `/api/generate/lyrics-pair/rate` | Bearer? | Rate lyrics pair | `[LEAD]` raw scan/topic prose |
| POST? | `/api/generate/concat/v2/` | Bearer? | Concatenate generated segments | `[LEAD]` raw scan; not exercised in reviewed capture |
| POST? | `/api/generate/merge/` | Bearer? | Merge generated segments | `[LEAD]` raw scan/topic prose |
| POST? | `/api/extend_audio` | Bearer? | Alternate extension path | `[LEAD]` old topic prose |
| POST? | `/api/upload-and-cover/` | Bearer? | Upload-and-cover path | `[LEAD]` old topic prose; multipart fields unproved |
| POST? | `/api/generate/upsample` | Bearer? | Audio upsampling | `[LEAD]` raw scan/topic prose |
| POST? | `/api/generate/get_recommend_styles` | Bearer? | Style recommendation surface | `[LEAD]` raw scan/topic prose |
| POST? | `/api/generate/matrix` | Bearer? | Generation matrix | `[LEAD]` raw scan |
| POST? | `/api/generate/sum/` | Bearer? | Generation sum/session surface | `[LEAD]` raw scan/loose normalized scan |
| GET? | `/api/gen/{id}` | Bearer? | Claimed generation-status poll | `[LEAD]`; captured generation flow used feed/get-songs instead |
| GET? | `/api/clip/{id}` | Bearer? | Claimed clip-status/detail poll | `[LEAD]` old inventory/topic prose |
| POST? | `/api/gen/{id}/convert_wav/` | Bearer? | Convert generation/clip to WAV | `[LEAD]` current endpoint map/topic prose |
| GET? | `/api/gen/{id}/wav_file/` | Bearer? | WAV artifact retrieval | `[LEAD]` current endpoint map |
| POST? | `/api/gen/bulk_increment_play_counts/v2` | Bearer? | Bulk play-count telemetry | `[LEAD]` old inventory/raw scan |
| POST? | `/api/gen/increment_action_counts/` | Bearer? | Bulk action-count telemetry | `[LEAD]` old inventory/raw scan |
| POST? | `/api/gen/prompt_image/` | Bearer? | Prompt-image job | `[LEAD]` old inventory/topic prose |
| POST? | `/api/gen/trash` | Bearer? | Trash generated items | `[LEAD]` old inventory/topic prose |
| POST? | `/api/gen/set_metadata/` | Bearer? | Set generation metadata | `[LEAD]` old inventory |
| GET? | `/api/prompts/` | Bearer? | Legacy prompt collection | `[LEAD]` old inventory/current endpoint map |
| POST? | `/api/prompts/delete/` | Bearer? | Delete prompt | `[LEAD]` raw scan |
| POST? | `/api/generate/stems` | Bearer? | Stem-generation surface | `[LEAD]` old upload topic prose |
| GET? | `/api/instruments` | Bearer? | Instrument selection surface | `[LEAD]` loose normalized scan |
| `?` | `/api/instrument/describe-doodle` | Bearer? | Instrument description surface | `[LEAD]` loose normalized scan |
| POST? | `/api/lyricists` | Bearer? | Lyricist creation surface | `[LEAD]`; only `GET /api/lyricists` is captured |

### 4.5 Library, feed, search, and playlists

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| POST | `/api/feed/v3` | Bearer | Primary cursor-based library feed | `[T1]` Burp 2026-08-25 |
| POST | `/api/unified/feed` | Bearer | Profile-style unified feed | `[T1]` Burp 2026-08-25 |
| POST | `/api/unified/homepage` | Bearer | Homepage/unified feed | `[T1]` corrective endpoint map/Burp evidence |
| GET | `/api/clips/get_songs_by_ids` | Bearer | Bulk clip/song hydration by repeated `ids` query values | `[T1]` Burp 2026-08-25 |
| GET | `/api/playlist/me` | Bearer | Current user's playlists | `[T1]` Burp 2026-08-25 |
| GET? | `/api/feed/` | Bearer? | Legacy feed | `[LEAD]` old library prose |
| GET? | `/api/feed/v2` | Bearer? | Legacy v2 feed | `[LEAD]` old library prose |
| GET? | `/api/feed/v3/offset` | Bearer? | Offset feed variant | `[LEAD]` old inventory/raw scan |
| POST | `/api/unified/explore` | Bearer | Cursor-based explore feed | `[T1]` Burp 2026-09-24 |
| GET? | `/api/unified/homepage/explore` | Bearer? | Explore homepage feed | `[LEAD]` old library prose |
| GET? | `/api/unified/homepage/explore/mobile` | Bearer? | Mobile explore feed | `[LEAD]` old library prose |
| GET? | `/api/unified/search/omnisearch` | Bearer? | Unified search | `[LEAD]` old library/loose scan |
| GET? | `/api/unified/search/suggest` | Bearer? | Unified search suggestions | `[LEAD]` loose normalized scan |
| GET? | `/api/unified/search/suggest/history` | Bearer? | Search-suggestion history | `[LEAD]` loose normalized scan |
| GET? | `/api/search/` | Bearer? | General search | `[LEAD]` old inventory/raw scan |
| GET? | `/api/search/history` | Bearer? | Search history | `[LEAD]` old library/loose scan |
| GET? | `/api/search/users` | Bearer? | User search | `[LEAD]` old inventory/raw scan |
| GET? | `/api/discover/shortcuts_songs` | Bearer? | Discover shortcut songs | `[LEAD]` old inventory |
| GET? | `/api/trending/metaplaylist/` | Bearer? | Trending metaplaylist | `[LEAD]` old inventory/raw scan |
| GET? | `/api/tags/recommend` | Bearer? | Recommended tags | `[LEAD]` old inventory/topic prose |
| POST? | `/api/recommend/hide-creator` | Bearer? | Hide creator from recommendations | `[LEAD]` old inventory/social prose |
| POST? | `/api/playlist/create/` | Bearer? | Create playlist | `[LEAD]` old inventory/topic prose |
| POST? | `/api/playlist/set_metadata` | Bearer? | Set playlist metadata | `[LEAD]` old inventory/topic prose |
| POST? | `/api/playlist/trash/` | Bearer? | Trash playlist | `[LEAD]` old inventory/topic prose |
| POST? | `/api/playlist/update_clips/` | Bearer? | Add/remove/reorder playlist clips | `[LEAD]` old inventory/topic prose |
| GET? | `/api/playlist/{playlist_id}/` | Bearer? | Playlist detail | `[LEAD]` old library/topic prose |
| GET? | `/api/playlist/{playlist_id}/tracks` | Bearer? | Playlist tracks | `[LEAD]` old library/topic prose |

No reviewed capture establishes server-side search as a `searchText` field on
`POST /api/feed/v3`. Search claims derived from old prose or client parameters
remain `[LEAD]`; clients must use local filtering until a direct request/response
capture proves the remote search contract.

### 4.6 Clips, media, download, upload, and processing

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/api/clips/{clip_id}/attribution` | Bearer | Clip attribution/rights metadata | `[T1]` Burp 2026-08-25 |
| GET | `/api/clips/parent` | Bearer | Parent clip relation | `[T1]` Burp 2026-08-25 |
| GET | `/api/clips/remixes` | Bearer | Clip remixes | `[T1]` Burp 2026-08-25 |
| GET | `/api/clips/remixes/count` | Bearer | Remix count | `[T1]` Burp 2026-08-25 |
| GET | `/api/clips/get_similar/` | Bearer | Similar clips | `[T1]` Burp 2026-08-25 |
| POST | `/api/mango/rights` | Bearer | Rights/crypto response surface | `[T1]` Burp 2026-08-25; payload is sensitive and not reproduced here |
| GET | `/api/video/generate/{clip_id}/status/` | Bearer | Video-render status | `[T1]` Burp 2026-08-25 |
| POST | `/api/video_gen/pending_batches` | Bearer | Pending video batch identifiers | `[T1]` Burp 2026-08-25 |
| GET? | `/api/clip/{clip_id}` | Bearer? | Clip detail | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/adjust-speed/` | Bearer? | Adjust playback speed | `[LEAD]` old inventory/topic prose; mutation-by-GET is not trusted |
| GET? | `/api/clips/aligned_clips` | Bearer? | Aligned clips | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/aligned_clip_siblings` | Bearer? | Aligned sibling clips | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/autoplay/` | Bearer? | Autoplay state/surface | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/delete/` | Bearer? | Delete surface | `[LEAD]` old inventory/topic prose; destructive and method-unproved |
| GET? | `/api/clips/direct_children` | Bearer? | Direct child clips | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/direct_children_by_user/` | Bearer? | User-scoped direct children | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/direct_children_count` | Bearer? | Direct-child count | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/displayable_remixes` | Bearer? | Displayable remixes | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/displayable_remixes_by_user/` | Bearer? | User-scoped displayable remixes | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/displayable_remixes_count` | Bearer? | Displayable-remix count | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/displayable_user_remixes_for_clip/` | Bearer? | Displayable user remixes for a clip | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/user_remixes_for_clip/` | Bearer? | User remixes for a clip | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clips/clip_roots` | Bearer? | Clip roots | `[LEAD]` loose normalized scan |
| POST? | `/api/clips/reverse-clip/` | Bearer? | Reverse clip | `[LEAD]` loose normalized scan |
| GET? | `/api/clip/{clip_id}/stems` | Bearer? | Stem metadata | `[LEAD]` old inventory/topic prose |
| GET? | `/api/clip/{clip_id}/stems/pages` | Bearer? | Paged stem data | `[LEAD]` old inventory/topic prose |
| POST? | `/api/edit/crop/{clip_id}/` | Bearer? | Crop clip | `[LEAD]` old library/topic prose |
| POST? | `/api/edit/stems/{clip_id}/` | Bearer? | Edit stems | `[LEAD]` old library/topic prose |
| GET? | `/api/billing/clips/{clip_id}/download/` | Bearer? | Billing-gated clip download | `[LEAD]` old inventory/topic prose; no download schema canonicalized |
| `?` | `/api/download/clips/zip/prepare` | Bearer? | Prepare clip ZIP download | `[VERIFY]` method/payload not captured |
| `?` | `/api/openai-speech/` | Bearer? | Claimed speech compatibility surface | `[VERIFY]` GET claim is scan-only; no method/schema contract |
| GET? | `/api/deepgram-token` | Bearer? | Claimed transcription token | `[LEAD]` old inventory/topic prose |
| POST | `/api/uploads/audio/` | Bearer | Initialize an audio upload and return a temporary direct-upload URL/policy | `[T1]` Burp 2026-09-24; JSON request/response |
| POST? | `/api/uploads/audio/{id}/initialize-clip/` | Bearer? | Initialize clip from uploaded audio | `[LEAD]` old inventory/topic prose; not captured in 2026-09-24 export |
| POST | `/api/uploads/audio/{id}/upload-finish/` | Bearer | Finalize a completed direct audio upload | `[T1]` Burp 2026-09-24; JSON request/empty-object response |
| POST | Returned URL on `suno-uploads.s3.amazonaws.com` | Temporary signed multipart fields | Direct-upload captured audio bytes | `[T1]` Burp 2026-09-24; 204 response |
| GET? | `/api/uploads/audio/{id}/` | Bearer? | Audio upload/processing status | `[LEAD]` old inventory/topic prose |
| POST? | `/api/uploads/audio/{id}/convert_wav/` | Bearer? | Convert uploaded audio to WAV | `[LEAD]` old upload topic prose |
| POST? | `/api/uploads/image` | Bearer? | Image upload, no trailing slash | `[LEAD]` raw scan; multipart fields unproved |
| POST? | `/api/uploads/image/` | Bearer? | Image upload, trailing slash | `[LEAD]` raw scan; do not alias automatically |
| GET? | `/api/uploads/video` | Bearer? | Video upload info, no trailing slash | `[VERIFY]` spelling/method not captured |
| GET? | `/api/uploads/video/` | Bearer? | Video upload info, trailing slash | `[VERIFY]` spelling/method not captured |
| POST? | `/api/uploads/video/{upload_id}/upload-finish/` | Bearer? | Finalize video upload | `[LEAD]` old inventory/topic prose |
| POST? | `/api/video/hooks/create` | Bearer? | Create video hook | `[LEAD]` old inventory/raw scan |
| GET? | `/api/video/hooks/feed` | Bearer? | Video-hook feed | `[LEAD]` old inventory/topic prose |
| GET? | `/api/video/hooks/{hook_id}/flag` | Bearer? | Flag video hook | `[LEAD]` old inventory/topic prose; mutation-by-GET is not trusted |
| `?` | `/api/video/hooks/fetch_hook_lyrics` | Bearer? | Fetch hook lyrics | `[LEAD]` raw scan |
| `?` | `/api/video/hooks/suggested_clips` | Bearer? | Suggested video-hook clips | `[LEAD]` raw scan |
| `?` | `/api/video_gen/poll_batches` | Bearer? | Poll video batches | `[LEAD]` old inventory/loose scan |

### 4.7 Persona, custom models, and voice

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/api/custom-model/pending/` | Bearer | Pending custom-model state | `[T1]` Burp 2026-08-25 |
| POST? | `/api/persona/create/` | Bearer?/entitlement? | Create persona | `[LEAD]` old inventory/topic prose |
| GET? | `/api/persona/get-personas/` | Bearer? | User personas | `[LEAD]` old inventory/topic prose |
| GET? | `/api/persona/get-followed-personas/` | Bearer? | Followed personas | `[LEAD]` old inventory/topic prose |
| GET? | `/api/persona/get-loved-personas/` | Bearer? | Loved personas | `[LEAD]` old inventory/topic prose |
| GET? | `/api/persona/get-persona-paginated/{id}/` | Bearer? | Paginated persona surface | `[LEAD]` old inventory/topic prose |
| POST? | `/api/custom-model/create/` | Bearer?/entitlement? | Create custom model | `[LEAD]` old inventory/topic prose; training schema unproved |
| POST? | `/api/custom-model/archive/` | Bearer?/entitlement? | Archive custom model | `[LEAD]` old inventory/topic prose |
| POST? | `/api/processed_clip/voice-vox-stem` | Bearer? | Voice/stem processing | `[LEAD]` old inventory/topic prose |

No plan matrix, verification requirement, per-account model limit, or voice-clone
availability statement is canonical without a current capture.

### 4.8 Social, sharing, comments, notifications, and rights

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| GET | `/api/profiles/{handle}/info` | Bearer | Public/private profile information | `[T1]` Burp 2026-08-25 |
| GET | `/api/profiles/pinned-clips` | Bearer | Pinned profile clips | `[T1]` Burp 2026-08-25 |
| GET | `/api/notification/v2` | Bearer | Notifications | `[T1]` Burp 2026-08-25 |
| GET | `/api/notification/v2/badge-count` | Bearer | Notification badge count | `[T1]` Burp 2026-08-25 |
| POST | `/api/notification/v2/clear-badge` | Bearer | Clear notification badge | `[T1]` Burp 2026-08-25 |
| GET | `/api/share/stats` | Bearer | Share count/statistics | `[T1]` Burp 2026-08-25 |
| GET? | `/api/profiles/` | Bearer? | Profile listing/search | `[LEAD]` old inventory/raw scan |
| GET? | `/api/profiles/{handle}` | Bearer? | Profile by handle | `[LEAD]` old inventory/social prose |
| GET? | `/api/profiles/follow` | Bearer? | Follow action | `[LEAD]` old inventory/social prose; mutation-by-GET is not trusted |
| GET? | `/api/profiles/mutual-followers` | Bearer? | Mutual followers | `[LEAD]` old inventory/raw scan |
| GET? | `/api/comment/{comment_id}` | Bearer? | Comment detail | `[LEAD]` old inventory/social prose |
| POST? | `/api/comment/block-user` | Bearer? | Block user | `[LEAD]` old inventory/social prose |
| POST? | `/api/comment/unblock-user` | Bearer? | Unblock user | `[LEAD]` old inventory/social prose |
| POST? | `/api/comment/{comment_id}/reaction` | Bearer? | React to comment | `[LEAD]` old inventory/social prose |
| POST? | `/api/comment/{comment_id}/replies` | Bearer? | Reply to comment | `[LEAD]` old inventory/social prose |
| POST? | `/api/comment/{comment_id}/report` | Bearer? | Report comment | `[LEAD]` old inventory/social prose |
| POST | `/api/notification/v2/read` | Bearer | Mark notifications read, optionally before a UTC cutoff | `[T1]` Burp 2026-09-24 |
| GET? | `/api/share/attribute/` | Bearer? | Share attribution | `[LEAD]` old inventory/social prose |
| GET? | `/api/share/event` | Bearer? | Share event | `[LEAD]` old inventory/social prose; mutation-by-GET is not trusted |
| GET? | `/api/share/link` | Bearer? | Share-link surface | `[LEAD]` old inventory/social prose |
| POST | `/api/social/following-feed` | Bearer | Activity feed for followed accounts | `[T1]` Burp 2026-09-24; subsequent-page behavior `[VERIFY]` |
| `GET?` or `POST?` | `/api/song_copy/send-song` | Bearer? | Send/copy song action | `[VERIFY]` direct method conflict |
| GET? | `/api/invite/` | Bearer? | Invitation surface | `[LEAD]` old inventory/raw scan |
| POST? | `/api/survey/survey-responses` | Bearer? | Survey response | `[LEAD]` raw/loose scan |

### 4.9 Feature gates, telemetry, app chrome, and experimental surfaces

| Method | Path | Auth | Purpose | Evidence |
|---|---|---|---|---|
| POST | `/api/statsig/experiment/` | Bearer | Query experiment parameters | `[T1]` Burp 2026-08-25 |
| GET | `/api/statsig/experiment/forked-onboarding` | Bearer | Forked-onboarding experiment state | `[T1]` Burp 2026-08-25 |
| GET | `/api/labs/configs` | Bearer | Labs catalog/configuration | `[T1]` Burp 2026-08-25 |
| `?` | `/api/labs/marketplace` | Bearer? | Historical Marketplace probe path; availability/method unproved | `[VERIFY]`; do not confuse with `/labs/marketplace` page or Marketplace artifacts |
| GET | `/api/modals` | Bearer | App modal catalog | `[T1]` Burp 2026-08-25 |
| GET | `/api/cms/nudges/publish-nudge` | Bearer | Publish nudge state/content | `[T1]` Burp 2026-08-25 |
| GET | `/api/cms/nudges/share-nudge` | Bearer | Share nudge state/content | `[T1]` Burp 2026-08-25 |
| GET | `/api/realtime/discover` | Bearer | Realtime/Ably discovery and token metadata | `[T1]` Burp 2026-08-25 |
| GET | `/api/challenge/progress` | Bearer | Challenge/bonus progress | `[T1]` Burp 2026-08-25 |
| GET | `/api/contests/` | Bearer | Contest catalog | `[T1]` Burp 2026-08-25 |
| GET | `/api/music_player/playbar_state` | Bearer | Read playbar state | `[T1]` Burp 2026-08-25 |
| POST | `/api/music_player/playbar_state` | Bearer | Synchronize playbar state | `[T1]` Burp 2026-08-25 |
| GET | `/api/personalization/memory` | Bearer | Personalization memory/profile | `[T1]` Burp 2026-08-25 |
| GET | `/api/personalization/settings` | Bearer | Personalization settings | `[T1]` Burp 2026-08-25 |
| GET? | `/api/ably/simple-mode-token` | Bearer? | Realtime simple-mode token | `[LEAD]` old inventory/raw scan |
| GET? | `/api/cms` | Bearer? | CMS content surface | `[LEAD]` old inventory/raw scan |
| GET? | `/api/cms/paywall/plan-options` | Bearer? | Paywall plan options | `[LEAD]` loose scan after `/marketplace/` normalization |
| POST? | `/api/moderation/ack-copyright-warning` | Bearer? | Acknowledge copyright warning | `[LEAD]` old inventory/raw scan |
| GET? | `/api/preferences/clip-review/pending` | Bearer? | Clip-review preference state | `[LEAD]` old inventory/raw scan |
| POST? | `/api/preferences/clip-review/submit` | Bearer? | Submit clip review | `[LEAD]` old inventory/raw scan |
| POST? | `/api/preferences/clip-review/opt-out` | Bearer? | Opt out of clip review | `[LEAD]` old inventory/raw scan |
| POST? | `/api/labs/verse/messages/` | Bearer? | Labs Verse messages | `[LEAD]` raw scan |

Observed flag/cache names are not server contracts. The following remain
`[LEAD]` names from scans, not an authorization mechanism or entitlement
source:

- `orpheus_is_enabled`, `orpheus_is_auto_mode`,
  `orpheus_is_canvas_enabled`, `orpheus_default_to_chat`,
  `orpheus_mobile_web_enabled`, `orpheus_group`
- `marketplace_enabled`, `marketplace_access`, `labs_marketplace`
- `gen-video-covers`, `labs_*`
- `hide-credits-enabled`, `hide-credits-for-subscribers-enabled`,
  `out-of-credits-banner*`, `free_*`, `can_buy_credit_top_up`
- `bypass_hook_feed_caches`, `bypass_unified_feed_caches`
- `statsig.cached.evaluations.*`

Forcing a client-side flag is not proof that the backend has enabled a route,
plan, model, or entitlement.

#### B-Side and Labs page-route leads

These are frontend page routes found in scans, not proven API contracts. All
are `[LEAD]`; method is normally browser navigation (`GET`) and access may
require a web session, entitlement, or internal role. Do not infer an
underlying `/api/...` path from a page route.

| Family | Paths preserved from the corpus |
|---|---|
| B-Side index/exploration | `/b-side`, `/b-side/explore`, `/b-side/nux`, `/b-side/labs-control`, `/b-side/personalization` |
| Account/moderation/admin | `/b-side/account-moderation`, `/b-side/contests`, `/b-side/impersonate`, `/b-side/song-moderation`, `/b-side/trending-moderation`, `/b-side/user-activity` |
| Audio/DSP/evaluation | `/b-side/audible-magic`, `/b-side/cover-art-eval`, `/b-side/describe-clip`, `/b-side/dsp-diag`, `/b-side/dsp-engine-talk`, `/b-side/lyrics-eval`, `/b-side/lyrics-eval-reports/{slug*}`, `/b-side/lyrics-eval/{slug*}`, `/b-side/stem-extract-test` |
| Creation/lyrics | `/b-side/hook-song-gen`, `/b-side/hooks-explorer/{slug*}`, `/b-side/lyrics-gen`, `/b-side/lyrics-viewer`, `/b-side/simple-remix/{slug*}` |
| Recommendations/search | `/b-side/because-you-like`, `/b-side/isthisus`, `/b-side/search-lens/*`, `/b-side/music-soulmate`, `/b-side/music-soulmate-talk`, `/b-side/user-mix`, `/b-side/user-similarity` |
| Search-lens subroutes | `/b-side/search-lens/crate`, `/b-side/search-lens/crate/browse`, `/b-side/search-lens/crate/listening-room`, `/b-side/search-lens/crate/listening-room/mock-needle`, `/b-side/search-lens/crate/similar`, `/b-side/search-lens/crate/workbench`, `/b-side/search-lens/playground` |
| Projects/Studio | `/b-side/agentic-transcript`, `/b-side/agentic-transcript/{clipId}`, `/b-side/playlist-copier`, `/b-side/project-state-tour`, `/b-side/studio-access` |
| Commerce/account experiments | `/b-side/billing/revcat`, `/b-side/vip` |
| Visual/video/voice | `/b-side/video-gen`, `/b-side/visual-art`, `/b-side/visual-art/{type}/{id}`, `/b-side/voice-verification` |
| Misc experiments | `/b-side/api-explorer`, `/b-side/banner`, `/b-side/bucket-viewer`, `/b-side/creators`, `/b-side/feature-flags`, `/b-side/hipster`, `/b-side/milo`, `/b-side/music-video`, `/b-side/onboarding-survey`, `/b-side/on-repeat`, `/b-side/orpheus`, `/b-side/reward-model`, `/b-side/sse-demo`, `/b-side/style-synth`, `/b-side/sunshine-list` |
| Labs pages | `/labs/canvas`, `/labs/divisi`, `/labs/genre-wheel`, `/labs/listen-and-rank`, `/labs/live-radio`, `/labs/marketplace`, `/labs/milo`, `/labs/pedalboard`, `/labs/splashpad`, `/labs/suno-jr`, `/labs/suno-jr/beats`, `/labs/suno-jr/divvy`, `/labs/suno-jr/lullaby`, `/labs/suno-jr/playlists`, `/labs/suno-jr/visuals`, `/labs/suno-mania`, `/labs/turntable`, `/labs/turntable/{roomId}`, `/labs/verse` |

### 4.10 Orpheus leads

No Orpheus request/response contract is canonical. Every row below is research
material only.

| Method | Path on claimed Modal host | Auth | Claimed purpose | Evidence |
|---|---|---|---|---|
| `?` | `/session-history` | Unconfirmed | Session history | `[LEAD]` old B-Side prose |
| `?` | `/v1/orchestrator/chat` | Unconfirmed | Orchestrator chat | `[LEAD]` current endpoint constants; no captured method/schema |
| `?` | `/v1/orchestrator/history` | Unconfirmed | Orchestrator history | `[LEAD]` current endpoint constants; no captured method/schema |
| POST? | `/api/v1/chat/completions` | Unconfirmed | Claimed OpenAI-compatible completion | `[VERIFY]` old inventory claim only |
| GET? | `/api/v1/models` | Unconfirmed | Claimed model listing | `[VERIFY]` old inventory claim only |

The names `orpheus-0.1` through `orpheus-0.5`, “OpenAI-compatible,” chat-driven
generation, auto mode, and canvas semantics are `[LEAD]` claims. They are not
a supported API, a verified model catalog, or a client contract.

## 5. Capture-backed contracts

### 5.1 Clerk client and session-token contracts

**Initial bearer — `[T1]`**

```http
GET /v1/client
```

The 2026-08-25 variant appended `__clerk_api_version` and
`_clerk_js_version` query keys; the 2026-09-24 request did not. Treat query
requiredness as point-in-time rather than synthesizing one request for both.

Relevant captured response structure, with values omitted:

```text
response.last_active_session_id
response.sessions[].id
response.sessions[].last_active_token.jwt
```

Select the session matching `last_active_session_id`. A session-array index is
not a stable selector.

**Session touch — `[T1]`, with point-in-time request variants**

```http
POST /v1/client/sessions/{sid}/touch
Content-Type: application/x-www-form-urlencoded

intent=focus
```

The 2026-09-24 request had no query keys and the form body above; its 200
response contained both `client` and `response` session envelopes. The
2026-08-25 capture used version query keys and an empty form body. Defensive
parsing may accept both envelope shapes, but requiredness and preference remain
`[VERIFY]`.

**Session token — `[T1]`**

```http
POST /v1/client/sessions/{sid}/tokens
Content-Type: application/x-www-form-urlencoded
```

The 2026-09-24 body was empty and the 200 response shape was:

```json
{
  "jwt": "<redacted>"
}
```

This directly resolves the old “unobserved” claim. It does not prove that
`tokens` replaces `touch`, and `/tokens/api` remains unobserved.

**Client verification route — `[VERIFY]`**

`/v1/client/verify` remains method/contract-conflicted in the reviewed
consolidated corpus. Do not synthesize a Turnstile heartbeat request or
repurpose this route as a generic “verify bearer” call; capture it before
implementation.

### 5.2 `POST /api/feed/v3` contract

**Request shape — `[T1]`**

The 2026-09-24 export contains 23 direct calls. The captured top-level request
keys were `cursor`, `limit`, and `filters`:

```json
{
  "cursor": null,
  "limit": 20,
  "filters": {
    "disliked": "False",
    "fullSong": "False",
    "public": "False",
    "stemComplement": "False",
    "trashed": "False",
    "unlocked": "False",
    "upload": "False",
    "liked": "False",
    "cover": {
      "presence": "<captured-presence-string>"
    },
    "fromStudioProject": {
      "presence": "<captured-presence-string>"
    },
    "persona": {
      "presence": "<captured-presence-string>"
    },
    "stem": {
      "presence": "<captured-presence-string>"
    },
    "user": {
      "presence": "<captured-presence-string>"
    },
    "workspace": {
      "presence": "<captured-presence-string>"
    },
    "ids": {
      "presence": "<captured-presence-string>",
      "clipIds": ["<redacted-clip-id>"]
    },
    "sort": {
      "sortBy": "created_at",
      "sortDirection": "desc"
    }
  }
}
```

Contract rules:

- The eight flag fields are JSON **strings** with observed values `True` or
  `False`, not JSON booleans.
- `cover`, `fromStudioProject`, `persona`, `stem`, `user`, and `workspace` use
  presence objects; captured identifier fields are optional. Do not replace
  presence semantics with booleans.
- `ids` carries a presence string and `clipIds[]` when clip identifiers are
  present. Identifiers remain opaque and are not reproduced here.
- Captured `sort.sortBy` values included `created_at` and `upvote_count`;
  captured direction values included `asc` and `desc`. This is an observed set,
  not an exhaustive enum.
- First-page cursor is `null`. Later pages put the prior response's opaque
  `next_cursor` under request key `cursor`; never send `next_cursor` as the
  request key.
- **`searchText` was not observed in any reviewed feed request.** Server-side
  search remains unimplemented/unverified; do not add it from old prose, UI
  behavior, or a client parameter alone.

**Response shape — `[T1]`**

The response root contains `clips`, `has_more`, and optional `next_cursor`.
Observed clip status values included `complete` and `streaming`; this is not an
exhaustive status vocabulary.

```json
{
  "clips": [
    {
      "id": "<opaque-clip-id>",
      "status": "streaming"
    }
  ],
  "next_cursor": "<opaque-cursor>",
  "has_more": true
}
```

Every observed non-final page carried a cursor. Every observed
`has_more: false` page omitted `next_cursor`. Do not synthesize a cursor after
the final page.

### 5.3 Clip and progressive-media schema

The following are **observed keys**, not a guarantee that every clip has every
field. `[T1]` evidence comes from feed, generation, project, and HAR captures.

| Area | Observed fields | Notes |
|---|---|---|
| Identity | `id`, `entity_type`, `title` | Song captures used `entity_type: "song_schema"`. |
| Processing | `status` | Observed values included `submitted` and `complete`; this is not an exhaustive status vocabulary. |
| Legacy media | `audio_url`, `video_url`, `image_url`, `image_large_url` | Values may be empty while processing. A non-empty value may still be a forbidden sentinel; see below. |
| Progressive media | `media_urls[]` | Each observed audio item has `url`, `content_type`, `delivery`, and `encoding`; this array is the playback/download authority. |
| Observed media taxonomy | `m4a-opus` / `progressive`, `mp3` / `streaming`, `webm-opus` / `streaming` | The first was returned on the captured CloudFront host; the latter two were returned on `audiopipe.suno.ai`. |
| Model observation | `major_model_version`, `model_name` | Values are account/time/model-catalog dependent. |
| Engagement | `play_count`, `upvote_count`, `allow_comments`, `is_verified` | Observed clip metadata/counters. |
| Ownership | `handle`, `display_name`, `user_id`, `is_public`, `is_trashed`, `is_liked` | Not all generation responses contained every ownership key. |
| Creation | `created_at`, `batch_index`, `has_hook`, `is_persona_root`, `action_config` | Observed on some clip contexts. |
| Metadata object | `tags`, `negative_tags`, `prompt`, and other generation/creation fields | `metadata` contents vary by route and generation mode. |

A captured media item has this shape:

```json
{
  "url": "<redacted-complete-media-url>",
  "content_type": "m4a-opus",
  "delivery": "progressive",
  "encoding": "<captured-encoding>"
}
```

In the 2026-09-24 feed sample, every clip had a non-empty legacy `audio_url`,
but every value used the alternate media host's `/api/forbidden` sentinel. That
sentinel is **non-playable**, not a successful media URL. Select a playable item
from `media_urls[]` by captured content type/delivery; never use a
construct-from-ID CDN fallback.

The constructed fallbacks `cdn1.suno.ai/{clip_id}.mp3` and
`d2lwuy8qc234o3.cloudfront.net/1/clip/{clip_id}.m4a` remain `[LEAD]` and are not
part of the captured media contract.

### 5.4 Captured generation and polling behavior

**Captcha check — `[T1]`**

`POST /api/c/check` was captured with a generation context and returned a
requirement decision plus captcha-version metadata. A captcha token was then
present in the generation request. Do not assume captcha is required for every
account/request; call/check the captured flow and handle both decisions.

**Generation submission — `[T1]`**

`POST /api/generate/v2-web/` was captured with a JSON body containing a
captcha token, generation type, title/tags/prompt text, model key, flags,
metadata, cover/continue/artist/persona references, a client transaction ID,
and lyrics-project linkage. The captured response contained a batch `id`,
`clips[]`, batch/model/status metadata, creation time, and batch size.

Those exact request and response keys are capture-backed for the captured run,
but this master intentionally does **not** turn them into a universal required
schema. In particular:

- Model identifiers and availability are runtime data.
- Cover/continue/artist/persona fields are conditional.
- Client metadata is web-client state, not a stable public API requirement.
- The observed response used `id` and `clips[]`; older prose claiming a
  mandatory `task_id` is not canonical.

**Completion polling — `[T1]` behavior, not a fixed `/api/gen/{id}` contract**

The 2026-08-25 capture observed submitted clips with empty `audio_url`, then
observed completion through:

1. `POST /api/feed/v3` using the captured cursor contract, and/or
2. `GET /api/clips/get_songs_by_ids` using repeated `ids` query parameters.

No fixed polling interval was established. `GET /api/gen/{id}` remains a
`[LEAD]`, not the canonical poll route.

**Lyrics co-writing — `[T1]`**

`POST /api/generate/cowrite-lyrics/` was a single-shot request/response. The
captured response exposed a lyrics request ID, lyrics ID, and edited lyrics.
No polling was observed for that call.

### 5.5 Notification, following, and explore contracts

**Mark notifications read — `[T1]`**

`POST /api/notification/v2/read` used JSON with `all` (boolean) and
`before_datetime_utc` (string), and returned a 200 object containing a `status`
string. The capture does not establish a complete status enum.

**Following feed — `[T1]` route, `[VERIFY]` subsequent-page behavior**

`POST /api/social/following-feed` used JSON with numeric `page_size` plus string
`ranking_method` and `result_type`. The 200 response contained
`first_item_timestamp`, `last_item_timestamp`, `page_size`, and `items[]`.
Observed item variants were clip, comment, and followed-user-profile shapes. No
next-page token or cursor was captured, so later-page behavior remains
`[VERIFY]`.

**Explore feed — `[T1]`**

`POST /api/unified/explore` used JSON with `cursor` as a string or `null`. Its
200 response contained `feeds[]` and `next_cursor`; the captured feed objects
included identifier/title, metadata, items, presentation, and logging-context
fields. Exact presentation and logging schemas are intentionally not promoted
beyond those observed categories.

### 5.6 Three-step audio upload contract

**1. Initialize — `[T1]`**

`POST /api/uploads/audio/` used JSON with string `extension` and `upload_type`.
The 200 response contained an opaque upload `id`, a temporary `url`,
`is_file_uploaded`, and a `fields` object with `AWSAccessKeyId`, `Content-Type`,
`key`, `policy`, and `signature`.

**2. Direct storage upload — `[T1]`**

POST multipart form data to the **returned** storage URL with the returned
fields plus `file`. The captured leg returned 204. Do not substitute a
hard-coded bucket URL, synthesize policy fields, attach the Suno bearer, log
the temporary URL/fields, or retain them beyond the upload operation.

**3. Finish — `[T1]`**

`POST /api/uploads/audio/{id}/upload-finish/` used JSON with boolean
`agreed_to_vip_upload_terms` and string `upload_filename` and `upload_type`.
The 200 response was an empty object.

This proves the initialize → direct multipart → finish transport sequence only.
The export did not capture `initialize-clip`, processing-status, or
upload-to-generation linkage, so those steps remain `[LEAD]`/`[VERIFY]` rather
than a complete product workflow.

### 5.7 Other captured analysis contracts

- `GET /api/gen/{id}/aligned_lyrics/v2/` returned `aligned_words[]` with
  `word`, `start_s`, `end_s`, `success`, and `p_align`, plus opaque auxiliary
  objects. The older `start_time`/`end_time` field claim is superseded.
  `[T1]`
- `POST /api/gen/{id}/downbeats_streaming/v2` accepted `{}` and returned a
  complete downbeat analysis object in one response despite the route name.
  `[T1]`
- `GET /api/gen/{id}/waveform-aggregates` returned multi-resolution waveform
  aggregate levels. `[T1]`
- `GET /api/video/generate/{clip_id}/status/` and
  `POST /api/video_gen/pending_batches` were captured. `[T1]`
- `POST /api/lyrics-projects`,
  `POST /api/lyrics-projects/{id}/flush`, and both methods on
  `/api/prompts/v2` were captured, but their complete mutation schemas are not
  reproduced here. `[T1] route/method; schema intentionally conservative]`

### 5.8 Documented routes whose detailed schemas remain uncaptured

The following route families are useful inventory but do **not** have a
canonical request/response schema in this master:

- Billing mutations, checkout, portal, coupons, payment methods, surveys
- Project/collaboration creation and Studio save/render operations
- Lyrics generation, infill, mashup, pair, concat, merge, extend, and cover
  variants other than the captured v2-web submission
- Prompt mutation payloads beyond captured route/method evidence
- Audio-upload `initialize-clip`, processing status, generation linkage,
  validation/error envelopes, and post-finish lifecycle beyond the captured
  three-step transport sequence
- Download preparation, ZIP export, billing-gated clip download, and exact
  `Content-Disposition`/filename behavior
- Persona/custom-model creation, training, archive, and voice verification
- Playlist mutations, project mutations, comments, follows, shares,
  notifications beyond listed response summaries, and song-copy behavior
- Feature-gate values, entitlement matrices, and client cache contents
- Orpheus chat/history/model contracts

For these families, capture the request and response before writing a client.

## 6. Errors, statuses, and rate limits

### 6.1 What the captures establish

- The 2026-08-25 Studio/auth session primarily captured successful 200
  responses, several 204 responses, and OAuth/handshake 302 redirects.
- The 2026-09-24 export contains 74 direct Studio calls: 73 returned 200 and
  one returned 204. This is route evidence, not a universal success/error
  envelope.
- Successful feed, project, generation submission, and media-analysis routes
  returned JSON. Some telemetry, upload-finish, and direct-upload responses
  returned an empty body.
- The 2026-09-22 OAuth recon has no response statuses or bodies and therefore
  adds no status semantics.
- No reviewed capture in the consolidated baseline provides a canonical Suno
  error-body schema.

### 6.2 What is not established

Do not promote these old claims as Suno contracts:

- `200` universally means every documented operation succeeded.
- `401` has one stable refresh/error-body shape.
- `403`, `404`, or `405` have stable Suno error JSON.
- `429` means insufficient credits.
- `430` means requests are too frequent.
- A generation or upload route has a fixed retry count or polling interval.

`429` and `430` meanings are explicitly unresolved. A client may use generic
HTTP handling and captured response metadata, but it must not branch on those
codes using the old “insufficient credits”/“too frequent” folklore. Capture
the status, headers, and redacted body for each rate/quota path before encoding
product behavior.

A `404` is not proof that a scanned route never existed; it may reflect
feature rollout, entitlement, host/path drift, method mismatch, or an invalid
request. A client-side feature gate is not a substitute for server
authorization.

## 7. Model, entitlement, and runtime notes

| Topic | Canonical statement | Evidence |
|---|---|---|
| Model catalog | `GET /api/session/` returned `models[]` plus `user` and runtime configuration. `models[].major_version` and `models[].max_lengths.*` were JSON **integers**, while identifier/name/availability fields remained strings or structured values. Parse numeric fields as numbers with range checks; do not string-coerce them or hardcode an old model list. | `[T1]` Burp 2026-08-25 and 2026-09-24 |
| Account numeric fields | `user.total_clips` was an integer. Billing fields such as `credits`, `monthly_usage`, `monthly_limit`, and `total_credits_left` were integers, while plan/credit-pack price fields included floating-point numbers. Select the parser by field semantics; do not apply one integer conversion to every JSON number. | `[T1]` Burp 2026-09-24 |
| Generation model key | A generation capture used a model key and returned model metadata. The session catalog's numeric `major_version` is not, by itself, proof of the request `mv` string. Read the runtime external model key and availability; do not derive or hardcode a generation key from the integer alone. | `[T1]` generation capture; runtime catalog `[T1]` |
| Model duration/stem limits | Do not hardcode values from old V3/V4/V5/V5.5 summaries. Numeric limits in the session/billing catalog are account- and model-dependent runtime data. | `[T1]` numeric catalog fields; old hardcoded lists superseded |
| Prompt/title limits | Do not hardcode old character maxima. `models[].max_lengths.title`, `prompt`, `tags`, `negative_tags`, and `gpt_description_prompt` were captured as JSON integers; validate defensively before converting to application integers. | `[T1]` session shape; values are runtime data |
| Generation status | `submitted` and `complete` were observed. This is not the complete status vocabulary. | `[T1]` feed/generation captures |
| Polling | Captured completion was observed through feed/get-songs; no interval is canonical. | `[T1]` behavioral capture |
| Feature gates | Statsig routes are captured. Gate names/defaults/local-storage values from scans are leads, not server authorization. | `[T1]` route; `[LEAD]` values |
| Billing/plan matrix | Billing exposes numeric credit, usage, download, upload, voice, agentic, and custom-model limits. Values remain account/time dependent; parse integer and floating-point fields according to their names, never as booleans or fixed plan constants. | `[T1]` Burp 2026-09-24 shape; entitlement interpretation remains runtime data |
| Upload limits | Billing returned numeric `audio_upload_limits.min` and `.max`; the old fixed two-minute claim remains non-canonical. Apply account/runtime limits rather than a hardcoded duration. | `[T1]` numeric shape 2026-09-24; fixed duration superseded |
| Stem counts | The old “12 stems” claim is not canonical without model/catalog or processing capture. | `[LEAD]` |
| Video status | The captured video-status response exposed `status` and `video_url`, but one observed `complete` value with an empty URL is not a universal success rule. Clip-level media remains distinct. | `[T1]` route; interpretation conservative |
| Realtime | `GET /api/realtime/discover` returned stream/auth metadata. Do not log the returned token material. | `[T1]` |
| Cookies/analytics | Clerk cookies establish browser auth; analytics cookies do not grant Studio authorization. | `[T1]` auth request context |

## 8. Source and provenance map

### 8.1 2026-09-24 Burp export

The reviewed source is the external directory
`~/Documents/suno-burp-exports/sept-09-2026/`. Despite its `sept-09` label,
Burp 2026.8 exported 432 items on **2026-09-24 14:01–14:13 MDT**, and the item
timestamps are 2026-09-24. The directory remains outside the repository because
its base64 XML is not sanitized.

| Source file | SHA-256 prefix | Directly observed role | Limitation |
|---|---|---|---|
| `auth.suno.com` | `fdf9794b66a94739` | `GET /v1/client`, session `POST .../tokens`, session `POST .../touch` | Point-in-time; no Google callback; raw file contains live auth material. |
| `studio-api-prod.suno.co` | `e714fe9cf751ed17` | 74 direct Studio calls and 48 matching preflights, including feed, account/billing, notification, social, explore, upload-init/finish, and media/status routes | Filename is truncated/mistyped; actual API host is `studio-api-prod.suno.com`. Raw file contains credentials and private data. |
| `suno-uploads.s3.amazonaws.com` | `40e256480d15001f` | Direct multipart audio upload leg and 204 response | Temporary URL and policy fields are secret; no Studio bearer is used. |
| `suno.com` | `7190f66cc7e886da` | Web redirects, static bundles, and web session-recovery evidence | Bundle strings remain leads; this export did not capture Google OAuth. |
| `cdn1.suno.ai`, `cdn2.suno.ai`, `cdn-o.suno.com` | `a7a4330a4694dfc`, `9452a8461f1ff37a`, `d184605c13754cbc` | Direct image/static asset requests supporting media-host distinctions | Assets do not prove API authorization or return URL stability. |
| `logger.full.log.txt` | `cdf06c2302598d14` | One static JavaScript request | Not a log of route calls; decoded route strings are `[LEAD]` only. |
| Remaining telemetry/static/health files | See [`raw/README.md`](raw/README.md) | Supporting provenance and hash manifest | No promoted product API contract. |

Full source hashes and handling rules are in
[`raw/README.md`](raw/README.md). The source map must not copy raw payloads,
complete media URLs, identifiers, or personal data into Markdown.

### 8.2 Earlier evidence and implementation references

| Source | Role | Limitation |
|---|---|---|
| 2026-08-25 Burp exports and sanitized extracts | Auth, feed, generation, billing, project, clip, lyrics, analysis, video, and app behavior not re-observed directly on 2026-09-24 | External, point-in-time evidence. |
| 2026-09-22 [`raw/sanitized-recon-2026-09-22.json`](raw/sanitized-recon-2026-09-22.json) | Strongest direct evidence for the observed Google OAuth request sequence | Request-only; no response status/body. |
| 2026-09-23 browser HAR | Browser/Studio traffic and progressive-media evidence | External, point-in-time capture. |
| [`raw/endpoints_sniffed.list`](raw/endpoints_sniffed.list) | Candidate route discovery | `[LEAD]` only; no method, schema, status, or liveness guarantee. |
| `src/suno/SunoEndpoints.hpp` and commit `678be76` | Implementation mirror and corrective history | Neither is a capture and neither overrides this inventory. |

Superseded topic prose remains in Git history and the timestamped backup
graveyard. It is not a live authority and must not be linked as current
documentation.

## 9. Maintenance rule for future captures

For every future capture:

1. Record capture timestamp, source filename, SHA-256, exact method, normalized
   host/path, content type, and only redacted request/response shapes.
2. Promote to `[T1]` only from a directly reviewed request/response. Scans,
   bundles, prose, and client constants remain `[LEAD]`.
3. `[T1]` means observed, not universally required. Keep conflicting variants or
   unknown fallback/preference behavior `[VERIFY]` until a capture resolves it.
4. Never record secrets, cookies, OAuth codes/state, personal data, identifiers,
   private text, temporary upload fields, or complete media URLs.
5. Update the source map and remove dead live-document links; do not copy raw
   secret-bearing exports into the repository.
