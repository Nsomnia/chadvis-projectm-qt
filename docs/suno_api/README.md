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
  `[T1]`/`[LEAD]`/`[VERIFY]` evidence status.
- [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) is limited to the
  observed Google web flow and the binding native-callback gate. Native Google
  sign-in remains disabled until a human capture proves Clerk accepts a loopback
  or custom-scheme callback.
- [`raw/README.md`](raw/README.md) records provenance, hashes, and safe handling
  for evidence. Raw scans and unredacted external exports are evidence, never
  implementation contracts or documentation sources.

This index intentionally contains no endpoint tables, auth recipes, model
claims, or response schemas. Update the canonical inventory first; keep this
file as navigation only.

## Evidence labels

- `[T1]`: directly present in a reviewed request/response capture.
- `[LEAD]`: scan, bundle, prose, or reconstruction without a direct contract.
- `[VERIFY]`: conflicting, incomplete, or observed but not established as
  universally required.

`[T1]` is point-in-time evidence, not an availability or compatibility
guarantee.

## 2026-09-24 capture audit

The external source directory is labeled `sept-09-2026`, but its Burp export and
all item timestamps are dated 2026-09-24. The raw base64 XML is not sanitized
and remains outside the repository. See the inventory's dated source map and
the raw provenance hash manifest before using any evidence.

Retained in-repository raw material:

- [Endpoint sniff list](raw/endpoints_sniffed.list) — unfiltered `[LEAD]`
  discovery output.
- [Sanitized OAuth recon](raw/sanitized-recon-2026-09-22.json) — request-only
  evidence supporting the web-flow analysis.

Never copy live credentials, cookies, OAuth state/codes, personal data,
identifiers, private text, temporary upload fields, or complete media URLs into
Markdown or logs.
