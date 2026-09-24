# OAuth Redirect Analysis for Suno Remote Logging

**Date:** 2026-09-22
**Status:** COMPLETE for the observed web flow — sanitized evidence saved; native desktop callback remains capture-gated

## Request

Integrate logging into Suno remote using OAuth redirect. If the endpoint scan does not provide sufficient details about OAuth redirect parameters, stop and inform the user that a Chrome extension is needed.

## Outcome

The recon recording (`~/Downloads/7752c544-ebc6-4f2b-98d1-f1dbb657dff4.json`) was found to contain **sufficient OAuth redirect details**. A Chrome extension was NOT needed — the recording captured the complete Google OAuth login flow through Suno's auth system.

The recording has been sanitized and saved to:
- `docs/suno_api/raw/sanitized-recon-2026-09-22.json` (79 entries, 64 redactions)

This reconstruction covers Suno's web flow only. It does not validate a native
desktop callback: no reviewed capture uses a loopback or custom-scheme redirect
target. Native Google sign-in must remain disabled until a human capture proves
Clerk accepts one.

## OAuth Redirect Flow Discovered

### Complete Google OAuth Login Flow

1. **Suno Social Login Initiation**
   ```
   GET https://auth.suno.com/social/login/google-oauth2/?next=https%3A%2F%2Fsuno.com%2Fcreate%3F...
   ```

2. **Google OAuth Authorization Request**
   ```
   GET https://accounts.google.com/o/oauth2/auth?
     client_id=[REDACTED-GOOGLE-CLIENT-ID]
     &redirect_uri=https://auth.suno.com/social/complete/google-oauth2/
     &state=[REDACTED-OAUTH-STATE]
     &response_type=code
     &scope=openid+email+profile
     &prompt=select_account
   ```

3. **Google Account Chooser**
   ```
   GET https://accounts.google.com/v3/signin/accountchooser?
     client_id=[REDACTED-GOOGLE-CLIENT-ID]
     &prompt=select_account
     &redirect_uri=https%3A%2F%2Fauth.suno.com%2Fsocial%2Fcomplete%2Fgoogle-oauth2%2F
     &response_type=code
     &scope=openid+email+profile
     &state=[REDACTED-OAUTH-STATE]
   ```

4. **Google OAuth Consent**
   ```
   GET https://accounts.google.com/signin/oauth/consent?
     authuser=0
     &client_id=[REDACTED-GOOGLE-CLIENT-ID]
     &scope=openid+email+profile
     &state=[REDACTED-OAUTH-STATE]
   ```

5. **OAuth Callback Completion**
   ```
   GET https://auth.suno.com/social/complete/google-oauth2/?
     state=[REDACTED-OAUTH-STATE]
     &iss=https://accounts.google.com
     &code=[REDACTED-GOOGLE-AUTH-CODE]
     &scope=email+profile+https://www.googleapis.com/auth/userinfo.profile+https://www.googleapis.com/auth/userinfo.email+openid
     &authuser=0
     &prompt=none
   ```

6. **Final Redirect to Suno Create**
   ```
   GET https://suno.com/create?signup_source=splashpage&referrer=%2F&...
     &redirected_from=signin
   ```

## Key OAuth Parameters

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `client_id` | [REDACTED-GOOGLE-CLIENT-ID] | Google OAuth client ID |
| `redirect_uri` | `https://auth.suno.com/social/complete/google-oauth2/` | OAuth callback URL |
| `response_type` | `code` | OAuth response type |
| `scope` | `openid email profile` | OAuth scopes requested |
| `state` | [REDACTED-OAUTH-STATE] | CSRF protection token |
| `prompt` | `select_account` | Google account selection prompt |

## Suno Auth Endpoints Discovered

| Endpoint | Purpose |
|----------|---------|
| `https://auth.suno.com/social/login/google-oauth2/` | Initiate Google OAuth login |
| `https://auth.suno.com/social/complete/google-oauth2/` | OAuth callback completion |
| `https://auth.suno.com/v1/client/sign_ins` | Clerk sign-in tracking |
| `https://auth.suno.com/auth/session-recovery` | Session recovery page |

## Other Endpoints Discovered in Recon

| Domain | Endpoint | Purpose |
|--------|----------|---------|
| `suno.com` | `/9i3s/td` | Google Tag Manager tracking |
| `suno.com` | `/9i3s/g` | Google Analytics |
| `suno.com` | `/9i3s/as` | AdSense |
| `suno.com` | `/9i3s/gs` | Google Services |
| `s.prod.suno.com` | `/v1/rgstr` | Registration tracking |
| `m-stratovibe.prod.suno.com` | `/agg-receiver-service/v1/events/t` | Stratovibe event tracking |
| `analytics.tiktok.com` | `/api/v2/pixel/act` | TikTok pixel |
| `analytics.twitter.com` | `/i/adsct` | Twitter ads tracking |
| `www.facebook.com` | `/tr` | Facebook pixel |
| `js.stripe.com` | `/v3/...` | Stripe JS |
| `api.hcaptcha.com` | `/getcaptcha/...` | hCaptcha |
| `api.stripe.com` | `/v1/radar/session` | Stripe Radar |

## Sanitization Applied

The following sensitive data was redacted from the recon recording:

- Email addresses (`[REDACTED-EMAIL]`)
- Google OAuth client ID
- Datadog API keys
- Stripe API keys
- hCaptcha pixel codes and IDs
- Google Tag Manager IDs
- Clerk JWT tokens
- Google session tokens
- OAuth authorization codes
- OAuth state tokens
- User IDs, session IDs, anonymous IDs
- IP addresses
- Timezone and country information

## Current Auth Implementation

- `src/suno/auth/ClerkAuthClient.hpp` — Clerk auth client
- `src/suno/auth/CredentialStore.hpp` — Credential storage
- `src/suno/auth/AuthHeaders.hpp` — API header builder
- `src/suno/SunoClient.hpp` — Suno API client
- `src/suno/SunoEndpoints.hpp` — Centralized endpoint map

## Related Files

- [`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) — sole API-spec master, including captured auth and session facts
- [`raw/README.md`](raw/README.md) — retained raw-evidence provenance and limitations
- [`raw/endpoints_sniffed.list`](raw/endpoints_sniffed.list) — raw endpoint scan data
- [`raw/sanitized-recon-2026-09-22.json`](raw/sanitized-recon-2026-09-22.json) — sanitized recon recording supporting this analysis
