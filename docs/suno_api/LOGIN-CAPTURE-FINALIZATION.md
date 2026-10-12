# Login Capture Finalization

Working record of what the current Suno login captures provide and whether the
ChadVis client (`src/suno/auth/`) has enough to reach a wire-ready API
authentication path. This document lives alongside `OAUTH_REDIRECT_ANALYSIS.md`,
`ENDPOINT-INVENTORY.md`, and `OBSERVED-LEADS.md`. It replaces the original
Open Sniffer-only analysis below with findings from the second HAR recording
(`scratch/auth-flow/suno.com_auth-login-flow.har`).

## Evidence status

| Grade | Meaning |
|---|---|
| `[T1]` | Directly observed request/response pair. |
| `[LEAD]` | Inferred from client bundle, sibling route, or unverified source; not a standalone contract. |
| `[VERIFY]` | Conflicting or incomplete; must be resolved by a new capture before wiring. |

Two captures are in scope:

1. **Open Sniffer** (`0c3452dd-cf11-42a1-bc58-e6e663fb0bd0.json`) — request-only:
   request URLs and POST bodies, **no response bodies, no status codes, no
   `Set-Cookie` headers, no `Cookie` request headers**.
2. **New HAR** (`scratch/auth-flow/suno.com_auth-login-flow.har`, 84 auth-related
   entries, ~1379 total) — full request + response including response bodies,
   status codes, and redirect `Location` headers. However, **the HAR tool stripped
   all cookie values**: zero `Set-Cookie` response headers and zero `Cookie`
   request headers appear anywhere, despite responses being clearly
   authenticated (the `GET /v1/client` response returns a full session envelope).

## What the captures provide

### Clerk client lifecycle

| Route | Open Sniffer | New HAR | Resolution |
|---|---|---|---|
| `POST /v1/client/sign_ins` | Body misattributed (GitHub beacon) | `[T1]` — body captured | True body recovered (below) |
| `GET /social/login/google-oauth2/` | Not observed | `[T1]` 302 → Google OAuth | Server-side redirect, not loopback |
| Google OAuth (`accounts.google.com`) | Not observed | `[T1]` full chain | Standard auth-code flow |
| `GET /social/complete/google-oauth2/` | Not observed | `[T1]` 302 → `suno.com/create` | Google code arrives here, **server-side** |
| `GET /v1/client` | Request only | `[T1]` full response | Session envelope captured |
| `POST /v1/client/sessions/{sid}/tokens` | Request only | `[T1]` full response | Bearer JWT captured |
| `POST .../sessions/{sid}/touch` | Request only | **Not observed** | `[LEAD]` only |
| `DELETE /v1/client` (sign-out) | Not observed | **Not observed** | Missing entirely |

#### `POST /v1/client/sign_ins` — the true payload

Content-Type: `application/x-www-form-urlencoded`

```
strategy=oauth_google
&redirect_url=https://suno.com/sso-callback?auth_mode=sign-in&redirect_url=/create?signup_source=splashpage&referrer=%2F&...
&action_complete_redirect_url=https://suno.com/create?signup_source=splashpage&referrer=%2F&...
```

No `state`, `nonce`, or `code_challenge` is present in this request. No
`redirect_uri` to a loopback is present — the `redirect_url` is the browser
destination after completion, not the OAuth `redirect_uri`.

The response (`[T1]`, status 200) is:

```json
{
  "response": {
    "object": "sign_in_attempt",
    "id": "signin_PP98YBTiy2MgYGudUxuE4L",
    "status": "needs_first_factor",
    "supported_identifiers": [],
    "supported_first_factors": [
      {"strategy": "oauth_google", "default": false}
    ],
    "first_factor_verification": {
      "object": "verification_oauth",
      "status": "unverified",
      "strategy": "oauth_google",
      "external_verification_redirect_url": "https://auth.suno.com/social/login/google-oauth2/?next=...&__client=<refresh_jwt>",
      "expire_at": 1791849117370
    }
  },
  "client": { "object": "client", "id": "client_HrABYKrK8Uvf5LMrfiax2s", ... },
  "sign_in": { ... },
  "sign_up": {},
  "cookie_expires_at": <number>,
  "captcha_bypass": <bool>,
  "created_at": <number>,
  "updated_at": <number>,
  "last_active_session_id": "session_124ef7060eb675bb46152b"
}
```

