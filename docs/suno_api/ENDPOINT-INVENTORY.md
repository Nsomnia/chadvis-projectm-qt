# Suno API Canonical Endpoint Inventory

**Status:** Canonical API-spec master, consolidated 2026-09-24; non-contractual
material extracted 2026-09-26; 2026-08-25 `auth.suno.com` export folded in
2026-09-28
**Current evidence baseline:** the 2026-09-24 Burp export reviewed on 2026-09-24, reconciled with the 2026-08-25 Burp corpus — including the 13-item `auth.suno.com` export folded in on 2026-09-28 — the 2026-09-22 sanitized OAuth recon, and the 2026-09-23 browser HAR
**Scope:** Suno web authentication, Studio API, library/feed, generation, media processing, account surfaces, social surfaces, and capture-backed contracts

> **Unofficial and reverse-engineered.** Suno does not publish this API as a
> supported public API. This inventory records behavior observed from the
> production web application, sanitized request recon, and static/endpoint
> scans. A listed route is not an endorsement, availability guarantee, or
> permission to bypass access controls. Use only accounts and actions you are
> authorized to use, and accept that private interfaces can change without
> notice.

> **Canonical-source rule:** this file is the sole API-spec master for this
> repository. Where it conflicts with older topic prose, scans, implementation
> comments, code constants, or any other document, this file wins. It records
> **directly captured** contracts only. `[LEAD]` and `[VERIFY]` rows are not
> contracts and do not live here; they are research and capture planning
> material in [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md).

## Document map

