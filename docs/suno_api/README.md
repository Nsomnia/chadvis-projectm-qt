# Suno API Research Index

> **Unofficial, capture-based research**
>
> Suno does not publish a public API contract for this client. The material here
> was reverse-engineered from observed web behavior and retained evidence, is
> subject to change without notice, and may conflict with Suno's Terms of Use.
> ChadVis is not affiliated with or endorsed by Suno. Treat it as non-contractual
> research, not an official integration guide.

[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) is the **sole API-spec
master**. This index intentionally does not duplicate endpoint tables, auth
claims, model details, or other contract-shaped information.

## Navigation

- [Endpoint Inventory](ENDPOINT-INVENTORY.md) — the maintained source of truth
  for captured endpoints, evidence status, and API behavior.
- [OAuth Redirect Analysis](OAUTH_REDIRECT_ANALYSIS.md) — the reconstructed
  Google web flow and the native desktop callback gate. Native sign-in remains
  disabled until a human capture proves Clerk accepts a loopback or custom-scheme
  callback.
- [Raw evidence provenance](raw/README.md) — scope and limitations of the
  retained research artifacts:
  - [Endpoint sniff list](raw/endpoints_sniffed.list) — unfiltered endpoint
    discovery output.
  - [Sanitized OAuth recon](raw/sanitized-recon-2026-09-22.json) — sanitized
    recording supporting the web-flow analysis.

Raw artifacts are evidence, not API contracts. Do not infer implementation
approval or copy live credentials into documentation.