The `external_verification_redirect_url` is the **first redirect target** the
browser follows. It is on `auth.suno.com` and carries a `__client` query
parameter.

#### The `__client` refresh-token JWT

Embedded in the `external_verification_redirect_url`:

- **Header**: `{"alg":"RS256","kid":"suno-api-rs256-key-1","typ":"JWT"}`
- **Payload**:
  ```json
  {
    "suno.com/claims/client_id": "client_HrABYKrK8Uvf5LMrfiax2s",
    "suno.com/claims/token_type": "refresh",
    "iss": "https://auth.suno.com",
    "exp": 1823381517,
    "secret": "rOyn11MXeGLexoIoxX6ExauqUyjrwxrw1fDXInB-oL4"
  }
  ```
- This is a **client refresh JWT**, not a session bearer. It allows Clerk to
  re-establish the client without re-running OAuth.

#### The Google OAuth redirect URI

From the OAuth authorization request in the HAR (entry on
`accounts.google.com/o/oauth2/auth`):

```
redirect_uri = https://auth.suno.com/social/complete/google-oauth2/
```

This is **not** a loopback URL. It is a Clerk server-side endpoint.

#### The server-side callback

After Google exchanges the consent for a `code`, it redirects back to:

```
GET https://auth.suno.com/social/complete/google-oauth2/
    ?state=i6i1AFxQfEpPvUVrLZIh9loY1Y1kMAwC
    &iss=https://accounts.google.com
    &code=4/0AXlqoi5Laev8dIIPNRWJPjca-ZKrUv0fyMT7D0-_jyxn6t8qQrJcPfKvmXrtX2R9YFKDwA
    &scope=email+profile+https://www.googleapis.com/auth/userinfo.profile+...
    &authuser=0
    &prompt=none
```

This `GET` returns a **302** (not a 200) redirecting to
`https://suno.com/create?signup_source=splashpage&...` — no `Set-Cookie`
header is visible in the HAR (stripped). Clerk exchanges the Google `code`
**server-side**, establishes the session cookie on the browser, and redirects
the browser to the app.

The `code` parameter **never reaches the ChadVis client process** — it is
consumed by `auth.suno.com` and never arrives at any `127.0.0.1` address.

#### `GET /v1/client` — session envelope

Returns the full client envelope (response body captured, `[T1]`):

```json
{
  "response": {
    "object": "client",
    "id": "client_HrABYKrK8Uvf5LMrfiax2s",
    "sessions": [{
      "id": "session_124ef7060eb675bb46152b",
      "object": "session",
      "status": "active",
      "expire_at": 1823381529308,
      "abandon_at": 1823381529308,
      "last_active_at": 1791845529308,
      "last_active_token": {
        "object": "token",
        "jwt": "<access_token_jwt>"
      },
      "user": { "id": "user_...", "username": "sderek02@gmail.com", ... }
    }],
    "last_active_session_id": "session_124ef7060eb675bb46152b",
    "cookie_expires_at": <number>,
    "captcha_bypass": <bool>,
    "created_at": <number>,
    "updated_at": <number>
  }
}
```

#### Access-JWT claims

Decoded from `sessions[0].last_active_token.jwt` (identical in both `/v1/client`
and `/tokens`):

**Header**: `{"alg":"RS256","kid":"suno-api-rs256-key-1","typ":"JWT","x-ably-token":"..."}`

**Payload key fields**:

