# Suno Auth & Session API

## Common request headers

- `Authorization: Bearer {jwt}`
- `Device-Id: {uuid}` — generate once, persist
- `Browser-Token: {base64-encoded-timestamp-json}`
- `Origin: https://suno.com`
- `Referer: https://suno.com/...`
- `User-Agent: Mozilla/5.0...` — browser-like

## Common cookies

- `__session` — JWT, domain `.suno.com`, `Lax`, ~6 months
- `__session_Jnxw-muT` — session variant
- `__client` — client token
- `__client_uat` — client updated-at time
- `__client_uat_Jnxw-muT` — variant with suffix
- `_ga`, `_ga_*` — Google Analytics
- `_fbp` — Facebook pixel
- `_ttpid`, `_twpid` — Twitter
- `_sp_id.*` — Segment
- `_dd_s` — Datadog RUM
- `_scid`, `_scid_r` — session IDs
- `__stripe_mid`, `__stripe_sid` — Stripe
- `_u` — Suno tracking (encrypted)
- `_uetsid`, `_uetvid` — Microsoft Clarity

## JWT claims

- `suno.com/claims/user_id` — UUID
- `https://suno.ai/claims/clerk_id` — `user_*`
- `suno.com/claims/token_type` — `access`
- `aud` — `suno-api`
- `fva` — `[0, -1]` feature access vector
- `exp` — expiration, roughly 1 hour from issue
- `sun/handle` — user handle
- `sun/user_id` — numeric user ID
- `sun/username` — email

## Clerk Auth (`auth.suno.com/v1`)

### `GET /v1/client?_is_native=true&_clerk_js_version={ver}`
**Purpose**: Fetch Clerk client config and current browser/session state.
**Auth**: Cookie
**Headers**: `Cookie: __session=...; __client=...`, browser-like `User-Agent`, `Origin`, `Referer`
**Body**: None.
**Response**: JSON client config; includes `last_active_session_id` and session metadata.
**Notes**: Native flag and Clerk JS version must match the web app’s expectation; used to discover the active session before token exchange.

### `POST /v1/client/sessions/{sid}/tokens`
**Purpose**: Exchange a Clerk session for a Suno JWT.
**Auth**: Cookie
**Headers**: `Cookie`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: JWT payload/metadata for the session.
**Notes**: Primary session-to-bearer exchange path; token is typically the `__session`-backed access token used against `studio-api-prod.suno.com`.

### `POST /v1/client/sessions/{sid}/tokens/api`
**Purpose**: Alternative API-oriented session-to-JWT exchange.
**Auth**: Cookie
**Headers**: `Cookie`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: JWT payload/metadata.
**Notes**: Alternate exchange path observed in the Clerk client; behavior is effectively the same class of output as `/tokens`.

### `POST /v1/client/sessions/{sid}/touch`
**Purpose**: Refresh session activity and keep the Clerk session alive.
**Auth**: Cookie
**Headers**: `Cookie`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: Session touch acknowledgment.
**Notes**: Heartbeat-style call; helps maintain `last_active_session_id` and avoids session staleness.

### `GET /v1/client/sync`
**Purpose**: Sync client state between browser, Clerk, and Suno.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: None.
**Response**: Synced client/session state.
**Notes**: Used by the web client to reconcile local client state with server-side session info.

### `GET /v1/client/verify`
**Purpose**: Verify the client/session is valid.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: None.
**Response**: Verification status and session validity metadata.
**Notes**: Common preflight check before issuing or refreshing bearer tokens.

### `POST /v1/verify`
**Purpose**: Perform verification for the current Clerk session.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: Verification payload as required by Clerk flow.
**Response**: Verification result.
**Notes**: Used during auth flows that require explicit session verification or challenge completion.

### `GET /v1/event`
**Purpose**: Send analytics/event telemetry.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: None.
**Response**: Event acceptance or no-content style acknowledgment.
**Notes**: GET variant used by the client for lightweight tracking pings.

### `POST /v1/event`
**Purpose**: Send analytics/event telemetry.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: Event JSON payload.
**Response**: Event acceptance or no-content style acknowledgment.
**Notes**: POST variant for richer event payloads; same telemetry surface as the GET endpoint.

### `GET /v1/logs`
**Purpose**: Retrieve client logs.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: None.
**Response**: Log payload, typically JSON or text.
**Notes**: Used for diagnostics and client-side troubleshooting.

### `POST /v1/tickets/accept`
**Purpose**: Accept ticket or policy acceptance state tied to auth flows.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: Ticket acceptance payload.
**Response**: Acceptance acknowledgment.
**Notes**: Observed during Clerk-driven gating flows; may be required to proceed past a prompt or entitlement check.

### `GET /v1/client?...&_method=PATCH`
**Purpose**: Hidden update path that tunnels a PATCH through GET semantics.
**Auth**: Cookie
**Headers**: `Cookie`, browser-like headers
**Body**: Query/body fields describing the client patch/update.
**Response**: Updated client config/state.
**Notes**: Method override trick used by the client when a direct PATCH is not issued; treat as a mutating operation despite the GET verb.

## Suno Session (`studio-api-prod.suno.com`)

### `GET /api/session/`
**Purpose**: Return the current authenticated session context.
**Auth**: Bearer
**Headers**: `Authorization: Bearer {jwt}`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: Session object with user/session metadata.
**Notes**: Core session endpoint used to confirm the JWT is accepted by the Suno backend.

### `GET /api/auth/verify-token`
**Purpose**: Verify the JWT token is valid and accepted by the backend.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: Token verification status.
**Notes**: Lightweight auth validation endpoint; useful before making stateful calls.

### `GET /api/user/me`
**Purpose**: Fetch the current user profile.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: User profile JSON.
**Notes**: Usually includes handle, display info, account state, and entitlement data.

### `GET /api/user/get_user_session_id/`
**Purpose**: Retrieve the backend session ID tied to the current user.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: Session ID payload.
**Notes**: Distinct from Clerk session IDs; this is the Suno backend/session mapping.

### `GET /api/user/user_config/`
**Purpose**: Get persisted user configuration.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: User config JSON.
**Notes**: Includes settings, toggles, preferences, and sometimes feature gates.

### `POST /api/user/update_user_config/`
**Purpose**: Update persisted user configuration.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: JSON patch/update object for config fields.
**Response**: Updated config JSON or success acknowledgment.
**Notes**: Primary config mutation endpoint; pair with `GET /api/user/user_config/` for read-modify-write flows.

### `GET /api/user/tos_acceptance`
**Purpose**: Check terms-of-service acceptance status.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None.
**Response**: Acceptance status JSON.
**Notes**: Used to gate access to generation or account features until terms are accepted.

### `POST /api/user/reset_onboarding/`
**Purpose**: Reset onboarding progress/state.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None or minimal reset payload.
**Response**: Reset acknowledgment.
**Notes**: Replays onboarding or clears onboarding completion flags.

### `POST /api/user/accept_timbaland_terms/`
**Purpose**: Record acceptance of Timbaland-specific terms.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None or minimal acceptance payload.
**Response**: Acceptance acknowledgment.
**Notes**: Feature-specific legal/entitlement acceptance gate.

### `DELETE /api/user/delete-account/`
**Purpose**: Delete the authenticated user account.
**Auth**: Bearer
**Headers**: `Authorization`, `Device-Id`, `Browser-Token`, `Origin`, `Referer`, `User-Agent`
**Body**: None or confirmation payload if required.
**Response**: Deletion acknowledgment or queued deletion result.
**Notes**: High-risk destructive action; generally requires re-auth or confirmation.
