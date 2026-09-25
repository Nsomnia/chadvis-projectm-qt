# Raw Evidence Provenance

The sole API-spec master is
[`../ENDPOINT-INVENTORY.md`](../ENDPOINT-INVENTORY.md). Everything described as
raw, sniffed, bundled, or exported is provenance only. It may be stale,
incomplete, secret-bearing, or contradictory and cannot promote an endpoint
above the inventory's evidence label.

## Handling rules

- Never copy live credentials, cookies, OAuth state/codes, personal data,
  account or clip identifiers, private text, temporary upload fields, or
  complete media URLs into Markdown, source, tests, or logs.
- Base64 is encoding, not sanitization. Treat an unredacted Burp XML export as
  secret material even when its payload is not readable at a glance.
- Prefer host-relative paths, query-key names, field names/types, status codes,
  and redacted structural samples in documentation.
- A decoded frontend route string is `[LEAD]` unless a matching direct
  request/response item exists.
- Record source timestamp, filename, and SHA-256 when evidence is reviewed.

## Retained repository artifacts

### `endpoints_sniffed.list`

An unfiltered endpoint-discovery dump with one URL per line. Candidates were
harvested from Suno web properties, JavaScript bundles, saved HTML, and network
observations, then sorted and deduplicated. It has no method, auth, schema,
status, or liveness guarantee and remains `[LEAD]` inventory only.

### `sanitized-recon-2026-09-22.json`

A sanitized 79-entry request recording with 64 redactions. It supports the
observed Google web-flow reconstruction in
[`../OAUTH_REDIRECT_ANALYSIS.md`](../OAUTH_REDIRECT_ANALYSIS.md). It contains
no response statuses or response bodies and does not validate a loopback or
custom-scheme desktop callback.

## External 2026-09-24 Burp export

Reviewed source:
`~/Documents/suno-burp-exports/sept-09-2026/`.

The directory label says September 9, but Burp 2026.8 exported **432 items on
2026-09-24 14:01–14:13 MDT**, and the item timestamps are 2026-09-24. The
Studio filename is truncated/mistyped as `studio-api-prod.suno.co`; its direct
requests use `studio-api-prod.suno.com`. The telemetry filename similarly
mis-spells the actual `m-stratovibe.prod.suno.com` host.

The complete XML remains outside the repository because it contains live
credentials, cookies, identity data, temporary upload fields, and private media
URLs. These hashes identify the reviewed local sources without reproducing
their contents:

| File | SHA-256 | Evidence role |
|---|---|---|
| `auth.suno.com` | `fdf9794b66a94739ccfda2cd7784eb5cec9b12f8fda696643f55bd9d727cfc01` | Direct Clerk client/tokens/touch evidence |
| `studio-api-prod.suno.co` | `e714fe9cf751ed17dae83bc7ff7a471e236dcebea331e6453e0aab73d028510b` | Direct Studio calls and matching preflights |
| `suno-uploads.s3.amazonaws.com` | `40e256480d15001fbaae3ddcbf5a9bbfbaece5138235ea37ddebac2dcd1998d8` | Direct multipart upload evidence |
| `suno.com` | `7190f66cc7e886da20071793e841b20fec715e18e2ee11ead962c87e5aa757fc` | Web redirect/static-bundle evidence |
| `cdn1.suno.ai` | `a7a4330a4694dfc675d42e41c0e9267be0cb693cc74ccd1d1a02b7f05b5b74fa` | Direct image/static asset requests |
| `cdn2.suno.ai` | `9452a8461f1ff37af82d010ea17618e9d32871928cae72ecde4c5d943dafb615` | Direct image/static asset requests |
| `cdn-o.suno.com` | `d184605c13754cbc7893e17e575fb27e371147fa9a44263d06076b86533eaca3` | Static asset support |
| `goto.suno.com` | `e79e70f3c8a43e298c6b953fb1cd48104ffc6ec7c7751609ec184de05cf2d862` | Static/supporting traffic only |
| `hcaptcha-assets-prod.suno.com_9i3s` | `00b6ddfc27c2a8a185de2594816fd12ba344598d86212e81130026534429343d` | Static assets; no challenge contract |
| `hcaptcha-endpoint-prod.suno.com` | `beec2e0b82fb60ba44ca7ce34843fa5179c37b916e50f3e0c9d9ef1df6b9420b` | Supporting traffic only |
| `logger.full.log.txt` | `cdf06c2302598d143c26df1162a89edf7ebf2060d022faa81355d12388e4e7d1` | One static JavaScript request, not a route log |
| `m-stratrovibe.prod.suno.com` | `0f1ed6e0a20798929b4f1dfc76bf64fefa824ff744364b4ef8bddca410d4ca67` | Telemetry only |
| `s.prod.suno.com` | `546a500f763db371a6da839e7cb7e93d3fef5874d2d9428d522a199590575787` | Telemetry only |
| `statusz.suno.ai` | `6a8536b528ef11ae6127fea499e84f6faaedb746260647335146a57589bac2d9` | Health/static traffic only |

The export directly supports only the contracts promoted in the canonical
inventory. Bundle-only routes, analytics, static assets, health checks, and
telemetry remain `[LEAD]` or otherwise non-contractual. The export did not
capture a Google OAuth callback, a loopback/custom-scheme callback, direct
generation submission, playlist mutation, WAV conversion, video-generation
submission, or an Orpheus/Modal request.