| Claim | Value |
|---|---|
| `suno.com/claims/token_type` | `access` |
| `suno.com/claims/user_id` | `6f1554f2-6c19-44cb-804c-8836ec9dbde5` |
| `https://suno.ai/claims/clerk_id` | same UUID |
| `https://suno.ai/claims/email` | user's email (redacted in capture) |
| `suno/handle` | user handle (redacted) |
| `suno/user_id` | numeric account ID (redacted) |
| `suno/did` | numeric device ID |
| `sid` | `session_124ef7060eb675bb46152b` |
| `sub` | user UUID (same as `suno.com/claims/user_id`) |
| `aud` | `suno-api` |
| `iss` | `https://auth.suno.com` |
| `iat` / `exp` | issuance / expiry (Unix ms) |
| `azp` | `https://suno.com` |
| `fva` | `[0, -1]` |
| `plan` | `"<uuid>:month:"` |
| `suno/joined` | Unix timestamp |

The `sid` claim in the JWT matches `last_active_session_id` in the envelope —
**corroborating** the substitution documented in `ENDPOINT-INVENTORY.md` Appendix A
for a single-session account.

#### `POST /v1/client/sessions/{sid}/tokens`

```
POST /v1/client/sessions/session_124ef7060eb675bb46152b/tokens
```

**Response**: `{"jwt": "<identical_access_token_jwt>"}` — the same JWT appears in
both `/v1/client` and `/tokens`.

### Google One Tap

The Open Sniffer capture showed Google One Tap (`accounts.google.com/gsi/issue`),
but the new HAR shows the **full** Google OAuth standard flow (account chooser →
consent screen). The user clicked "Continue with Google" on the sign-in page,
Google redirected to the account chooser, and the full `oauth2/auth` → `gsi/issue`
chain is captured. The `recorder.json` step log also shows the flow landing on
`suno.com/auth/session-recovery` after account selection — see note below.

### Facebook login

**Absent from both captures.** Zero entries referencing `facebook.com` or
`fbs_*` cookies. The Facebook social-login button was never clicked.

### Sign-out

**Absent from both captures.** No `DELETE /v1/client`, no sign-out navigation,
no cookie-clearing behavior. `recorder.json` ends at the Google sign-in flow
without a sign-out step.

## What the captures resolve

### The native loopback callback gate is now **resolved negatively**

This is the primary finding. The new HAR definitively shows that **Clerk does
not use a native loopback callback for the Google social-login flow**. The
complete chain is:

1. `POST /v1/client/sign_ins` → response with
   `external_verification_redirect_url` containing `__client=<refresh_jwt>`
2. Browser navigates to `auth.suno.com/social/login/google-oauth2/?__client=...`
3. **302** redirect to `accounts.google.com/o/oauth2/auth` with:
   `redirect_uri=https://auth.suno.com/social/complete/google-oauth2/`
   (a **server-side** URL, never `http://127.0.0.1:<port>`)
4. Google OAuth consent flow (account chooser → consent)
5. Google redirects to `auth.suno.com/social/complete/google-oauth2/?code=...&state=...`
6. Clerk server exchanges `code` for a session, **302** redirect to
   `suno.com/create?...`
7. Browser loads the app, which calls `GET /v1/client` using session cookies
   set by Clerk

**No `127.0.0.1` address appears anywhere in the 1379-entry HAR.** No
`code` query parameter ever arrives at a loopback URL. The OAuth
`redirect_uri` is `https://auth.suno.com/social/complete/google-oauth2/`,
a Clerk-owned server endpoint.

The implication for `src/suno/auth/oauth/`:

- `OAuthLoginService` and `LoopbackListener` are designed to receive an
  authorization-code callback at `http://127.0.0.1:<port>` and extract
  `code`/`state` from the query string. In Clerk's actual flow, **the `code`
  never leaves the browser server boundary** — it goes directly from Google to
  `auth.suno.com/social/complete/`, and the client only observes the resulting
  session via `GET /v1/client`.
- `CaptureApprovedLaunch::loopbackPath` can never be populated with a real,
  working loopback callback because **Clerk does not offer one** for this flow.
- The Google OAuth parameters (`state=i6i1AFxQfEpPvUVrLZIh9loY1Y1kMAwC`,
  `redirect_uri`, `scope=openid email profile`, `response_type=code`,
  `prompt=select_account`) are generated server-side by Clerk, not by the
  client. The client cannot and should not forge them.
