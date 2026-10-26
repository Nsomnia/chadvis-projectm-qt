# Capture kit — encrypted ("mango") playback channel

The channel is a content-rights call plus an encrypted media body
([`../ENDPOINT-INVENTORY.md`](../ENDPOINT-INVENTORY.md) section 5.11 owns the
contract). This directory owns **how to capture it again**, because a capture
is the only thing that can promote a `[LEAD]` to a `[T1]`, and because the
server can change.

**Nothing here records a secret.** Every route below produces a *shape*
report: field names, value lengths, host names, path templates. A key, an IV,
a token, a cookie, a clip id, and a complete media URL are never written down,
and a report is safe to paste into a chat or an issue.

## Route 1 — the probe (no browser) — preferred

```bash
node docs/suno_api/capture/mango-probe.mjs            # anonymous variant
node docs/suno_api/capture/mango-probe.mjs --bearer   # authenticated variant
```

It reads one clip's `media_urls[]` entry out of this machine's own Suno
library database (the client's own captured feed data), asks the rights
endpoint for the key, unwraps it, and decrypts the first 4 KiB of the media.
It prints the license status, the response field names and lengths, the
unwrapped key/counter lengths, and the decrypted container magic.

`--bearer` reads the bearer this app already has in the OS keychain
(macOS service `chadvis-projectm-qt`, account
`chadvis-projectm-qt/chadvis/chadvis-projectm-qt/chadvis/suno/bearer`) and
sends it. That is the authenticated variant: the response then carries **no**
`glt`, and the key unwraps with `SHA-256(bearer)`.

A complete capture is: `rights status : 200` **and**
`decrypted magic : MP4 (audio/mp4)`.

Requirements: Node >= 22 (for `node:sqlite`) and `node:crypto` webcrypto. No
npm packages, no login, no cookies. It works without a session because the
rights endpoint answers an **anonymous** request with a guest token — the
whole channel can be exercised without credentials.

## Route 1b — the base probe (`route-probe.mjs`) — T0333 / T0336

```bash
node docs/suno_api/capture/route-probe.mjs
```

Sends the app's own keychain bearer to the four route/base combinations that
matter — `unified/explore` and `feed/v3` on `studio-api-prod.suno.com/api` and on
`suno.com/a/api`, plus `notification/v3` on the new base — and prints **shapes**:
status, byte count, top-level key names, array lengths, and the first element's
key names. No values, no ids, no tokens.

It exists because "is the API dead or is our parser wrong?" is one run to answer
and a day to guess at. Measured 2026-10-25: **both bases answer all three routes
with byte-identical bodies**, so a route that returns 200 with no rows is a
parser problem, not a host problem.

## Route 2 — the browser userscript (for the authenticated variant)

Install [`mango-capture.user.js`](mango-capture.user.js) in Tampermonkey or
Violentmonkey, open `https://suno.com`, **play one song**, then run
`__chadvisCaptureDump()` in the console. The report is printed and copied to
the clipboard.

Use this when the *authenticated* rights response is the question — the
signed-in variant wraps the key with `SHA-256(bearer)` rather than
`SHA-256(glt)`, and only a signed-in session shows that. The script reports
the `Authorization` header's **presence and length only**.

## Route 3 — Burp or ZAP (for a proxy-grade record)

Use this when the request must be recorded with its full headers, or when the
rights call is made by a *different* client than the site's own.

**Burp**

1. Proxy the browser through Burp (127.0.0.1:8080), install Burp's CA in the
   browser, and log in to suno.com.
2. Play one song. In Proxy -> HTTP history, filter:
   `host: studio-api-prod.suno.com AND url: /api/mango/rights`
   plus a second filter for the media body:
   `url ends with .m4a` (or `.webm`).
3. Right-click the rights item -> **Save item** (a `.json`-ish body plus
   headers) and the media item -> **Save item** (raw bytes).
4. **Redact before handing it over:** the `Authorization` header, every
   `Cookie`, and the clip id in `content_params.content_id`. Replace the key
   and IV values with their lengths; keep the field names.

**ZAP**

1. Set a browser proxy to 127.0.0.1:8081 (the ZAP proxy), install the ZAP
   root CA in the browser, log in, play one song.
2. Sites -> tree -> `studio-api-prod.suno.com` -> expand; open
   `/api/mango/rights` -> **Request/Response** tabs, and the `.m4a` request.
3. Export with the same redaction rules as above.

**What a proxy record adds** over route 1: the exact request headers the site
sends (the `Device-Id` and `Browser-Token` headers the site's API client
adds, and whether the rights call carries a cookie), and the raw encrypted
bytes for a byte-exact decryption test.

## Handing a capture over

- Prefer the report over the raw capture: it is the same information with no
  secret in it.
- A clip id is an identifier. Hash it (the probe and the userscript both
  print a hash) unless the capture is about a specific clip.
- A complete media URL carries the clip id in its path. Report it as
  `host/<clip-id>.m4a`.
