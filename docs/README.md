# ChadVis Documentation

Source-verified documentation hub for ChadVis, a Suno-first desktop client with a
native projectM video workspace.

Every document here has exactly one owning subject. If two files claim the same
fact, that is a bug — fix it here rather than adding a third copy.

## Using ChadVis

| Document | Covers |
| :--- | :--- |
| [Installation](user/INSTALL.md) | Prerequisites, Linux and macOS builds, dependencies, running from source. |
| [Configuration](user/CONFIG.md) | Every shipped TOML key, its type, default, and meaning. Includes the legacy migration-only fields. |
| [Usage](user/USAGE.md) | The seven shell surfaces, credential entry, and the keyboard shortcut table. |

## Development

| Document | Covers |
| :--- | :--- |
| [Architecture](dev/ARCHITECTURE.md) | Process ownership, init and teardown order, the QML shell, bridge registration, Suno data flow. |
| [Testing](dev/TESTING.md) | The 17 `unit_tests` sources, the 6 standalone executables, and the integration harness. How to actually run them. |
| [Manual QA](dev/manual-qa.md) | The MT-001 GUI checklist. Real-GUI versus offscreen coverage, per item. |
| [Contributing](dev/CONTRIBUTING.md) | C++23 conventions, error handling, and PR process. |

## Suno API research

> Suno publishes no supported API for this client. Everything in this section is
> unofficial and capture-derived, may break without notice, and is not an
> official integration guide. ChadVis is not affiliated with Suno.

The authority model is deliberately narrow. **Read
[`suno_api/README.md`](suno_api/README.md) before writing any code against these
routes** — it states which single file owns which kind of fact.

| Document | Role | Covers |
| :--- | :--- | :--- |
| [API index](suno_api/README.md) | **Navigation boundary** | Authority rules and the `[T1]`/`[LEAD]`/`[VERIFY]` legend. No endpoint tables. |
| [Endpoint inventory](suno_api/ENDPOINT-INVENTORY.md) | **Sole API-spec master** | Hosts, request conventions, the `[T1]` route catalog with contracts, error and model parsing. |
| [Observed leads](suno_api/OBSERVED-LEADS.md) | **Non-contractual** | Every `[LEAD]`/`[VERIFY]` route, plus client flag names. Research only — never a stable contract, never an implementation source. |
| [OAuth redirect analysis](suno_api/OAUTH_REDIRECT_ANALYSIS.md) | **Native-callback gate** | The captured Google/Facebook web flow and the intended `127.0.0.1` loopback sign-in path. |
| [Evidence provenance](suno_api/raw/README.md) | **Provenance only** | Capture dates, SHA-256 hashes, and the rules for handling evidence. |

Raw network captures are **not** committed. See
[raw/README.md](suno_api/raw/README.md) for the retention policy and the one
sanitized artifact that is retained.

`src/suno/SunoEndpoints.hpp` mirrors these routes for the code. It is an
implementation convenience, **not** evidence — never resolve a documentation
question by reading it.

## Engine integration

| Document | Covers |
| :--- | :--- |
| [projectM](integration/PROJECTM.md) | The `vc::pm::Bridge` render path, FBO format, preset scanning, and known limitations. |
| [Suno integration](integration/SUNO.md) | Operator-facing rules for the Suno client, and the API-authority disclaimer. |

## Project

| Document | Covers |
| :--- | :--- |
| [Roadmap and pivot plan](PIVOT_PLAN.md) | The Suno-first product direction, binding rules, and phase plan. |
| [Backlog](../TODO.md) | The live work tracker. Single source for task state. |
| [Changelog](../CHANGELOG.md) | Released changes. |
| [Legacy changelog](CHANGELOG_LEGACY.md) | Pre-1.1.0 history. |
| [History](lore/HISTORY.md) | Project chronology, including the 2026-08-26 product pivot. |
| [Manifesto](lore/MANIFESTO.md) | Why this project exists, in the authors' own words. |

## For agents

If you are an AI agent working in this repository, these rules are load-bearing
and are not stylistic preferences:

- **Never promote `[LEAD]` to `[T1]`.** Evidence status changes only when a human
  capture proves it. See [`suno_api/README.md`](suno_api/README.md).
- **Never wire a route from `OBSERVED-LEADS.md`.** It exists so the master stays
  readable, not so leads can be implemented.
- **`[T1]` means "directly captured", not "shipped".** The `Implemented` column
  in the inventory is the independent second axis. A row can be `[T1]` and
  `not-in-code`; that is a normal, expected state.
- **Fail closed on unverified hosts.** Never send cookies, bearers, or automatic
  remote image requests to an absolute host that is not on a captured allowlist.
- **Never commit raw captures**, credentials, cookies, tokens, account
  identifiers, or complete media URLs.
- **Verify claims from source, not from docs.** Documentation in this repository
  has drifted from the code before; if a doc and `src/` disagree, `src/` wins and
  the doc is a bug worth reporting.
- **The backlog lives in [`TODO.md`](../TODO.md)** and operating rules in
  [`AGENTS.md`](../AGENTS.md).
