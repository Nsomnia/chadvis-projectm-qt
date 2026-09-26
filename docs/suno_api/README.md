# Suno API Research Index

> **Unofficial, capture-based research**
>
> Suno does not publish a supported API contract for this client. These notes
> describe point-in-time observations, may change without notice, and are not
> an official integration guide. ChadVis is not affiliated with or endorsed by
> Suno.

## Authority boundary

- [`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) is the **sole API-spec
  master** for routes, request/response shapes, host distinctions, and
  `[T1]` evidence status.
- [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) is the catalogue of **non-contractual
  observations** — every `[LEAD]` and `[VERIFY]` row. It is never a stable
  contract and never an implementation source; it exists only for research and
  capture planning, and may not be wired without a direct capture.
- [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) is the single
  location for the observed Google web flow, the Clerk cookie families, and the
  **native-callback gate**. The intended desktop sign-in path is the system
  default browser plus an app-owned `http://127.0.0.1:<port>` loopback callback
  for Google/Facebook social login; Clerk's loopback acceptance still owes a
  human capture.
- [`raw/README.md`](raw/README.md) records provenance, SHA-256 hashes, artifact
  retention, and capture maintenance discipline. Raw scans and unredacted
  external exports are evidence, never implementation contracts or
  documentation sources.

This index intentionally contains no endpoint tables, auth recipes, model
claims, or response schemas. Update the canonical inventory first; keep this
file as navigation only.

## Evidence labels

- `[T1]`: directly present in a reviewed request/response capture.
- `[LEAD]`: scan, bundle, prose, or reconstruction without a direct contract.
- `[VERIFY]`: conflicting, incomplete, or observed but not established as
  universally required.

Labels are defined in
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) section 1.1, which is the
label authority. `[T1]` is point-in-time evidence, not an availability or
compatibility guarantee, and it does **not** mean the route is implemented.
Implementation state is a separate axis
([section 1.3](ENDPOINT-INVENTORY.md)); do not read one as the other.

## 2026-09-24 capture audit

The external source directory is labeled `sept-09-2026`, but its Burp export and
all item timestamps are dated 2026-09-24. The raw base64 XML is not sanitized
and remains outside the repository. See
[`raw/README.md`](raw/README.md) for the reviewed-source map, the full SHA-256
hash manifest, and the host/filename misspell reconciliation.

Raw captures stay **outside** the repository. The only in-repository raw
material is the sanitized recon:

- [Sanitized OAuth recon](raw/sanitized-recon-2026-09-22.json) — request-only
  evidence supporting the web-flow analysis. 79 entries, 64 redactions, no
  response statuses or bodies.

The endpoint sniff list that used to sit here was removed on 2026-09-26
because it contained real PII. Its non-PII `[LEAD]` rows survive as prose in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md). Do not restore it.

Never copy live credentials, cookies, OAuth state/codes, personal data,
identifiers, private text, temporary upload fields, or complete media URLs into
Markdown or logs.
