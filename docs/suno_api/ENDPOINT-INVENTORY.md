# Suno API Canonical Endpoint Inventory

**Status:** Canonical API-spec master, consolidated 2026-09-24; non-contractual
material extracted 2026-09-26; 2026-08-25 `auth.suno.com` export folded in
2026-09-28; contradictions and staleness corrected 2026-10-05
**Current evidence baseline:** the 2026-09-24 Burp export reviewed on 2026-09-24, reconciled with the 2026-08-25 Burp corpus — including the 13-item `auth.suno.com` export folded in on 2026-09-28 — the 2026-09-22 sanitized OAuth recon, the 2026-09-23 browser HAR, and the 2026-09-28/30 sanitized recon where it directly captured a request or response (section 1.2, item 5)
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
semantics, and runtime notes. It also owns **capture methodology** — the probe
verdict vocabulary and the server-manufactured error shapes that must not be
read as wire format ([§1.5](#15-capture-methodology--probe-verdicts-and-three-shapes-the-server-manufactures)) —
and the record of surfaces that are **known and deliberately excluded**
([§5.10](#510-known-surfaces-that-are-deliberately-excluded)), so that absence is
never misread as an oversight. Within authentication it also owns the
**captured credential shapes** ([§3.3](#33-captured-clerk-credential-shapes--t1))
and the **captured Clerk instance and captcha configuration**
([§3.4](#34-captured-clerk-instance-and-captcha-configuration--t1)) — that is
where the token hierarchy, cookie attributes, enabled sign-in strategies, and
the two-captcha-systems split live.
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) owns
every `[LEAD]`/`[VERIFY]` observation.
[`THIRD-PARTY-IMPLEMENTATIONS.md`](THIRD-PARTY-IMPLEMENTATIONS.md) owns the
register of other clients' API material — what each source claims, what it may
and may not settle, and the hosts it names that are deliberately **not**
allowlist candidates.
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
| `[LEAD]` | Found in a JS/HTML scan, raw endpoint dump, old inventory, reconstructed path, prose without a direct capture, or another client's source. An unlabeled old row is always a lead. | Research and capture planning only. |
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
5. The 2026-09-28/30 sanitized recon governs the contracts it **directly
   captured** — the `/api/c/check` response body (section 5.4), the 40-entry
   `media_urls[]` observation (section 5.3), the recovered write-request schemas
   (section 5.9), the server-flag and gating-plane counts (section 7), and the
   probe verdicts in section 1.5. Where it is a static bundle or route-string
   observation it is `[LEAD]`, and where it merely read a host name out of a
   client bundle it is `[LEAD]` even though the string looks like a contract.
6. Corrective commit `678be76` and the current endpoint map are implementation
   references, not evidence that can override a newer direct capture.
7. Raw scans, decoded frontend bundles, and old topic prose supply `[LEAD]`
   inventory only. External SDK folklore, constructed media paths, and
   client-side flag names cannot promote a route above `[LEAD]`.
8. **Another client's source is `[LEAD]` by definition, in both directions.**
   A third-party implementation that reports live traffic against its own
   account has observed *its* corpus on *its* date. That is not a capture by
   this repository, so it cannot promote a row into this file, and it cannot
   demote one out of it — including where our own local bytes are currently
   unverifiable (see [`raw/README.md`](raw/README.md)). Agreement between two
   sources is not a stronger grade; disagreement is not a weaker one. A
   third-party claim can do exactly three things: add a `[LEAD]` capture
   target, add an Appendix A conflict row, or be recorded as a design
   reference. The registered sources, and what each one may and may not
   settle, are in
   [`THIRD-PARTY-IMPLEMENTATIONS.md`](THIRD-PARTY-IMPLEMENTATIONS.md).

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

**One captured-fact mismatch belongs here rather than in a backlog, because it
is the current boundary.** Download/playback requires
`content_type == "mp3"` on the `audiopipe.suno.ai` streaming origin; the
2026-09-30 recon observed `m4a-opus` on the CloudFront progressive origin in
**40 of 40** `media_urls[]` entries and no `mp3` at all (section 5.3). The
selection filter therefore rejects every observed media URL. This is a
**boundary statement, not a defect claim**: the two `[T1]` observations come
from different corpora and both are recorded, so the client is fail-closed
against the form it has not captured. It is recorded here because "the client
requires mp3" and "mp3 is the normal case" cannot both be true, and the master
previously implied the latter. Widening the accepted set requires capture
evidence and is not implied by this note.

**The likelier resolution is a route we have never captured, not a wider filter
on this one.** A download-*preparation* family — a preparation call returning a
short-lived signed URL, separate from the playback array — is registered as
`[LEAD]` in [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) section 4.6 and discussed
in [`THIRD-PARTY-IMPLEMENTATIONS.md`](THIRD-PARTY-IMPLEMENTATIONS.md). If it is
real, then "the client requires mp3 in `media_urls[]`" is not a filter that is
too narrow but a selection from the wrong source. **Capturing that family is the
next step; loosening `selectDownloadUrl()` is not.** Widening the accepted set
without a capture would keep the fail-closed property only by accident.

### 1.5 Capture methodology — probe verdicts, and three shapes the server manufactures

The 2026-09-28/30 recon added a method the earlier corpora did not use: probing
routes and **reading server validation errors as schema evidence**. The method is
sound and high-value — it is how a whole write surface was recovered without
creating anything. It also has three traps, and they are recorded here because
the failure mode is silent: each one yields output that is shaped exactly like a
captured contract and is wrong anyway.

**Validation-error recovery is legitimate; the error's own shape is not.** An
empty body against a write route returns `422` listing required field names,
expected types, and complete enum members. That is how the mutation contracts in
section 5.9 were recovered, and it is `[T1]`-grade. But:

- **The `loc` chain is fabricated.** A write schema probe reports
  `loc=['body','spec','is_public']` for a visibility call, yet a nested
  `{"spec":{...}}` body still fails *Field required* while the **flat**
  `{"is_public": …}` passes validation and reaches the handler. The `loc` array
  is a renamed projection of the error, not the wire format. **Read every
  recovered write schema as flat**, and never reconstruct a client payload by
  walking `loc`.
- **`detail` is a string containing a Python `repr()`**, not the RFC 9457 array
  the field name implies. It breaks any machine parser that expects structured
  validation problems.

**Route registration must be judged by `x-matched-path` equalling the requested
path, never by status.** The platform returns `x-matched-path: /` for many
paths that do not exist, because they fall through to the root layout and render
the homepage with a **`200`**. A naive `200 → EXISTS` rule marks nonexistent
staff paths as live. A 404 is not the only dead answer either: the verdict
vocabulary observed is

| Probe result | Verdict | What it establishes |
|---|---|---|
| `401` unauthenticated | existence only | A handler is registered. **No request and no response body was seen**, so this is exactly the `[LEAD]` definition reached by a better oracle — never promoted to `[T1]`. |
| `404` **with** a JSON body | route registered, resource missing | The *resource* handler answered `{"detail": "…"}`. The route is live. |
| `404` with `text/html` | route not registered | A genuine absence. |
| `405` on `GET`/`OPTIONS` | registered, POST-only | Existence, no contract. |

**Four routes first recorded as dead were `{param}` path-template artifacts** of
the prober's own path list, and were reclassified as live on re-probe. A 404 from
a naive probe is weak evidence, which is the same lesson section 6.2 already
states for client-side routes.

**Host enumeration is not a name pattern.** `studio-api-beta`, `studio-api-dev`,
`studio-api-preview`, `studio-api-canary`, and bare `studio-api` do not resolve.
Staging is the only live non-production tier, and it answers **unauthenticated** —
which is a disclosure, handled in the responsible-disclosure artifact, not a
client configuration to adopt.

## 2. Base URLs and request conventions

### 2.1 Hosts

| Host | Role | Evidence |
|---|---|---|
| `https://suno.com` | Web application and OAuth final destination. | `[T1]` Burp/HAR/recon |
| `https://auth.suno.com` | Current Clerk host; catalog auth paths are host-relative. | `[T1]` 2026-08-25 and 2026-09-24 Burp; 2026-09-22 recon |
| `https://studio-api-prod.suno.com` | Primary Studio API host. | `[T1]` 2026-08-25 and 2026-09-24 Burp; 2026-09-23 HAR |
| `https://suno-uploads.s3.amazonaws.com` | Temporary direct multipart upload target returned by the audio-upload initializer. | `[T1]` 2026-09-24 request/response |
| `https://cdn1.suno.ai`, `https://cdn2.suno.ai` | Captured image asset hosts. | `[T1]` response fields and direct requests, 2026-09-24 |
| `https://d2lwuy8qc234o3.cloudfront.net` | Captured progressive-media host, and the **only** host observed for `media_urls[]` in the 2026-09-30 recon (40 of 40 entries). | `[T1]` response values in captured clip objects (2026-09-24); sole observed progressive host (2026-09-30) |
| `https://audiopipe.suno.ai` | Captured streaming-audio host. **Zero** occurrences as a captured response value in the 2026-09-30 recon. | `[T1]` response values in 2026-09-24 clip objects — a **different corpus** from the recon; see section 5.3 |
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
| Generation — `POST /api/c/check` (section 5.4) | **Cloudflare Turnstile, version `2`.** `[T1]` | The 2026-09-30 recon **captured the response body**: `POST /api/c/check` with `{"ctype":"generation"}` answered `200` with `{"required": <bool>, "captcha_version": 2}`. The provider follows from `captcha_version` against the server's map — `1` = hCaptcha, `2` = Turnstile — so this is a directly observed response contract, not a bundle inference. Corroborating client constants `TURNSTILE_GENERATION_CAPTCHA_VERSION = 2` and `HCAPTCHA_GENERATION_CAPTCHA_VERSION = 1` agree but remain `[LEAD]` on their own; `FORCE_ENABLE_CAPTCHA = false` is a bundle default, `[LEAD]`, and not a promise for any account. Still uncaptured: any sitekey, token lifetime, widget mode, or token format for this route. **`required` is per-account, not universal.** The one captured call returned `required: false`; that records what *that* account was told, and a decision of "above a trust threshold" is the plain reading of a per-account `false`. It is not a guarantee for any other account, not a promise for future calls, and **not authorization to solve a challenge** — automating a captcha is a terms-of-service decision for a human to make, never an engineering inference from a `false`. |

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

Billing **mutations are captured as routes and deliberately excluded** —
`cancel-sub`, `cancel-sub/undo`, `change-plan`, `change-plan/preview`,
`pause-sub`, `unpause-sub`, `accept-sub-coupon`, and
`auto-reload/enable|disable`. Their request schemas are **not** captured: the
2026-09-30 recon confirmed each as POST-only (`405` on `GET`/`OPTIONS`) and
deliberately did not probe their bodies. See section 5.10 for why absence from
the client is deliberate. Checkout, portal, payment-method, and survey routes
remain uncaptured.

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

Project **read** operations are `[T1]`. Project **mutation** operations are not: the
2026-09-28 recon establishes that the following are **registered** — create-project,
save-project, render-state, render-state-multitrack, `project_revision/{id}/clone`,
`create-or-load-project-for-clip/{id}`, project trash, project clips, collaborators,
collaborator-me, invite, and project metadata. Existence is `[REGISTERED]`, not `[T1]`:
most were read from the client bundle rather than probed, so no request or response
shape is observed. **No project creation, collaboration, or Studio save/render contract
is captured**, and none of these may be wired on existence alone. The consequence for
this client is recorded in the backlog: the music-video workspace surface is gated on
our own wiring, not on Suno's API.

### 4.4 Generation, lyrics, prompts, and analysis

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| POST | `/api/c/check` | Bearer | Captcha requirement check for generation; returns `required` and `captcha_version` | `[T1]` Burp 2026-08-25; request/response body recon 2026-09-30 | declared-unused |
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
| POST | `/api/gen/{gen_id}/set_visibility/` | Bearer | Set a clip's public/private visibility; body `is_public: boolean` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/clips/{clip_id}/toggle_remixes/` | Bearer | Toggle remix permission; body `can_remix: boolean` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/clips/{clip_id}/toggle_show_remixes` | Bearer | Toggle remix-list visibility; body `show_remix: boolean` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/gen/{gen_id}/update_feedback_state/` | Bearer | Record a feedback state; body `feedback_reason: string` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/share/link` | Bearer | Mint a share link; body `content_id`, `content_type` (both strings) | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/playlist/set_metadata` | Bearer | Rename/describe a playlist; body `name`, `description`, `playlist_id` (all strings) | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/playlist/update_clips/` | Bearer | Add/remove/reorder playlist clips; body `playlist_id`, `metadata`, `update_type` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/playlist/v2/{id}/tracks/add` | Bearer | Append 1–100 clips by id | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/playlist/v2/{id}/tracks/remove` | Bearer | Remove 1–100 clips by id | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/playlist/v2/{id}/tracks/reorder-by-index` | Bearer | Reorder by index; body `positions` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |
| POST | `/api/playlist/v2/{id}/cover-image` | Bearer | Set playlist cover; body `id`, `type` | `[T1]` validation-probe contract 2026-09-30 | not-in-code |

No reviewed capture establishes server-side search as a `searchText` field on
`POST /api/feed/v3`. Search claims derived from old prose or client parameters
remain `[LEAD]`; clients must use local filtering until a direct request/response
capture proves the remote search contract.

**Library and playlist mutations are captured, but only their request side.**
The rows above carry **request** contracts recovered by schema probe
(section 5.9). Their **response** shapes are not captured, and four
commonly-assumed sibling routes were never probed at all — `playlist/create/`,
`playlist/trash/`, `gen/{id}/set_metadata/`, and `gen/{id}/update_reaction_type/`
are **client-bundle route strings only**, `[LEAD]` on both axes, and
`POST /api/clips/delete/` answered `404` when exercised as a POST. None of these
may be wired on the strength of a neighbouring row.

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
captured, so later-page behavior remains `[VERIFY]` (section 5.5). Follows and
song-copy remain uncaptured; **share-link creation is captured** — see the
`/api/share/link` row in section 4.5 — and comments remain uncaptured.

**A static negative that contradicts client corroboration — read before trusting
`wired` on these rows.** Two independent passes over roughly 96 client-bundle
chunks (7.3 MB) on 2026-09-28 and again on 2026-09-30 returned **zero**
occurrences of any `/api/notification*` route. The only `notification` strings in
that corpus are i18n keys, plus `notification-feed-v3-enabled` and
`notification-settings`.

That is a **static negative, and it is not proof of removal.** The
section 6.2 rule — absence from the current bundle is evidence of client-side
disuse, not of server removal — applies **symmetrically**, and it is the reason
these three rows stay `[T1]`: the 2026-08-25 and 2026-09-24 exports are real
observed request/response pairs. Nothing here demotes them, and
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) may not be used to argue them away.

What it does change is the **risk posture**. Three `wired` live calls now sit on
a surface with zero current client corroboration, on a Notifications surface a
user can open at any time. So the recorded handling is deliberate: **do not
delete `SunoNotificationService` on a static negative, and do not present the
surface as working.** A dead route should degrade to a visible error state, so
the failure is diagnosable rather than an empty list that reads as "no
notifications". One authenticated probe settles it.

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
| Progressive media | `media_urls[]` | Each observed audio item has `url`, `content_type`, `delivery`, and `encoding`. **This array is a playback authority, not an established download contract** — see the correction below. |
| Observed media taxonomy | `m4a-opus` / `progressive` on the CloudFront host | **The only progressive combination observed in the 2026-09-30 recon: 40 of 40 entries, no exceptions.** `mp3`, `webm-opus`, and any `streaming` delivery were **not** observed there. `mp3` and `webm-opus` over `streaming` remain `[T1]` *response values in the 2026-09-24 clip objects* — a different corpus — and are not contradicted, but they are not the normal case and must not be documented as one. |
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

**What the 2026-09-30 recon observed, and why it matters — `[T1]`.** Across two
authenticated captured feed bodies there are **40 `media_urls[]` entries with zero
exceptions**:

| Field | Observed value | n / 40 |
|---|---|---|
| host | the captured CloudFront host | 40 |
| `content_type` | `m4a-opus` | 40 |
| `delivery` | `progressive` | 40 |
| `encoding` | `1.0.0` | 40 |

`mp3` occurs **zero** times in those bodies, and `audiopipe.suno.ai` occurs
**zero** times anywhere in that corpus's captures. Two distinctions matter and
neither is a demotion:

- **Corpus, not host, is the axis.** The `audiopipe.suno.ai` rows and the
  `mp3`/`webm-opus`-over-`streaming` values above are `[T1]` from the **2026-09-24
  Burp corpus**. The 40-entry observation is from the **2026-09-30 recon**. Two
  captures of different accounts at different times disagree about which
  progressive form is served, and a client must handle both. Neither `[T1]` is
  deleted by the other; the recon simply makes clear which one is the *observed
  default* and which is a real-but-less-common branch.
- **A bundle string is not a capture.** `audiopipe.suno.ai` does appear in the
  recon corpus — but only inside client-bundle literals: a hostname regex that
  also matches an `audiopipe-dev` variant, and a **constructed** URL template.
  That is `[LEAD]` under section 1.1 and is precisely the
  construct-from-ID fallback the paragraph above forbids.

**`media_urls[]` is playback; downloading is a separate contract we have not
captured.** The table above originally read "this array is the
playback/**download** authority". That characterization is withdrawn. What the
captures actually establish is narrower:

- `media_urls[]` is **captured** as a shape, and its observed contents are a
  progressive `m4a-opus` variant on the CloudFront host. That is `[T1]`.
- Nothing captured shows a **`media_urls[]` entry being transferred as a
  download**, or shows the server *authorizing* a save against one.
- `mp3` is **zero occurrences in 40 of 40** observed entries in the 2026-09-30
  corpus. A client that selects an `mp3` entry out of `media_urls[]` selects
  against an observed-empty set.
- The 2026-09-24 corpus separately showed legacy `audio_url` carrying the
  `/api/forbidden` sentinel. So neither the progressive array nor the legacy
  field is an established download contract.

A download-preparation route family is registered as `[LEAD]` in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) section 4.6 and summarized in
[`THIRD-PARTY-IMPLEMENTATIONS.md`](THIRD-PARTY-IMPLEMENTATIONS.md). It is
**not** in section 4 of this document, because we have not captured it. Its
existence is a capture target; its shape, its host, and the host its returned
signed URL points at are all unknown to us.

The practical consequence is a standing one: **do not treat any captured media
URL as a download contract, and do not add a host to
`src/suno/CapturedHosts.cpp` on the strength of a signed URL this repository
has never seen returned.**

**Direct consequence for the client, recorded here so it is not rediscovered as
a bug.** The download/playback layer selects an entry only when
`content_type == "mp3"` **and** the host is the `audiopipe.suno.ai` streaming
origin. Against the observed 40-entry corpus that filter rejects **100%** of
media URLs, so a captured clip can resolve to nothing. Two independent
conditions must both be true for rejection to be the explanation, which is what
makes it diagnosable: either widen the accepted `content_type` set or accept the
CloudFront progressive origin, on capture evidence, and until then say *why*
selection failed rather than failing silently. Do not widen the set from a
bundle selector; that is the `[LEAD]` path this section exists to prevent.

The constructed fallbacks `cdn1.suno.ai/{clip_id}.mp3` and
`d2lwuy8qc234o3.cloudfront.net/1/clip/{clip_id}.m4a` remain `[LEAD]` and are not
part of the captured media contract.

### 5.4 Captured generation and polling behavior

**Captcha check — `[T1]`**

```http
POST /api/c/check
Content-Type: application/json

{"ctype": "generation"}
```

`ctype` is a single-value enum; `generation` was the only observed member.
`token_provider`, where a call supplies it, is an **integer** — a string value
422s — and the captured provider map is `1` = hCaptcha, `2` = Turnstile.

A captured response:

```json
{
  "required": false,
  "captcha_version": 2
}
```

`captcha_version: 2` resolves the generation provider to **Cloudflare
Turnstile** (section 3.4). It is the same provider as Clerk authentication and a
*different* provider from the `suno.com` web sign-in UI's hCaptcha, which is why
the three surfaces stay separated there. A token from either of those two is not
evidence about this route.

**What `required: false` does and does not establish.** It is the answer that
one account received on one call. The consistent reading is a per-account trust
threshold, and that is exactly as far as it goes:

- It is **not** universal. A different account, a different tier, or a changed
  trust state can receive `true` from the same route with the same body.
- It is **not** durable. It is not cached, not sticky, and not a capability.
- It is **not** permission. Nothing here authorizes solving, replaying, routing
  around, or automatically obtaining a captcha token. That is a
  terms-of-service decision reserved to the user; a client must treat
  `required: true` as an ordinary handled branch, not as an obstacle to defeat.
- Generation submission therefore remains intentionally **not** wired
  (section 1.4). This section removes the uncertainty about *which* captcha
  guards the route; it does not remove the gate.

Do not assume captcha is required for every account/request: call the captured
flow and handle both decisions.

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

All three notification routes remain `[T1]` on capture, and carry a **static
negative** against them from the 2026-09-28/30 recon: zero `/api/notification*`
routes anywhere in ~96 client-bundle chunks. That negative is recorded in full,
including why it does **not** demote the rows, in section 4.8. Do not resolve the
disagreement by deleting the surface.

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

### 5.8 Routes whose detailed schemas remain uncaptured

The request/response split is the axis here, and it is not cosmetic. A recovered
**request** contract tells a client what to send; it says nothing about what
comes back. Two families are therefore listed separately.

**Request contract captured, response contract not captured** — writable, but a
client has no documented success shape to parse and no documented failure
envelope:

- Library and playlist mutations — see section 5.9 for the full recovered
  request set
- Billing mutations (section 5.10), which are additionally excluded on purpose
- Prompt mutation payloads beyond captured route/method evidence
- Audio-upload `initialize-clip`, processing status, generation linkage, and
  post-finish lifecycle beyond the captured three-step transport sequence
- Download preparation, ZIP export, and exact `Content-Disposition`/filename
  behaviour
- Persona/custom-model creation, training, archive, and voice verification
- Checkout, portal, payment-method, and survey surfaces

**No contract on either axis** — do not write a client against these at all:

- Project/collaboration creation and Studio save/render operations
- Lyrics generation, infill, mashup, pair, concat, merge, extend, and cover
  variants other than the captured v2-web submission
- Comments, follows, and song-copy behavior
- Feature-gate values, entitlement matrices, and client cache contents
- Orpheus chat/history/model contracts
- Validation/error envelopes generally (section 6)

For these families, capture the request and response before writing a client.
The lead inventory that still claims them is in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md).

### 5.9 Recovered library and playlist mutation request contracts — `[T1]` (request side)

The 2026-09-30 recon probed the write surface by sending an empty object to each
write route and re-sending with deliberately wrong-typed values. The server's own
validation error names every required field, its expected type, and — for enums —
the complete allowed member list. **Nothing was created**: every payload was
rejected by the validator or by a lookup on a non-existent identifier. Sixty-four
write endpoints were probed this way; the library- and playlist-facing subset is
recorded here because it closes a standing gap.

| Method | Path | Required request fields | Server-side rule not expressible in the schema |
|---|---|---|---|
| POST | `/api/gen/{gen_id}/set_visibility/` | `is_public: boolean` | **"Cannot make trashed clips public."** Observed by reaching the handler, not by validation. |
| POST | `/api/clips/{clip_id}/toggle_remixes/` | `can_remix: boolean` | — |
| POST | `/api/clips/{clip_id}/toggle_show_remixes` | `show_remix: boolean` | — |
| POST | `/api/gen/{gen_id}/update_feedback_state/` | `feedback_reason: string` | — |
| POST | `/api/share/link` | `content_id: string`, `content_type: string` | — |
| POST | `/api/playlist/set_metadata` | `name: string`, `description: string`, `playlist_id: string` | — |
| POST | `/api/playlist/update_clips/` | `playlist_id: string`, `metadata: object`, `update_type: ENUM {add, remove, remove_by_id, reorder}` | — |
| POST | `/api/playlist/v2/{id}/cover-image` | `id: string`, `type: ENUM {generated}` | — |
| POST | `/api/playlist/v2/{id}/tracks/add` | `clip_ids: array` | **`clip_ids` must hold between 1 and 100 items.** |
| POST | `/api/playlist/v2/{id}/tracks/remove` | `clip_ids: array` | **1–100 items.** |
| POST | `/api/playlist/v2/{id}/tracks/reorder-by-index` | `positions: array` | — |

Four rules govern how far these may be taken:

- **Bodies are flat.** The error's `loc` chain is a manufactured projection;
  see section 1.5. Every field above is a top-level key.
- **A required-field list is a minimum, not a maximum.** It is what the server
  rejected a request for. Unlisted optional fields may exist.
- **A `4xx` seen during probing is not a response contract.** The only response
  facts here are the *rejections*; no success body was observed for any route in
  this table.
- **Neighbouring routes are not covered.** `POST /api/playlist/create/`,
  `POST /api/playlist/trash/`, `POST /api/gen/{gen_id}/set_metadata/`,
  `POST /api/gen/{gen_id}/update_reaction_type/`, and
  `POST /api/gen/trash` are client-bundle route strings that were **not
  probed**: `[LEAD]` on both axes. `POST /api/clips/delete/` was exercised and
  answered `404`. No `DELETE` on any route was probed at all, so a `DELETE`
  spelling of any of these is entirely unproved. Existence, contract, and
  entitlement remain three independent axes (section 1.1) — a captured request
  field is not evidence that the account may perform the action.

### 5.10 Known surfaces that are deliberately excluded

Recorded so a future session does not read absence as an oversight. Each is
captured-as-a-route or known-to-exist and **not implemented on purpose**. Adding
any of them is a product decision, not a missing feature.

**Money-moving billing mutations.** `cancel-sub`, `cancel-sub/undo`,
`change-plan`, `change-plan/preview`, `pause-sub`, `unpause-sub`,
`accept-sub-coupon`, and `auto-reload/enable|disable` were each confirmed
POST-only (`405` on `GET`/`OPTIONS`) and their bodies were deliberately not
probed. They are excluded because a desktop client that can cancel a
subscription, change a plan, or apply a coupon is a support burden and a
terms-of-service surface, permanently. The read-only billing surface in section
4.1 stays.

**Legal and privacy consent writes.** `PUT /api/privacy/data-sharing-consent`
and `POST /api/user/vip_program_acceptance` write consent records on the user's
behalf. Both are route strings that were not probed. A third-party client that
can record a user's consent to data sharing or to a programme's terms carries
consequences its author cannot discharge, so neither belongs here regardless of
availability.

**Contest clip downloads.** The server flag
`remix-contest-disable-downloads` is live in production. Contest clips are
download-restricted server-side; downloading one would be circumventing a
server-enforced restriction, so no download path may be built for contest
content.

**Staff backoffice.** The `/b-side/*` surface (84 routes, including an
impersonation route) is absent from both the deployed page set and the API
surface — authorization by non-deployment. It is responsible-disclosure material,
never a surface to build on.

For all of the above: existence was observed, contract was not, entitlement is
not ours to grant, and the correct client behaviour is to leave the switch off.

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
| Captured model identifiers | The captured model keys are **v6 `chirp-hawk`**, **v6-wild `chirp-hawk-wild`**, **v6-mini `chirp-goose`**, and remaster **`chirp-halibut`**, with `major_model_version` matching the tier. This supersedes any older claim that v3.5 is *the* captured model — v3.5 is one tier among several, and the captured account's own default remaster model reported `can_use: false`, a per-account entitlement distinct from `PlanFeature.Remaster`. | `[T1]` response values, 2026-09-30 |
| Credit arithmetic | Captured client constants put `CREDITS_PER_SONG = 5` while `canGenerateSong` requires at least 10 credits, and `CUSTOM_MODEL_CREDIT_COST = 100`. The threshold is therefore **not** the per-song cost; do not derive "enough credits for one song" from the cost alone. | `[LEAD]` client constants — runtime data, not a contract |
| Model duration/stem limits | Do not hardcode values from old V3/V4/V5/V5.5 summaries. Numeric limits in the session/billing catalog are account- and model-dependent runtime data. One asymmetry is captured and worth not flattening: `max_neg_tags` is 1000 on **every** model, while `max_tags` caps at 200 on v4 and below. | `[T1]` numeric catalog fields; `[LEAD]` constants; old hardcoded lists superseded |
| Prompt/title limits | Do not hardcode old character maxima. `models[].max_lengths.title`, `prompt`, `tags`, `negative_tags`, and `gpt_description_prompt` were captured as JSON integers; validate defensively before converting to application integers. | `[T1]` session shape; values are runtime data |
| Generation status | `submitted` and `complete` were observed. This is not the complete status vocabulary. | `[T1]` feed/generation captures |
| Polling | Captured completion was observed through feed/get-songs; no interval is canonical. | `[T1]` behavioral capture |
| Feature gates | Statsig routes are captured. Gate names/defaults/local-storage values from scans are leads, not server authorization. | `[T1]` route; `[LEAD]` values |
| Server feature flags | `GET /api/session/` returns a `flags` map that needs **no credentials**: **47** flags in production. The same corpus measures **58** on the staging tier. The machine-readable drift record, the findings write-up, and the disclosure all agree on **47 / 58**; the corpus's own long-form `endpoints.md` §6d figure of **51 is stale** and is superseded by the three that agree. Do not quote 51. Names captured include `studio`, `studio-v2`, `studio-clip-editor`, `editing-stems`, `generative-stems`, `stem-dryer`, `negative-tags`, `video-to-song`, `crop-remove`, `control-sliders`, `max-mode`, `remix`, `playlists`, `song-duration-control`, `agentic-simple`, `contest-hub`, `v3_alpha`, `v3p5`, `custom-model-v6-cutover`, and `remix-contest-disable-downloads`. | `[T1]` response values, 2026-09-30 |
| Gating planes | **At least seven independent planes** decide what a given account may see or do; none can be flipped client-side and the server enforces entitlements separately. See the note below. | `[T1]` response values; one plane `[LEAD]` |
| Billing/plan matrix | Billing exposes numeric credit, usage, download, upload, voice, agentic, and custom-model limits. Values remain account/time dependent; parse integer and floating-point fields according to their names, never as booleans or fixed plan constants. | `[T1]` Burp 2026-09-24 shape; entitlement interpretation remains runtime data |
| Upload limits | Billing returned numeric `audio_upload_limits.min` and `.max`; the old fixed two-minute claim remains non-canonical. Apply account/runtime limits rather than a hardcoded duration. | `[T1]` numeric shape 2026-09-24; fixed duration superseded |
| Stem counts | The old “12 stems” claim is not canonical without model/catalog or processing capture. | `[LEAD]` |
| Video status | The captured video-status response exposed `status` and `video_url`, but one observed `complete` value with an empty URL is not a universal success rule. Clip-level media remains distinct. | `[T1]` route; interpretation conservative |
| Realtime | `GET /api/realtime/discover` returned stream/auth metadata. Do not log the returned token material. | `[T1]` |
| Cookies/analytics | Clerk cookies establish browser auth; analytics cookies do not grant Studio authorization. | `[T1]` auth request context |

**The seven gating planes.** Listing them is what stops a client from reading one
source of truth as the whole answer; each has been the sole apparent answer in
some earlier pass.

| # | Plane | Observed | Evidence |
|---|---|---|---|
| 1 | Server `flags` on `/api/session/` | 47 unauthenticated; a larger set when authenticated (57); 58 on staging. `roles` = `is_day_zero_user`, `pro`, `staff`, `unlimited_credits`. The unauthenticated/staging split is a disclosed exposure, not a client strategy. | `[T1]` response values |
| 2 | Statsig | 1,311 gates and 606 dynamic configs, with **no ID→name mapping**. The names are opaque numeric IDs, so "gate X controls feature Y" is unsupportable from this data and must not be written down as though it were. | `[T1]` counts; `[LEAD]` semantics |
| 3 | ParameterStore | A client-side `Parameter`/`ParameterStore` const-enum: 60 parameters across 6 stores. | `[LEAD]` client const-enum |
| 4 | `PlanFeature` | 22 server-supplied entitlements, delivered per plan. | `[T1]` response values |
| 5 | `access_group_attrs` on `/api/user/metadata` | Per-account cohort membership. The captured account carried `is_voices_early_access: "true"`. | `[T1]` response value |
| 6 | `experiments` on `/api/session/` | A first-class server experiments plane, **distinct from both Statsig and `flags`**. Captured as an empty object for the authenticated account and `null` unauthenticated — an empty plane is not an absent plane. | `[T1]` response values |
| 7 | **`configs.gen-endpoint`** | The server states which generation route to call: the captured value was `/api/generate/v2-web/`, the same path the client's own hardcoded constant uses. | `[T1]` response value |

Plane 7 is recorded here because **the server telling the client which route to
use is not written down anywhere else**, and it is the cheapest one to miss: a
client that hardcodes the path looks correct today and can be moved onto a
different generation pipeline server-side with no client change. That makes it a
plausible mechanism for a staged rollout, and it is a better thing to watch than
any individual endpoint. Read it at runtime; do not treat the captured value as
a constant.

Two consequences, both load-bearing. **This is visibility, not access** — it
explains why a capability is absent instead of silently omitting it, and it is
not a bypass. And **a same-named flag and a same-named community-documented
generation parameter means the parameter is first-party**: that coincidence is a
usable attribution rule, and it applies to several previously-unverified claims.

The corpus's own enumeration also records a further dimension rather than an
eighth plane: a client-identity header changes which model catalogue
`/api/session/` serves. That is **not** an entitlement bypass — the captured
account independently already held the newer-model rights — and it is noted here
only so nobody mistakes a client-keyed catalogue for a gated one.

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
| Captcha provider behind `POST /api/c/check` | Three incompatible statements coexisted: this row said "not captured", section 3.4 said "resolved to `[LEAD]`: Cloudflare Turnstile v2", and section 5.4 said "deliberately left unstated". The two other captcha surfaces being separately resolved is what invited reading one route's provider as another's. | **Resolved to `[T1]`: Cloudflare Turnstile, version `2`.** The 2026-09-30 recon captured the response body of `POST /api/c/check` — `200` with `{"required": <bool>, "captcha_version": 2}` — and `captcha_version` maps to a provider directly (`1` = hCaptcha, `2` = Turnstile). The bundle constants that previously carried this as `[LEAD]` were corroboration, not the basis. Clerk auth remains Turnstile `[T1]` and the `suno.com` sign-in UI remains hCaptcha `[T1]`; this third route is Turnstile by its own captured response. The captured `required: false` is one account's trust-threshold answer — not universal, and not authorization to automate a challenge. | **Resolved for provider; per-account `required` remains per-account** |
| Session identifier source | The implementation re-derives the active session id from the access token's `sid` claim instead of selecting `response.last_active_session_id`, which reads as a deviation from the section 3.1 selector rule. | In the 2026-08-25 capture the envelope's `last_active_session_id`, `sessions[0].id`, the `sid` claim, and the `touch` `{sid}` segment are all the same value, which **corroborates** the substitution. That is one capture with a single session, and the instance reports `single_session_mode: true`, so equivalence for a multi-session account is unproved. Select from the envelope; do not assume array position. | **Corroborated single-session; multi-session unproved** |
| Acceptance of a non-`suno.com` handshake `redirect_url` | Both captured handshake `redirect_url` values were `https://suno.com/…`, so it is unclear whether Clerk validates or honours an arbitrary absolute target. | Not this file's question. The handshake contract is captured in section 3.3; the loopback gate and its five proof requirements are owned solely by [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) and are unchanged. | **Unresolved — gate held** |
| Server feature-flag count | The corpus's long-form `endpoints.md` §6d states 51 server flags; its own machine-readable drift record, findings write-up, and disclosure all state **47** production / **58** staging. | **47 / 58.** Three independent artifacts agree and one does not, so the outlier is the stale figure: quote 47. The corpus's `endpoints.md` is not edited here — it is a captured artifact outside the repository, and a capture is evidence, not a file this project maintains. | **Resolved — 47, not 51** |
| `media_urls[]` as the download authority | This document's section 5.3 called the array "the playback/**download** authority". A third-party client (`suno-cli` @ `6d28c67a`) and independently the 2026-09 bundle corpus both describe a **separate** download-preparation contract: a preparation route returning a short-lived signed URL, with the legacy `audio_url` as the fallback. | **Our captures stand and no `[T1]` is demoted.** What the captures establish is the array's *shape*, not its fitness for download: `mp3` is zero occurrences in 40 of 40 observed entries, and the legacy field carried a `/api/forbidden` sentinel in the 2026-09-24 sample. The "download authority" wording was an interpretation layered on a shape and is withdrawn in section 5.3. The preparation family stays `[LEAD]` in [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) until captured — and per section 1.2 item 8, a third-party claim cannot promote it either way. | **Unresolved — capture stands; characterization corrected** |
| `/api/edit/stems/{clip_id}` trailing slash | `suno-cli` @ `6d28c67a` spells it **without** a trailing slash; our lead rows carry a trailing slash. No capture from this repository selects either spelling. | Neither spelling may be rewritten into the other. This is a spelling conflict, not a retitling — path template *parameter names* (`{id}` vs `{clip_id}`) are **not** on the wire and are not drift, so they are deliberately not recorded here. | **Unresolved `[VERIFY]` at use time** |
| Progressive media host and content type | The 2026-09-24 clip objects returned `mp3`/`webm-opus` over `streaming` on `audiopipe.suno.ai`, while the 2026-09-30 feed bodies returned `m4a-opus` over `progressive` on the CloudFront origin in 40 of 40 entries with no `mp3` and no `audiopipe` occurrence. Two captures of two accounts disagree about the default. | **Both observations stand as `[T1]`**; they are different corpora, not a correction of one another. The recon's 40-entry observation is the only evidence for which form is *observed as the default*, and the client's `mp3`-plus-`audiopipe` filter therefore rejects 100% of it (section 5.3). `audiopipe.suno.ai` does appear inside the recon's client bundles as a hostname regex and a constructed URL template, which is `[LEAD]` and does not affect its `[T1]` row in section 2.1. | **Unresolved which is served to a given account; both captured** |
| Notification routes vs current client bundle | `/api/notification/v2`, `/v2/badge-count`, and `/v2/read` are `[T1]` from the 2026-08-25/2026-09-24 request-response exports and all three are `wired`, yet two passes over ~96 client-bundle chunks found **zero** `/api/notification*` strings. | The captures are real, so the rows stay `[T1]` and nothing is demoted: section 6.2's rule that absence from the bundle is evidence of client-side disuse rather than server removal applies symmetrically. What the negative changes is handling, not status — degrade a failing notification surface to a visible error state rather than deleting the service on a static negative, and do not present it as working. One authenticated probe settles it. | **Unresolved — captured routes, uncorroborated by the current client** |
| Library mutation request contracts | This master and the backlog both treated library and playlist mutations as needing a fresh human capture. | **Request** contracts are `[T1]` — recovered from server validation errors, 64 write endpoints probed, and the library/playlist subset is canonical in section 5.9. **Response** shapes were not captured, and `playlist/create/`, `playlist/trash/`, `gen/{id}/set_metadata/`, `gen/{id}/update_reaction_type/`, and `gen/trash` were never probed and remain `[LEAD]`; `POST /api/clips/delete/` answered 404. Recovered bodies are **flat** (section 1.5). | **Resolved for requests; responses and unprobed siblings remain uncaptured** |
