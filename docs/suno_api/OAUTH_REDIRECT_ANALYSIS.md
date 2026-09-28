# OAuth Redirect Gate

**Evidence reviewed:** 2026-09-22 sanitized request recon; 2026-09-24 Burp
export; 2026-08-25 `auth.suno.com` Burp export (13 API items)
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

The 2026-08-25 `auth.suno.com` export did capture both Suno-owned Google social
legs, with 302 statuses and a `sessionid` cookie on initiation (see
[Observed Suno web sequence](#observed-suno-web-sequence)). It captured **no**
loopback callback, no non-`suno.com` redirect target, and no evidence about
Clerk's `redirect_url` validation. Its export identity and hash are in
[`raw/README.md`](raw/README.md).

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

The 2026-08-25 `auth.suno.com` export captured **both** Suno-owned social legs
directly, with response statuses this file could not previously assert. Only
shapes are recorded:

| Step | Captured request shape | Captured response shape |
|---|---|---|
| Provider redirect initiation | `GET /social/login/google-oauth2/` with `next` and `__client` | **302**; `Location` to the Google authorization endpoint; `Set-Cookie: sessionid` (HttpOnly, `SameSite=None`, Secure) |
| Suno-owned callback | `GET /social/complete/google-oauth2/` with `state`, `iss`, `code`, `scope`, `authuser`, `prompt` | **302**; `Location` to the captured `next` target; sets a Clerk `__client` cookie |

This upgrades the completion target from a reconstruction to a directly
observed 302-with-`Location`, and settles the sign-in legs' statuses. It does
**not** establish a non-Suno-owned target: nothing in this evidence points
anywhere but the Suno-owned HTTPS completion route already described above. No
OAuth code, state value, `client_id`, `sessionid` value, `__client` value, or
complete redirect query string is reproduced above or anywhere in this file.
The route purposes and evidence labels are in the canonical master, section 3.2.

The same 2026-08-25 `auth.suno.com` export also observed
`GET /v1/client/handshake`, `GET /v1/client`, the touch flow, and
`POST /v1/client/sign_ins`; the 2026-09-24 export re-observed `GET /v1/client`,
`tokens`, and `touch`, but did
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

Two further names are now directly observed, by attribute only:

- `sessionid` — set on the provider redirect initiation leg, HttpOnly,
  `SameSite=None`, Secure. Its value is never recorded.
- `__clerk_handshake` — issued with `Max-Age=0`, i.e. a **one-shot nonce**, and
  also carried as a query value appended to the handshake `redirect_url`. Its
  value is never recorded, and the client must not synthesize one.

The instance-keyed `__client_uat` variant is non-secret session-presence
metadata, **not** a credential. The refresh credential lives inside `__client`;
its shape and lifetime are in the canonical master, section 3.3, and are not
restated here.

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

**Sharpened 2026-09-28, gate unchanged.** The 2026-08-25 `auth.suno.com`
export makes the handshake understandable enough to design against, and it
narrows what a capture must prove. `GET /v1/client/handshake` accepts an
**absolute** `redirect_url` and answers 302 with that URL plus a one-shot
`__clerk_handshake` nonce, and `POST /v1/client/sign_ins` carries a
`redirect_url` field in its captured form body. The exchange mechanism and
credential shapes are in the canonical master, section 3.3.

That does **not** answer the gate, and nothing above relaxes it. Both observed
`redirect_url` values were `https://suno.com/…` URLs. The open question is now
a single, specific one: **does Clerk validate the `redirect_url`, or will it
honour an `http://127.0.0.1:<ephemeral-port>/<path>` target?** Until a human
capture answers that, requirement 2 above stands unproven and the native path
stays disabled. A second capture that merely repeats a `suno.com` `redirect_url`
closes nothing.

This section is the only place the gate may be stated. The master records the
handshake contract and, in its conflict register, records only that the
question is unresolved and gated here; it does not restate the requirements.
The gate's scope — Google or Facebook social login — is unchanged. The instance
enables more providers server-side (canonical master, section 3.4), which is a
product decision about sign-in surfaces and is not evidence that any additional
provider is accepted as a native callback.

## Evidence handling

Never retain or log OAuth codes/state, client identifiers, session cookies,
JWTs, email addresses, user/session identifiers, or complete redirect query
strings in live documentation. Future evidence should preserve only the method,
host/path, query-key names, status when available, and a redacted structural
sample.