- `OAUTH_REDIRECT_ANALYSIS.md` requirement 2 ("native loopback callback") is
  **negative**: there is no native loopback callback to capture. The
  `OAuthLoginService` / `LoopbackListener` architecture does not match Clerk's
  server-side social-auth flow.

This is a **human decision point**, not an engineering one. The auth path that
actually works for Google is:
- Open the system browser to `https://suno.com/create?redirected_from=signin`
- Let the user complete the Clerk social login in the browser
- The browser receives session cookies from `auth.suno.com` (not captured in the
  HAR, but clearly set — the subsequent `GET /v1/client` is authenticated)
- The client reads the bearer from `GET /v1/client` (using cookies or by
  calling `/tokens`)

### Bearer acquisition routes

Both `GET /v1/client` (envelope bearer at
`sessions[0].last_active_token.jwt`) and `POST /v1/client/sessions/{sid}/tokens`
(top-level `jwt`) return the **same** access JWT (`[T1]`, confirmed identical
by base64 comparison in the HAR). The `touch` route was **not observed** in this
capture — it remains `[LEAD]` from the 2026-09-24 export only.

The route-selection question in `ENDPOINT-INVENTORY.md` Appendix A (tokens vs.
touch) is now: `tokens` is `[T1]` proven-working; `touch` is `[T1]` from an
older export but unverified here. The client can use either to obtain the
bearer; both return the same JWT shape.

### Set-Cookie attributes

**Still missing.** The HAR tool stripped all cookie data. The `__session` and
`__client` Clerk cookies' `Secure`, `HttpOnly`, `SameSite`, `Domain`, `Path`,
`Max-Age`/`Expires` attributes are **not captured**. The client cannot determine:

- Whether `HttpOnly` is set (affects whether the cookie is readable from
  JavaScript vs. requiring extraction through the system keychain on each
  platform).
