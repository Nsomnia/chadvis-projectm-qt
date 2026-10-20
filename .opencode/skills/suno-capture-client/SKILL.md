---
name: suno-capture-client
description: >-
  Works on ChadVis's unofficial suno.com client: endpoints, auth, capture-derived
  routes, host allowlists, media download and upload. Use whenever a task touches
  src/suno/, src/qml_bridge/SunoBridge.cpp, docs/suno_api/, or anything that sends
  a cookie, bearer or request to a suno.com-adjacent host. Encodes the evidence
  grades, the fail-closed host policy, the one-owner HTTP policy, and the
  credential/logging rules that make this client safe to ship.
---

# Suno capture client

Suno publishes **no supported API** for this client. Every route is capture-derived,
unofficial, and may break without notice. That single fact drives every rule below:
nothing is wired on inference, and no host receives a secret on a guess.

## Read the owner before editing

This skill routes; it does not own facts. In order:

| Question | Owning document |
| --- | --- |
| Which route is real, what grade of evidence? | [`docs/suno_api/ENDPOINT-INVENTORY.md`](../../../docs/suno_api/ENDPOINT-INVENTORY.md) |
| Which file owns which *kind* of fact? | [`docs/suno_api/README.md`](../../../docs/suno_api/README.md) |
| Why a constraint exists (the bug behind it) | [`docs/dev/INVARIANTS.md`](../../../docs/dev/INVARIANTS.md) § "Suno evidence" |
| Operating rules, do-nots | [`AGENTS.md`](../../../AGENTS.md) |

**If a doc and `src/` disagree, `src/` wins.** Then fix the doc in the same commit.

## The two policies that are already solved — do not re-implement

Two files exist specifically because the policy had been written three times over
and drifted. Both headers explain the history; read them before touching either.

- **`src/suno/CapturedHosts.hpp`** — the ONE owner of outbound-host policy and
  deployment tiering. `mayReceiveCredential(url, tier)` and `mayBeFetched(url, tier)`
  are the only predicates. Do not add a fourth `isAllowed*Url()` helper with a
  hardcoded literal; add a `HostRule` row with an `Evidence` grade whose `note`
  names the capture, or nothing.
- **`src/suno/HttpPolicy.hpp`** — the ONE owner of timeouts and response caps, by
  `RequestClass` (`Auth`/`JsonApi`/`MediaDownload`/`Upload`). `setTransferTimeout` is
  a **stall** deadline, not a wall-clock budget; never convert it into one. Every
  value is pinned by `tests/unit/suno/test_HttpPolicy.cpp`.

### Evidence grades are a credential control, not a label

`Captured` (`[T1]`) and `Registered` may carry a credential. `ResponseValue` and
`Lead` (`[LEAD]`) may not — `evidencePermitsCredential()` compiles that, and
`test_CapturedHosts.cpp` pins the tier census. A host that appears only as a URL
*value inside another host's response* has never received a credential, which is
why `ResponseValue` is its own grade rather than a flavour of `Registered`.

Never promote `[LEAD]` to `[T1]` without a **direct human capture**. A `401` proves
route existence and nothing else: no request, no response body.

## Gates before claiming done

```bash
./build.sh --fast --tests --safe
```

Then confirm the Suno suites actually **ran** — a suite in the CMake source list is
not evidence it runs (this has bitten twice; see INVARIANTS § "Testing and the
build"):

- `test_SunoEndpoints` — the fail-closed host allowlist guard.
- `test_CapturedHosts` — tier census and evidence gate.
- `test_HttpPolicy` — timeout and cap values.
- `test_ClipParser`, `test_DownloadQueue` where the change touches them.

Read the discovered count, not the exit code.

## Do not

- **Log a bearer, cookie, `Authorization` header, or a full URL with a query
  string.** Log `url.path()`, not `url.toString()`.
- **Send a cookie, bearer, or automatic remote image request to a host outside the
  captured allowlist.** A new host needs a capture first.
- **Widen a fail-closed filter on inference** — e.g. the downloader's
  `content_type == "mp3"` test. A captured `m4a-opus` item being unplayable is a
  visible error with a pinned test, not a bug to guess past.
- **Treat a cookie name from a user paste as trusted text.** Names are quoted,
  values never are, and an over-long or non-charset name is counted rather than
  reproduced.
- **Record a secret-shaped string in prose** — a note about a real account, clip or
  URL is a leak the same way the literal is.

## Where the work usually lands

`SunoClient` (authenticated requests, serialised), `SunoOrchestrator` (flow),
`DownloadQueue` (in-flight cap, no threads — it is a plain `QObject`),
`ClipParser`/`ClipResolver` (response → model), `SunoLibraryManager` +
`SunoLibraryMutations` (playlist state), `auth/` (Clerk envelopes — never
hand-rolled), `SunoBridge` (QML surface).
