# OAuth Redirect Gate

**Evidence reviewed:** 2026-09-22 sanitized request recon; 2026-09-24 Burp export
**Status:** Suno-owned web flow observed; intended native path is the system browser plus an app-owned `127.0.0.1` loopback callback, pending a capture of Clerk's loopback response
**Scope of this file:** the observed web request sequence, the Clerk cookie
families relevant to it, and the binding native-callback gate. Consolidated from
the canonical master on 2026-09-26; do not duplicate this gate elsewhere.

## Scope

This file is not an endpoint catalog and does not define a desktop OAuth
implementation. Its only live responsibility is to record the observed web
redirect shape and the binding gate for any future native callback.

The canonical API facts live in
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md), which is the sole place a
`[T1]`/route table may state an auth contract. Evidence provenance and hashes
live in [`raw/README.md`](raw/README.md). Uncaptured auth routes and the
method/purpose conflicts among them live in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) and the master's
[conflict register](ENDPOINT-INVENTORY.md#appendix-a--explicit-conflict-register).

## Evidence and limitations

The retained
[`sanitized-recon-2026-09-22.json`](raw/sanitized-recon-2026-09-22.json) is a
sanitized **request-only** recording with 79 entries and 64 redactions. It shows
the sequence below but contains no response statuses, response bodies,
redirect-status assertions, or cookie-setting proof.

The 2026-09-24 Burp export did not exercise Google sign-in or a provider
callback. It captured Clerk session-token traffic and a Suno web request that
entered session recovery; that is not OAuth callback evidence.

## Observed Suno web sequence

Only method, host/path, and query-key names are reproduced. OAuth client IDs,
state, codes, tokens, account values, and other identifiers are intentionally
omitted.

| Order | Method | Host and path | Captured query-key names |
|---:|---|---|---|
| 1 | POST | `auth.suno.com/v1/client/sign_ins` | None in this request record |
| 2 | GET | `auth.suno.com/social/login/google-oauth2/` | `next`, `__client` |
| 3 | GET | Google authorization endpoint | `client_id`, `redirect_uri`, `state`, `response_type`, `scope`, `prompt` |
| 4 | GET | Google account chooser | The authorization keys plus Google-internal keys |
| 5 | GET | Google consent endpoint | `authuser`, `client_id`, `state`, plus Google-internal keys |
| 6 | GET | `auth.suno.com/social/complete/google-oauth2/` | `state`, `iss`, `code`, `scope`, `authuser`, `prompt` |
| 7 | GET | `suno.com/create` | Signup/referrer/origin and `redirected_from` keys |

The observed provider completion target is Suno-owned HTTPS. A Google-owned
authorization hop followed by a Suno callback is still a web flow, not proof of
a native redirect.

The separate 2026-08-25 Burp auth capture also observed
`GET /v1/client/handshake`, `GET /v1/client`, and the touch flow. The
2026-09-24 export re-observed `GET /v1/client`, `tokens`, and `touch`, but did
not exercise Google sign-in or any callback. Session-token calls do not prove a
provider redirect contract, and the handshake must not be assumed to have
appeared in the 2026-09-22 sequence merely because both captures concern auth.
The bearer-acquisition sequence itself (`GET /v1/client` → active session →
bearer, plus the `touch`/`tokens` variants) is specified in the canonical
master, section 3.1 and 5.1, and is not restated here.

## Clerk cookie families relevant to the observed flow

Only these Clerk-related name families are relevant to the captured flow:

- `__session` and its instance-key-suffixed variant
- `__client` and its instance-key-suffixed variant
- `__client_uat` and its instance-key-suffixed variant

The exact instance-key suffix is intentionally omitted. Do not assume a fixed
Clerk frontend key is universal. Analytics, advertising, payment, and RUM
cookies seen alongside the flow are not authentication requirements and are
intentionally omitted.

## Intended native callback path

The target is the user's system default browser plus an app-owned
`http://127.0.0.1:<ephemeral-port>/<path>` loopback callback, with the user
completing Google or Facebook social login on suno.com and Clerk returning the
authorization code to the loopback. Before the live handshake is trusted, a
human capture must prove all of the following:

1. Clerk accepts the proposed app-owned callback registration.
2. Google/Clerk sends the callback to the app-owned `http://127.0.0.1:<port>` loopback target rather than
   only the observed Suno-owned HTTPS completion route.
3. State, transaction binding, PKCE, redirect ownership, and callback replay
   protections are enforceable end to end.
4. The resulting session can be persisted without scraping an unrelated browser
   cookie jar and refreshed through a directly captured Clerk route.
5. Sign-out, failure, timeout, and concurrent-transaction behavior are captured
   and sanitized.

Do not infer loopback support from local code, a generic OAuth client, or a
Suno-owned HTTPS redirect. Never hand-roll a Clerk handshake, guess a callback
URI, or place a Google ID token in `SunoClient`.

## Evidence handling

Never retain or log OAuth codes/state, client identifiers, session cookies,
JWTs, email addresses, user/session identifiers, or complete redirect query
strings in live documentation. Future evidence should preserve only the method,
host/path, query-key names, status when available, and a redacted structural
sample.