- Whether `Secure` / `SameSite` / `Domain` scope would exclude certain targets.
- Lifetime attributes (the envelope returns `cookie_expires_at` as a number,
  but that is the server-side expiry, not the cookie's `Max-Age`).

The client code in `CredentialStore.cpp` treats cookies as opaque values
stored in the system keychain (`Backend::Keychain` on macOS). Whether the
cookie is `HttpOnly` determines whether the client can read it from the page
DOM at all, or must rely on a server-side extraction. This is **still
unresolved**.

## What remains missing

| Gap | Status | Why it matters |
|---|---|---|
| Facebook login flow | Missing | Must cover both social providers |
| `Set-Cookie` response headers | Missing (HAR stripped) | Governs persistence and `HttpOnly` |
| Sign-out / `DELETE /v1/client` | Missing | Needed for disconnect |
| `POST .../sessions/{sid}/touch` | Not observed | `[LEAD]` only; tokens proved working |
| Native loopback callback | **Proven nonexistent** | The flow is server-side; `OAuthLoginService` does not match Clerk |
| `code` reaching `127.0.0.1` | Impossible | Clerk exchanges it server-side |

### Note on the `session-recovery` landing

The `recorder.json` step log shows the UI navigating to
`https://suno.com/auth/session-recovery?return_to=...` after the user selected
their Google account. This is a Clerk-hosted session-recovery page (offered
when the social account is not yet linked to an active session). Despite this
UI state, the HAR shows successful subsequent requests: `GET /v1/client`
returned a full `session_124ef7060eb675bb46152b` envelope, and
`POST /tokens` returned a valid bearer JWT. The `session-recovery` page may
have been an intermediate redirect that was later bypassed, or the recording
and HAR captures spanned slightly different time windows. The network-level
evidence is authoritative: the session was established and the bearer was
issued.

## What the client can wire now

With the new HAR, the bearer acquisition path is fully proven:

1. Open the system browser to `https://suno.com/create?redirected_from=signin`.
2. The user completes Google social login in the browser.
3. `GET /v1/client` — the client calls this (authenticated by session cookies
   that the browser manages) and reads `last_active_session_id` and
   `sessions[0].last_active_token.jwt` as the access bearer.
4. **Fallback**: `POST /v1/client/sessions/{sid}/tokens` returns the same JWT
   at top-level `jwt`.

The critical caveat is that **the session cookies (`__session`, `__client`)
are managed by the system browser**. The client must either:
- Rely on the browser to persist the cookies and call `GET /v1/client` in a
  web-context (e.g., a web view or embedded browser), or
- Persist the cookies through `CredentialStore` and replay them on
  `GET /v1/client` — **but** the cookie attributes (`HttpOnly`, `SameSite`,
  `Domain`) that determine whether the client can read or replay them are still
  uncaptured.

## What the client actually wired (T0181, 2026-10-13)

An embedded web view is **not** an option for step 3: Google blocks OAuth in
embedded browsers via a disallowed user agent, so the first bullet above is
unreachable and the client's only real choice is the second.

What shipped:

- `OAuthLoginService`, `LoopbackListener` and `OAuthTransaction` are
  **deleted**. They waited for an authorization code at
  `http://127.0.0.1:<port>` that this capture proves never arrives, and
  `setCaptureApprovedLaunch` gated the whole feature behind a capture that
  could never be taken — sign-in failed 100% of the time with
  "Desktop sign-in is not enabled in this build", which was literally true and
  completely unactionable. 1347 lines are in `.backup_graveyard/`.
- `AuthCoordinator::beginGoogleSignIn` opens the system browser to the URL in
  step 1 and reports only whether the launch succeeded. There is no cancel
  path: the browser is the user's own, this client cannot close it, and the
  flow has no client-side state left to abandon.
- The credential handoff is the **paste field the client already had** —
  `AccountPage.qml`'s "Paste session cookie header instead", routed through
  `CredentialStore`. It is the primary path for a server-side-callback flow,
  not a degraded mode, and the UI now says so.

Still blocked, and unchanged by the above: **the automation ceiling is one
captured fact.** A bookmarklet on suno.com that POSTs `document.cookie` to a
local listener would remove the paste entirely, but `document.cookie` cannot
read an `HttpOnly` cookie, and whether `__session` is `HttpOnly` is exactly the
`Set-Cookie` attribute gap this document records as `[VERIFY]`. Capture that
first (T0182); building the bookmarklet before knowing risks a control that
appears to work and reads nothing.

## What the client will wire once the remaining gaps are filled

The remaining `[VERIFY]` items in `ENDPOINT-INVENTORY.md` that block wiring:

1. **Facebook sign-in** — must be captured to cover the second social provider.
2. **`Set-Cookie` attributes** — a Burp or DevTools HAR (not Open Sniffer) must
   capture `Set-Cookie` response headers for `__session` and `__client` to
   determine whether the client can read them or must rely on the system
   keychain path.
3. **Sign-out** (`DELETE /v1/client` + cookie clears) — needed for the
   disconnect flow.

Once (2) is resolved, the client can determine whether
`CredentialStore::Backend::Keychain` is the right persistence path or whether
the cookies are `HttpOnly` and must be read differently. Once all three are
captured, the gates in `ENDPOINT-INVENTORY.md` §3.1, `OAUTH_REDIRECT_ANALYSIS.md`
requirement 2, and `TODO.md` T0011/T0141/T0145 can be assessed for flipping.

## Summary

The new HAR resolves the two biggest open questions:

- **The `sign_ins` payload is proven**: `strategy=oauth_google&redirect_url=...&action_complete_redirect_url=...`
  (form-encoded, no PKCE, no loopback `redirect_uri`).
- **The native loopback callback does not exist**: Clerk's Google flow uses
  `redirect_uri=https://auth.suno.com/social/complete/google-oauth2/` (server-side),
  and the authorization `code` never reaches `127.0.0.1`. The
  `OAuthLoginService` / `LoopbackListener` architecture in `src/suno/auth/oauth/`
  does not match the actual flow.

What remains for a fully wired client: Facebook login, `Set-Cookie` attributes,
and sign-out. The bearer acquisition route (`tokens` vs `touch`) is resolved
in favor of `tokens` (proven `[T1]`); `touch` remains unverified.
