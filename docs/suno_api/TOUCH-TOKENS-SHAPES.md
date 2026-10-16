# `tokens` and `touch` — response shapes recorded 2026-10-16

**Provenance.** A DevTools "save all as HAR + response bodies" session exported
into `scratch/auth-flow/` (gitignored; the parent repo never stores credential
values). The two files under `auth.suno.com/v1/client/sessions/<session id>/`
are the raw response bodies of the two routes T0141 records as
`[T1]`-observed. **No values from those files are transcribed here** —
shapes only, session ids redacted, and the JWT payloads are not decoded or
summarized field-by-field. Where the route takes a session id, we already
have it in code; nothing here adds a secret.

## `POST /v1/client/sessions/{sid}/tokens`

Body is an object with exactly one member:

```
{ "jwt": "<string>" }
```

This **confirms with a real body** what the inventory only asserted from
header capture: this route mints the `__session` bearer. `ClerkAuthClient`
parses `object` → `jwt` → `Authorization: Bearer`. No other members were
observed; treat any future extra members as additive (parse by member name,
ignore what is not read).

## `POST /v1/client/sessions/{sid}/touch`

Body is an object with three members:

- `response` — the session envelope: `id`, `object: "session"`,
  `status: "active"`, `expire_at` / `abandon_at` / `last_active_at`
  (epoch-ms **strings**, not numbers), `last_active_token: { object, jwt }`,
  `factor_verification_age: [<int>, …]`, `created_at`, `updated_at`, and
  `user` (a full Clerk user envelope).
- `client` — the Clerk client envelope: `id`, `sessions: [<session …>]`
  (same session shape), `sign_in` and `sign_up` attempt envelopes,
  `last_active_session_id`, `cookie_expires_at`, `captcha_bypass`.
- This body **contains the current `last_active_token.jwt`** — a live
  bearer. Any tooling that dumps response bodies raw (this capture included)
  holds credentials outside the repo. Record shapes; move files, or keep
  them inside the gitignored scratch area only.

## What this settles for T0141

The two routes are **not alternatives; both answered 200 on one working
session**: `tokens` is the bearer-minting route the client drives; `touch`
is the client keep-alive the browser drives, and its response happens to
carry the same bearer. Preference order stays as T0141 prescribes:
`tokens`-first for the client, `touch` not needed by us today.

Also captured in the same session (`from the Clerk script chunks`):
`POST /v1/client/sign_ins` with `strategy: "oauth_google"` and
`external_verification_redirect_url` on the verification envelope (Google's
OAuth URL, ~1 KB with `state`). This re-confirms T0199's premise — the
`code` goes to Clerk's redirect, never to a loopback — and that the canary
redirect_url r pair the app observes (`sso-callback?redirect_url=…` in the
chunk that builds them) is composed client-side per attempt.

## Risk note

`touch` response embeds `user.username` (an email address) and a bearer.
Both are why this file records **shape only**. If a future capture is
committed, sanitize at ingest per T0148 (field names, not values).