This file owns evidence labels, precedence, hosts, request/response
conventions, the captured route catalog, capture-backed contracts, status/limit
semantics, and runtime notes. Within authentication it also owns the
**captured credential shapes** ([§3.3](#33-captured-clerk-credential-shapes--t1))
and the **captured Clerk instance and captcha configuration**
([§3.4](#34-captured-clerk-instance-and-captcha-configuration--t1)) — that is
where the token hierarchy, cookie attributes, enabled sign-in strategies, and
the two-captcha-systems split live.
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) owns
every `[LEAD]`/`[VERIFY]` observation.
[`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) owns the observed
Google web request sequence, the Clerk cookie families, and the binding
native-callback gate. [`raw/README.md`](raw/README.md) owns provenance, SHA-256
hashes, artifact retention, and the maintenance rule for future captures. The
operational runbook is [`../integration/SUNO.md`](../integration/SUNO.md).

## 1. Evidence, precedence, and conflicts

### 1.1 Confidence labels

| Label | Meaning | Permitted use |
|---|---|---|
| `[T1]` | Directly present in a cited request/response capture, with secrets redacted. For the 2026-09-22 OAuth recon, request presence only is T1. | Historical contract, subject to drift and client-side defensive parsing. |
| `[LEAD]` | Found in a JS/HTML scan, raw endpoint dump, old inventory, reconstructed path, or prose without a direct capture. An unlabeled old row is always a lead. | Research and capture planning only. |
| `[VERIFY]` | Sources conflict, or one part of the route contract is not captured. | Do not call until a new capture resolves it. |

`[LEAD]` and `[VERIFY]` rows are catalogued in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md), which quotes the definitions above
verbatim as its governing rule. Neither label may be used to justify an
implementation. Conflicting `[VERIFY]` subjects are additionally tracked in
[Appendix A](#appendix-a--explicit-conflict-register).

Additional notation used in the catalog:

- A trailing `?` on a method, such as `GET?`, means the method came from prose
  or reconstruction rather than a direct capture.
- `Bearer?` means a bearer is plausible for a Studio route but the individual
  route's auth requirement was not directly established.
- `Clerk cookies` means only the Clerk cookie names listed in
  [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) are relevant.
  Analytics and advertising cookies are not authentication inputs.
- `GET/POST` means both methods were directly captured; it does not mean other
  methods are impossible.
- Path spellings, trailing slashes, query-key names, and body-field names are
  significant. Do not silently normalize them from a scan.

### 1.2 Precedence

1. The 2026-09-24 Burp request/response export is strongest for every contract
   it directly contains. Its directory is labeled `sept-09-2026`, but the XML
   export and item timestamps are dated 2026-09-24; use the timestamps, not the
   directory label, when dating evidence. The base64 XML is **not sanitized** and
   stays outside the repository.
2. The direct sanitized request capture `raw/sanitized-recon-2026-09-22.json`
   is strongest for the **observed Google OAuth web-request sequence**. It has
   no response status or response body, so this document assigns no status/body
   semantics to that sequence.
3. The 2026-08-25 Burp request/response exports and their sanitized extracts
   govern captured feed, generation, billing, project, media-analysis, and
   other Studio behavior not directly re-observed in the 2026-09-24 export.
   The separate 2026-08-25 `auth.suno.com` export folded in on 2026-09-28 is
   strongest for the captured Clerk auth contracts it directly contains —
   bearer/refresh/handshake credential shapes, the `sign_ins` form fields, the
   captcha-gated `verify` heartbeat, the `/v1/environment` instance facts, and
   the two Suno-owned Google social legs with their 302 statuses. Its export
   identity and hash are in [`raw/README.md`](raw/README.md); it captured no
   loopback or non-`suno.com` callback.
4. The 2026-09-23 browser HAR governs captured web/Studio traffic and the
   progressive `media_urls` observations.
5. Corrective commit `678be76` and the current endpoint map are implementation
   references, not evidence that can override a newer direct capture.
6. Raw scans, decoded frontend bundles, and old topic prose supply `[LEAD]`
   inventory only. External SDK folklore, constructed media paths, and
   client-side flag names cannot promote a route above `[LEAD]`.

### 1.3 Implementation status — a separate axis from evidence

Route catalog tables carry an `Implemented` column with exactly three values:

| Value | Meaning |
|---|---|
| `wired` | The route constant exists **and** is referenced from `src/`. |
| `declared-unused` | The constant exists in `src/suno/SunoEndpoints.hpp` but has zero references in `src/`. |
| `not-in-code` | No constant exists; the row is documented from a capture only. |

> **`[T1]` means "directly captured", not "shipped."** Evidence and
> implementation are **independent axes**. A route can be `[T1]` and
> `declared-unused` (captured, constant present, deliberately not called), or
> `[T1]` and `not-in-code` (captured, nothing in code yet), or `[T1]` and `wired`.
> Conflating the two axes is the specific error this column exists to prevent:
> an `[LEAD]` row cannot become a contract by being wired, and a `[T1]` row does
> not become implemented by being captured. Do not read `declared-unused` as
> "unsupported," and do not read `wired` as "verified."

Audit method and boundaries, so the column is not over-read:

- The audit surface is the constant set in `src/suno/SunoEndpoints.hpp`
  (77 constants at the 2026-09-26 audit: 12 referenced from `src/`, 65
  unreferenced). A reference in a comment does not count; only a use in code.
- The two implemented Clerk host-relative routes (`GET /v1/client` and
  `POST /v1/client/sessions/{sid}/touch`) are built in
  `src/suno/auth/ClerkAuthClient.cpp` from `ClerkAuthClient::AUTH_BASE`, because
  Clerk constants deliberately live outside `SunoEndpoints.hpp`. A path built
  and dispatched from a client source file is audited as `wired`. The captured
  `/tokens` route has no such builder and is `not-in-code`.
- `wired` on those two routes does not mean either captured variant is
  implemented. As of the 2026-09-28 review they send **no**
  `__clerk_api_version`/`_clerk_js_version` query keys and hardcode
  `intent=focus` on the touch body — i.e. they implement the **2026-09-24**
  variant, while the 2026-08-25 `auth.suno.com` export observed the
  version-query-key variant with an empty body. Both variants are `[T1]`
  (sections 3.1 and 5.1); the master does not choose between them.
- Host enforcement that compares an inline literal rather than a constant — for
  example the playback host check in `src/suno/SunoDownloader.cpp` — is not
  visible to this column. A host row with no constant is `not-in-code` here even
  when the host is enforced inline.
- Re-run the audit before trusting these values; they describe this tree on
  2026-09-26, not the server.

### 1.4 Current client implementation boundary

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

This table is a **contract-host allowlist**, not a log of every host that
appeared in the corpus. Hosts observed only as static assets, telemetry,
analytics, or health checks — including `cdn-o.suno.com`, `goto.suno.com`,
`s.prod.suno.com`, `statusz.suno.ai`, the hCaptcha asset/endpoint hosts, and the
`telemetry host whose export filename mis-spells the real
`m-stratovibe.prod.suno.com` — are deliberately **absent** here, because none of
them carries a promoted contract. Their provenance and SHA-256 hashes are in
[`raw/README.md`](raw/README.md). Their absence is a fail-closed decision, not an
oversight: do not add a host here without a captured contract for it.

Naming a captcha provider in section 3.4 does **not** add any captcha host,
widget, or challenge endpoint here. A sitekey is a public embed value, not an
API contract, and a client must not send requests to a captcha service that is
not in this table.

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

## 3. Authentication and OAuth

The Clerk cookie families relevant to the observed flow, the captured Google
web-flow request table, and the binding native-callback gate are owned by
[`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md). They are not
duplicated here. That file is the only place a native-callback requirement may
be stated.

### 3.1 Canonical bearer acquisition and session-token routes

1. Send `GET /v1/client` on the Clerk host with the captured cookie context.
   The 2026-09-24 request had no query keys; the 2026-08-25 request included
   version query keys. Both are `[T1]` point-in-time variants.
2. Read the active session identifier from
   `response.last_active_session_id` and the bearer from the matching
   `response.sessions[].last_active_token.jwt`. Do not assume array position.
   `[T1]`
   In the 2026-08-25 capture the four available identifiers — the envelope's
   `response.last_active_session_id`, `response.sessions[0].id`, the access
   token's `sid` claim, and the `{sid}` path segment of the `touch` request —
   are the same value, which corroborates selecting from the envelope. That is
   one capture containing one session, so it does not prove the envelope
   selector and the `sid` claim stay identical for a multi-session account. The
   envelope remains the selector; the rule above is unchanged
   ([conflict register](#appendix-a--explicit-conflict-register)).
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

### 3.2 Captured auth and OAuth route catalog

All rows here are `[T1]`. Every `[LEAD]` and `[VERIFY]` auth row is in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md); the method/purpose conflicts among
them remain recorded in [Appendix A](#appendix-a--explicit-conflict-register).

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/v1/client` | Clerk cookies | Fetch client/session state and initial bearer | `[T1]` Burp 2026-08-25 and 2026-09-24 | wired |
| POST | `/v1/client/sessions/{sid}/touch` | Clerk cookies | Touch active session and obtain a fresh bearer from client/session envelopes | `[T1]` Burp 2026-08-25 and 2026-09-24; request variants coexist | wired |
| POST | `/v1/client/sessions/{sid}/tokens` | Clerk cookies | Mint/return a session bearer as top-level `jwt` | `[T1]` Burp 2026-09-24 | not-in-code |
| POST | `/v1/client/sign_ins` | Clerk/browser context | Begin Clerk sign-in attempt; the captured form carries a `redirect_url` naming a Suno-owned SSO completion path | `[T1]` request-only recon 2026-09-22; form flow also in Burp 2026-08-25 | not-in-code |
| GET | `/social/login/google-oauth2/` | Browser/Clerk context | Provider redirect initiation; captured 302 to the provider, `Set-Cookie: sessionid` (HttpOnly, `SameSite=None`, Secure) | `[T1]` request-only recon 2026-09-22; Burp 2026-08-25 request/response | not-in-code |
| GET | `/social/complete/google-oauth2/` | Provider callback context | Suno-owned Google callback; captured 302 to the captured `next` target, sets a Clerk `__client` cookie | `[T1]` request-only recon 2026-09-22; Burp 2026-08-25 request/response | not-in-code |
| GET | `/v1/client/handshake` | Clerk cookies | Browser cookie/session handshake; takes an absolute `redirect_url` and 302-redirects to it with a one-shot `__clerk_handshake` query value | `[T1]` Burp 2026-08-25 | not-in-code |
| POST | `/v1/client/verify` | Clerk cookies | Captcha-gated Clerk session heartbeat/keepalive; 204 No Content every observed time | `[T1]` Burp 2026-08-25, 5 items | not-in-code |
| GET | `/v1/environment` | Clerk/public instance context | Clerk instance/environment configuration | `[T1]` Burp 2026-08-25 | not-in-code |

`POST /v1/client/sessions/{sid}/tokens` is `[T1]` and `not-in-code`: captured,
not implemented. That combination is the intended use of the `Implemented`
column — see section 1.3.

**`sign_ins` form contract — `[T1]`.** The 2026-08-25 capture recorded a
form-encoded body whose field names, in captured order, were `strategy`,
`redirect_url`, and `action_complete_redirect_url`, with `strategy` observed as
`oauth_google`, `redirect_url` naming a `suno.com` SSO completion path, and
`action_complete_redirect_url` carrying the post-handshake target with the
handshake value embedded as a query parameter. No value is reproduced here. This
is the **field contract only**: the SSO completion route named by
`redirect_url` was never itself exchanged, and its lead row stays `[LEAD]` in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md). Do not promote that route from this
observation, and do not assume `strategy` or `action_complete_redirect_url`
are the only legal values for other providers.

**`verify` is a different route from `/v1/verify`.** `POST /v1/client/verify`
is now `[T1]`; `/v1/verify` is a separate, still-unobserved route with its own
conflict-register entry and its own `[VERIFY]` lead row. Do not merge them,
alias them, or read the resolution of one as evidence about the other.

**Stale lead row.** The `/v1/client/verify` row in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) is superseded by the resolution above
and should be dropped from that file by its owner. This master wins on
conflict, so the lead row is not authority and nothing in this section is
waiting on it; it is called out here only so a reader who lands on the lead
file first is not misled.

### 3.3 Captured Clerk credential shapes — `[T1]`

The 2026-08-25 export makes three distinct credentials observable. Record
**shapes, claim names, and lifetimes only**. No token, `secret`, `jti`/`jit`,
`client_id`, `user_id`, session identifier, email, handle, or other account
value is reproduced anywhere in this document, and none may be logged,
persisted, or reconstructed from these names.

| Credential | Where it appears | Observed shape | Observed lifetime |
|---|---|---|---|
| Access token | `Authorization: Bearer` on Studio calls; `sessions[].last_active_token.jwt` in the `/v1/client` envelope | RS256, JOSE `kid: suno-api-rs256-key-1`; the JOSE header additionally carried an `x-ably-token` key | **Exactly 3600 s** (`exp - iat`, one sample) |
| Refresh token | A separate credential inside the `__client` cookie — not the `__session` access cookie | RS256, same `kid`; carries a `suno.com/claims/token_type` of `refresh`, a `suno.com/claims/client_id`, `iss`, `exp`, and a `secret` claim that is the actual bearer material | **≈ 1 year**, matching the observed `Set-Cookie` `Max-Age=31536000` |
| Handshake nonce | The `__clerk_handshake` query value appended to the handshake `redirect_url`, and the `Max-Age=0` cookie of the same name | RS256; `suno.com/claims/token_type` of `handshake`, `iss`, plus a `handshake` **array whose entries are literal `Set-Cookie` directive strings** | One-shot (`Max-Age=0`) |

Access-token claim names, by group, as observed on the wire: the standard
`aud` (`suno-api`), `azp` (`https://suno.com`), `iss` (`https://auth.suno.com`),
`sub`, `sid`, `iat`, and `exp`; the namespaced identity claims
`suno.com/claims/token_type`, `suno.com/claims/user_id`,
`suno.com/claims/email`, `https://suno.ai/claims/clerk_id`, and
`https://suno.ai/claims/email`; the device/session group `suno/did` (a numeric
device id), `suno/handle`, `suno/user_id`, `suno/username`, and `suno/joined`;
and the entitlement/anti-abuse group `plan`, `jit`, and `fva`. Treat this as an
observed claim-name set for one capture, not a closed schema: a client must not
reject a token for carrying an unknown claim, and must not require any claim
beyond the ones it actually reads.

The `__client_uat` cookie is a separate, **non-secret**, suffixed and
instance-keyed companion cookie (`Domain=suno.com`, `Max-Age=31536000`). It is
session-presence metadata, not a credential, and must not be treated as a
refresh token or substituted for the `__client` refresh credential. The
instance-key suffixes themselves belong to the cookie families owned by
[`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md).

**What the handshake is — `[T1]`.** `GET /v1/client/handshake` takes an absolute
`redirect_url` and answers 302 with a `Location` carrying the same URL plus the
handshake nonce as a query value. The nonce's `handshake` array is the
sanctioned mechanism for exchanging a refresh credential for a session
credential: its entries are literal cookie directives, observed clearing
`__session` and `__clerk_handshake` and setting `__client_uat` to `0`. Observed
query-key names were `redirect_url`, `__clerk_api_version`,
`suffixed_cookies`, `__clerk_hs_reason`, and `format` (value `nonce`); the
observed `__clerk_hs_reason` value was `client-uat-but-no-session-token`. This
is a coherent, directly observed exchange contract.

**What it does not establish.** It is **not** proof that a non-`suno.com`
`redirect_url` — and in particular an app-owned `http://127.0.0.1:<port>`
loopback target — is accepted. Both observed `redirect_url` values were
`https://suno.com/…`. Whether Clerk validates or honours an arbitrary
`redirect_url` is the single open question, and it is owned by
[`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md), whose gate is
unchanged by this section. **This section is not that gate and must not be
read as a relaxation of it.** A client must not hand-roll the handshake,
synthesize a `Set-Cookie` directive array, or construct a nonce.

The 3600 s measured access-token lifetime is also the independent confirmation
that the client's sub-hour proactive-refresh window is proportionate; it does
not establish a refresh cadence, a refresh-route preference, or a server
renewal policy. Route selection among `tokens` and `touch` remains `[VERIFY]`
(section 3.1 and the [conflict register](#appendix-a--explicit-conflict-register)).

### 3.4 Captured Clerk instance and captcha configuration — `[T1]`

`GET /v1/environment` (14 KB JSON) is `[T1]` and `not-in-code`. Only the fields
below are recorded. Other top-level keys observed in the same response —
`user_settings`, `commerce_settings`, `fraud_settings`, `api_keys_settings`,
`maintenance_mode`, and `organization_settings` — are named here and their
contents are deliberately not reproduced.

| Field | Observed value | Notes |
|---|---|---|
| `auth_config.single_session_mode` | `true` | One session at a time, which is why the session-selector corroboration in section 3.1 is single-session by construction. |
| `auth_config.identification_strategies` | `oauth_apple`, `oauth_discord`, `oauth_facebook`, `oauth_google`, `oauth_microsoft`, `oauth_token_apple`, `phone_number` | **Six social providers are enabled server-side, not only Google.** |
| `auth_config.first_factors` | `phone_code` | Distinct from the identification strategy list; do not conflate the two arrays. |
| `auth_config.second_factors` | `[]` | Empty in this capture; not a universal statement. |
| `auth_config.preferred_sign_in_strategy` | `otp` | |
| `auth_config.password` / `auth_config.phone_number` / `auth_config.username` | `on` / `on` / `off` | Username sign-in is off. |
| `display_config.google_one_tap_client_id` | present | **Equal to the `client_id` sent to Google** in the captured `/social/login/google-oauth2/` redirect. Agreement is the recorded fact; the value is an OAuth client identifier and is never reproduced here. |

**Two separate captcha systems — `[T1]`.** The Clerk auth captcha and the
`suno.com` web sign-in UI captcha are **different providers guarding different
things**, and the generation captcha is a third, separately governed surface.
They are not two names for one challenge and must never be conflated.

| Guard | Provider | Evidence |
|---|---|---|
| Clerk authentication — the `/v1/client/verify` heartbeat (section 5.1) | **Cloudflare Turnstile** | `display_config` reported `captcha_provider: turnstile`, `captcha_heartbeat: true`, and `captcha_widget_type: smart`. The `captcha_token` values on the wire had Turnstile's `1.<payload>.<hash>` shape, which independently rules out hCaptcha for this route. |
| The `suno.com` web sign-in and session-recovery **UI** | **hCaptcha** | The 2026-09-22 sanitized recon records `POST`/`OPTIONS` to `api.hcaptcha.com/getcaptcha/…` on those pages. A separate provider, on a different surface, with a different token shape. |
| Generation — `POST /api/c/check` (section 5.4) | **Not stated.** | The route is `[T1]` and its requirement decision is captured. **Its provider is not captured and is deliberately left unstated.** Do not infer it from either row above, from the widget type, or from the token format. |

The two Turnstile sitekeys below are the one secret-shaped value class this
document may carry, because they are public by design: Cloudflare sitekeys are
published for embedding in every page that uses them and grant no access to
anything. Recording them is what makes the captcha contract usable.

```text
display_config.captcha_provider             = "turnstile"
display_config.captcha_heartbeat            = true
display_config.captcha_widget_type          = "smart"
display_config.captcha_public_key           = "0x4AAAAAAAWXJGBD7bONzLBd"
display_config.captcha_public_key_invisible = "0x4AAAAAAAFV93qQdS0ycilX"
display_config.captcha_oauth_bypass         = []
```

`captcha_oauth_bypass` was an empty list. Separately, the `/v1/client` envelope
carries two top-level fields the master previously did not mention:
`captcha_bypass` (observed `false`), a second and independent captcha-requirement
signal alongside the `display_config` values above, and `cookie_expires_at`, an
epoch-millisecond timestamp whose observed value equalled the access token's
`exp` × 1000. Both field names are captured; **infer nothing about
`captcha_bypass` semantics beyond its name**, and treat `cookie_expires_at`
as advisory metadata rather than a guaranteed session lifetime.

A client must not solve or replay a Turnstile token, must not send one from
either route to the other, and must not treat either sitekey as an API
credential.

## 4. Captured route catalog

Every row in this section is `[T1]`. All `[LEAD]` and `[VERIFY]` rows were
extracted to [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) on 2026-09-26 without
change to their evidence status. `Implemented` is section 1.3's axis and is
independent of the evidence label.

### 4.1 Billing and account commerce

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/billing/info/` | Bearer | Current billing/credit/account information | `[T1]` Burp 2026-08-25 | wired |
| GET | `/api/billing/usage-plans` | Bearer | Available usage-plan list | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/billing/usage-plan-descriptions/` | Bearer | Plan descriptions | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/billing/usage-plan-faq/` | Bearer | Plan FAQ content | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/billing/usage-plan-web-table-comparison/` | Bearer | Web plan-comparison data | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/billing/eligible-discounts` | Bearer | Account-eligible discounts | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/billing/conversion-tracking` | Bearer | Conversion-tracking surface | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/billing/auto-reload/nudge-check` | Bearer | Auto-reload nudge check | `[T1]` Burp 2026-08-25 | declared-unused |

No billing mutation, checkout, portal, coupon, payment-method, or survey route
is captured. See section 5.8.

### 4.2 User, backend session, and onboarding

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/session/` | Bearer | Bootstrap user/session plus runtime model catalog | `[T1]` Burp 2026-08-25 | wired |
| GET | `/api/user/metadata` | Bearer | User metadata/plan summary | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/user/get_user_session_id/` | Bearer | Suno-side session identifier | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/user/tos_acceptance` | Bearer | Terms-acceptance state | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/user/user_config/` | Bearer | Read current config using an empty JSON object | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/onboarding/current` | Bearer | Current onboarding state | `[T1]` Burp 2026-08-25 | not-in-code |
| POST | `/api/onboarding/start` | Bearer | Start onboarding flow | `[T1]` Burp 2026-08-25 | not-in-code |

The endpoint mirror also carries a `SESSION_CATALOG` alias for the same
`/api/session/` path; it is `declared-unused` and is not a second route.

### 4.3 Projects and Studio

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/project/me` | Bearer | Workspace/project listing | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/project/default` | Bearer | Default workspace and project clips | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/project/{project_id}` | Bearer | Project detail | `[T1]` HAR 2026-09-23 | declared-unused |
| GET | `/api/project/default/pinned-clips` | Bearer | Pinned default-workspace clips | `[T1]` HAR 2026-09-23 | declared-unused |

No project creation, collaboration, or Studio save/render operation is captured.

### 4.4 Generation, lyrics, prompts, and analysis

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| POST | `/api/c/check` | Bearer | Captcha requirement check for generation | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/generate/v2-web/` | Bearer | Captured web music-generation submission | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/generate/cowrite-lyrics/` | Bearer | Single-shot lyrics co-writing/editing | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/lyricists` | Bearer | Lyricist/persona selection list | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/lyrics-projects` | Bearer | List/read lyrics projects | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/lyrics-projects` | Bearer | Create/update lyrics-project surface | `[T1]` Burp 2026-08-25; exact mutation schema not canonicalized here | declared-unused |
| POST | `/api/lyrics-projects/{id}/flush` | Bearer | Persist/finalize lyrics-project state | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/prompts/v2` | Bearer | Prompt/style data | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/prompts/v2` | Bearer | Prompt/style mutation surface | `[T1]` Burp 2026-08-25; exact mutation schema not canonicalized here | declared-unused |
| GET | `/api/prompts/suggestions` | Bearer | Prompt suggestions | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/prompts/upsample` | Bearer | Prompt/tag upsampling | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/gen/{id}/aligned_lyrics/v2/` | Bearer | Timed word alignment | `[T1]` Burp 2026-08-25 | wired |
| POST | `/api/gen/{id}/downbeats_streaming/v2` | Bearer | Complete downbeat analysis response | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/gen/{id}/waveform-aggregates` | Bearer | Multi-resolution waveform aggregates | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/gen/{id}/increment_play_count/v2` | Bearer | Play-count telemetry | `[T1]` Burp 2026-08-25 | not-in-code |
| POST | `/api/gen/{id}/listen_milestone` | Bearer | Listen-milestone telemetry | `[T1]` Burp 2026-08-25 | not-in-code |
| GET | `/api/gen/{id}/comments` | Bearer | Generation/clip comments | `[T1]` Burp 2026-08-25 | declared-unused |

Generation submission is captured but intentionally **not** wired: the CAPTCHA
token flow and durable processing contract are unimplemented (section 1.4).

### 4.5 Library, feed, and playlists

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| POST | `/api/feed/v3` | Bearer | Primary cursor-based library feed | `[T1]` Burp 2026-08-25 | wired |
| POST | `/api/unified/feed` | Bearer | Profile-style unified feed | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/unified/homepage` | Bearer | Homepage/unified feed | `[T1]` corrective endpoint map/Burp evidence | declared-unused |
| GET | `/api/clips/get_songs_by_ids` | Bearer | Bulk clip/song hydration by repeated `ids` query values | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/playlist/me` | Bearer | Current user's playlists | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/unified/explore` | Bearer | Cursor-based explore feed | `[T1]` Burp 2026-09-24 | wired |

No reviewed capture establishes server-side search as a `searchText` field on
`POST /api/feed/v3`. Search claims derived from old prose or client parameters
remain `[LEAD]`; clients must use local filtering until a direct request/response
capture proves the remote search contract. No playlist mutation is captured.

### 4.6 Clips, media, rights, upload, and processing

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/clips/{clip_id}/attribution` | Bearer | Clip attribution/rights metadata | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/clips/parent` | Bearer | Parent clip relation | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/clips/remixes` | Bearer | Clip remixes | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/clips/remixes/count` | Bearer | Remix count | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/clips/get_similar/` | Bearer | Similar clips | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/mango/rights` | Bearer | Rights/crypto response surface | `[T1]` Burp 2026-08-25; payload is sensitive and not reproduced here | declared-unused |
| GET | `/api/video/generate/{clip_id}/status/` | Bearer | Video-render status | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/video_gen/pending_batches` | Bearer | Pending video batch identifiers | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/uploads/audio/` | Bearer | Initialize an audio upload and return a temporary direct-upload URL/policy | `[T1]` Burp 2026-09-24; JSON request/response | wired |
| POST | `/api/uploads/audio/{id}/upload-finish/` | Bearer | Finalize a completed direct audio upload | `[T1]` Burp 2026-09-24; JSON request/empty-object response | wired |
| POST | Returned URL on `suno-uploads.s3.amazonaws.com` | Temporary signed multipart fields | Direct-upload captured audio bytes | `[T1]` Burp 2026-09-24; 204 response | wired |

The direct-upload row is the only row in this document whose target is not
fixed by the catalog: the URL and fields come from the initializer response and
must never be hard-coded (section 2.2). The three upload rows are the only
implemented transport sequence; the non-transport upload lifecycle is not
captured (section 5.6).

### 4.7 Persona and custom models

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/custom-model/pending/` | Bearer | Pending custom-model state | `[T1]` Burp 2026-08-25 | declared-unused |

No plan matrix, verification requirement, per-account model limit, or voice-clone
availability statement is canonical without a current capture. No persona
creation, custom-model training, archive, or voice-clone route is captured.

### 4.8 Social, sharing, notifications, and following

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/profiles/{handle}/info` | Bearer | Public/private profile information | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/profiles/pinned-clips` | Bearer | Pinned profile clips | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/notification/v2` | Bearer | Notifications | `[T1]` Burp 2026-08-25 | wired |
| GET | `/api/notification/v2/badge-count` | Bearer | Notification badge count | `[T1]` Burp 2026-08-25 | wired |
| POST | `/api/notification/v2/clear-badge` | Bearer | Clear notification badge | `[T1]` Burp 2026-08-25 | not-in-code |
| POST | `/api/notification/v2/read` | Bearer | Mark notifications read, optionally before a UTC cutoff | `[T1]` Burp 2026-09-24 | wired |
| GET | `/api/share/stats` | Bearer | Share count/statistics | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/social/following-feed` | Bearer | Activity feed for followed accounts | `[T1]` Burp 2026-09-24; subsequent-page behavior `[VERIFY]` | declared-unused |

The following feed's first page is captured; no next-page token or cursor was
captured, so later-page behavior remains `[VERIFY]` (section 5.5). No follow,
comment, share-link, or song-copy action is captured.

### 4.9 Feature gates, app chrome, and captured analysis surfaces

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| POST | `/api/statsig/experiment/` | Bearer | Query experiment parameters | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/statsig/experiment/forked-onboarding` | Bearer | Forked-onboarding experiment state | `[T1]` Burp 2026-08-25 | not-in-code |
| GET | `/api/labs/configs` | Bearer | Labs catalog/configuration | `[T1]` Burp 2026-08-25 | not-in-code |
| GET | `/api/modals` | Bearer | App modal catalog | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/cms/nudges/publish-nudge` | Bearer | Publish nudge state/content | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/cms/nudges/share-nudge` | Bearer | Share nudge state/content | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/realtime/discover` | Bearer | Realtime/Ably discovery and token metadata | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/challenge/progress` | Bearer | Challenge/bonus progress | `[T1]` Burp 2026-08-25 | not-in-code |
| GET | `/api/contests/` | Bearer | Contest catalog | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/music_player/playbar_state` | Bearer | Read playbar state | `[T1]` Burp 2026-08-25 | declared-unused |
| POST | `/api/music_player/playbar_state` | Bearer | Synchronize playbar state | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/personalization/memory` | Bearer | Personalization memory/profile | `[T1]` Burp 2026-08-25 | declared-unused |
| GET | `/api/personalization/settings` | Bearer | Personalization settings | `[T1]` Burp 2026-08-25 | declared-unused |

Observed **client-side** flag and cache names are not here. They are localStorage
keys with zero capture evidence, and they are listed — clearly marked as flag
names, never as routes — in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md). Forcing a client-side flag is not proof
that the backend has enabled a route, plan, model, or entitlement.

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
captcha_bypass
cookie_expires_at
```

Select the session matching `last_active_session_id`. A session-array index is
not a stable selector. `captcha_bypass` and `cookie_expires_at` are
top-level fields, not `response` children; their observed values and their
deliberately limited interpretation are in section 3.4.

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

**Client verification route — `[T1]`, resolved to POST**

The method conflict recorded before 2026-09-28 is closed. The 2026-08-25
`auth.suno.com` export observed this route **five times**, every time as
`POST`, every time answering `204 No Content`:

```http
POST /v1/client/verify?__clerk_api_version=…&_clerk_js_version=…
Content-Type: application/x-www-form-urlencoded

captcha_token=<redacted>&captcha_widget_type=invisible&captcha_action=heartbeat
```

The observed calls were spread roughly 1–8 minutes apart with no other traffic
between them. Its purpose is a **captcha-gated Clerk session heartbeat**, which
three independent observations agree on: the form field is literally
`captcha_action=heartbeat`; the instance's `display_config` carries
`captcha_heartbeat: true` with `captcha_provider: turnstile` (section 3.4); and
the call cadence is periodic keepalive traffic.

`204 No Content` with no response body settles the safety question
independently of any prose: **this route cannot return a bearer, so it cannot
be a refresh route or a generic "verify bearer" call.** Keep the earlier
caution and treat it as proven rather than merely prudent: do not repurpose it
as a generic preflight, do not branch on `204` as success for anything else, and
do not synthesize a heartbeat request to keep a session alive. It stays
`not-in-code` (section 3.2) — captured, understood, deliberately not called.

The captcha it carries is the Clerk-auth Turnstile system, not the `suno.com`
web sign-in hCaptcha and not the generation captcha; all three are separated in
section 3.4.

`/v1/verify` is a **different route**. Nothing in this resolution says anything
about it: it remains unobserved with its own `[VERIFY]` conflict-register entry.
Do not merge, alias, or cross-promote the two.

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

The **provider behind `/api/c/check` is not captured and is deliberately left
unstated** here. It is a third captcha surface, separate from the Clerk-auth
Turnstile system and the `suno.com` web sign-in hCaptcha; the three are
separated in section 3.4. A token from either of those two is not evidence
about this route, and a generation submission is not unblocked by either
system being understood.

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
The lead inventory that still claims them is in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md).

## 6. Errors, statuses, and rate limits

### 6.1 What the captures establish

- The 2026-08-25 Studio/auth session primarily captured successful 200
  responses, several 204 responses, and OAuth/handshake 302 redirects.
- The 2026-08-25 `auth.suno.com` export added two directly observed status
  shapes: the handshake and both Google social legs answered **302** with a
  `Location`, and the captcha-gated heartbeat answered **204 No Content** with
  no body (section 5.1). A 204 there is the strongest single proof that the
  route returns no credential, because there is no body to return one in.
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

## 8. Provenance and future-capture maintenance

Source provenance, SHA-256 hashes, artifact retention rules, the reviewed-source
inventory, the host-filename misspell reconciliation, and the maintenance rule
for every future capture are owned by [`raw/README.md`](raw/README.md). They are
not duplicated here.

## Appendix A — explicit conflict register

Moved out of section 1 on 2026-09-26. This register is the master's
authoritative record of *unresolved* subjects, so that a `[VERIFY]` row living in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) is never read as an open question with
no owner.

| Subject | Conflicting claims | Canonical resolution | State |
|---|---|---|---|
| Clerk session-token exchange | Older material rejected `/tokens`; the 2026-09-24 export directly captured both `POST /v1/client/sessions/{sid}/tokens` and `POST .../touch`. | Both are `[T1]` observed same-host session routes. `/tokens` returned a top-level `jwt`; `touch` returned client/session envelopes. `/tokens/api` remains unobserved. Which route a client should prefer, and whether either is universally required, remains `[VERIFY]`. | **Resolved coexistence; selection `[VERIFY]`** |
| `/v1/client/verify` | Older auth material disagreed between GET and POST. | The 2026-08-25 `auth.suno.com` export observed `POST` five times, all `204 No Content`, with a `captcha_action=heartbeat` form body. It is a captcha-gated Clerk session heartbeat and, having no body, cannot return a bearer. Not-in-code, and not a refresh or generic-preflight route. | **Resolved for method and purpose** |
| `/v1/verify` | Older auth material disagreed between POST and GET. | No reviewed capture establishes either method or response contract. The 2026-08-25 `auth.suno.com` export resolved `/v1/client/verify` and touched nothing here; the two routes stay separate. | **Unresolved `[VERIFY]`** |
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
| Following-feed pagination | The first-page request/response is captured; no next-page token or cursor was captured. | First page stays `[T1]`; later-page behavior is `[VERIFY]` and must not be synthesized. | **Unresolved `[VERIFY]`** |
| Server-side library search | No reviewed request contains `searchText`. | Local filtering only. Any remote-search field is a `[LEAD]`. | **Unresolved** |
| Route preference across Clerk routes | Both `tokens` and `touch` are captured; neither is proven sufficient alone. | No automatic fallback order; a client must not synthesize a hybrid. | **Unresolved `[VERIFY]`** |
| Captcha provider behind `POST /api/c/check` | The Clerk auth captcha and the `suno.com` web sign-in UI captcha are captcha-gated but use different providers, which invites reading one route's provider as another's. | Clerk auth is Cloudflare Turnstile and the web sign-in UI is hCaptcha, both `[T1]` (section 3.4). The provider behind `/api/c/check` is **not captured** and is deliberately unstated. Do not infer it from either, and do not treat understanding the Clerk captcha as progress on generation. | **Unresolved — provider uncaptured** |
| Session identifier source | The implementation re-derives the active session id from the access token's `sid` claim instead of selecting `response.last_active_session_id`, which reads as a deviation from the section 3.1 selector rule. | In the 2026-08-25 capture the envelope's `last_active_session_id`, `sessions[0].id`, the `sid` claim, and the `touch` `{sid}` segment are all the same value, which **corroborates** the substitution. That is one capture with a single session, and the instance reports `single_session_mode: true`, so equivalence for a multi-session account is unproved. Select from the envelope; do not assume array position. | **Corroborated single-session; multi-session unproved** |
| Acceptance of a non-`suno.com` handshake `redirect_url` | Both captured handshake `redirect_url` values were `https://suno.com/…`, so it is unclear whether Clerk validates or honours an arbitrary absolute target. | Not this file's question. The handshake contract is captured in section 3.3; the loopback gate and its five proof requirements are owned solely by [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) and are unchanged. | **Unresolved — gate held** |
