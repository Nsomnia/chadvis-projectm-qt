# OAuth Redirect Gate

**Evidence reviewed:** 2026-09-22 sanitized request recon; 2026-09-24 Burp export
**Status:** Suno-owned web flow observed; native Google sign-in remains disabled

## Scope

This file is not an endpoint catalog and does not define a desktop OAuth
implementation. Its only live responsibility is to record the observed web
redirect shape and the binding gate for any future native callback.

The canonical API facts live in
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md). Evidence provenance and hashes
live in [`raw/README.md`](raw/README.md).

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

## Native callback gate

Native Google sign-in stays disabled. Before any implementation can be enabled,
a human capture must prove all of the following:

1. Clerk accepts the proposed app-owned callback registration.
2. Google sends the callback to a loopback or custom-scheme target rather than
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
