# Retained Raw Evidence

The sole API-spec master is [`../ENDPOINT-INVENTORY.md`](../ENDPOINT-INVENTORY.md).
The artifacts in this directory are source evidence, not contracts: they may be
stale, incomplete, or unverified, and a listed URL does not establish that an
endpoint is live or approved for implementation.

## `endpoints_sniffed.list`

A large, unfiltered endpoint-discovery dump with one URL per line. Candidate
URLs were harvested from Suno web properties, JavaScript bundles, saved HTML,
and network observations, then sorted and deduplicated. Entries have not been
individually verified and must be checked against the master and capture
evidence before use.

## `sanitized-recon-2026-09-22.json`

A sanitized 79-entry recon recording with 64 redactions. It supports the
observed Google web-flow reconstruction in
[`../OAUTH_REDIRECT_ANALYSIS.md`](../OAUTH_REDIRECT_ANALYSIS.md).

Sensitive fields were redacted before retention. In the retained raw URL list and recon JSON, credential-, signature-, IP-, and account-state-like query values are represented by placeholders where found; this preserves route/parameter provenance only. The artifact is evidence for the web flow and route discovery only; it does not validate a loopback or custom-scheme desktop callback. Never add live credentials or unredacted personal data to raw evidence.
