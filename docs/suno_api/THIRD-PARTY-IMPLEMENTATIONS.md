# Third-Party API Implementation Sources

**Role:** non-contractual register of what other clients believe the Suno API is
**Status:** opened 2026-10-10
**Canonical master:**
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md)

## The one rule

**Nothing in this file is evidence, in either direction.**

A third-party client that reports traffic against its own account has observed
*its* corpus, on *its* date, with *its* entitlements. That is not a capture by
this repository, so it can neither promote a row into the inventory nor demote
one out of it — not even where our own local capture bytes are currently
unverifiable (see [`raw/README.md`](raw/README.md)).

Every claim below is `[LEAD]` under
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) section 1.1, which includes
"another client's source" in the `[LEAD]` definition. A source's own vocabulary
is not adopted: where a source calls something "live verified", this file says
`[LEAD]` and keeps the date. **Agreement is not a stronger grade, and
disagreement is not a weaker one.**

A third-party claim can do exactly three things:

1. add a `[LEAD]` capture target to
   [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md),
2. add a conflict row to the master's
   [Appendix A](ENDPOINT-INVENTORY.md#appendix-a--explicit-conflict-register),
3. serve as an engineering reference, on its own merits, adopted through our own
   tests and recorded in [`../../CHANGELOG.md`](../../CHANGELOG.md) — never here.

That is the whole list. It cannot become a route constant, a request builder, or
an allowlist entry.

## 1. Source register

| Source | Pinned commit | Retrieved | License | Ships capture artifacts? |
|---|---|---|---|---|
| [`paperfoot/suno-cli`](https://github.com/paperfoot/suno-cli) | `6d28c67a733b32183a80d81306bf0013f46b00b7` | 2026-10-10 | MIT | **No.** Its `conformance/` validates its own CLI output schemas, not captured traffic. |

Pins are **commit SHAs**, never tags or branch names, so a stale citation is
visible by grep and cannot silently drift. A re-read at a different SHA is a new
row, not an update to this one.

The tree was reviewed in a gitignored scratch clone. It is **not vendored** into
this repository: raw third-party material stays outside the worktree, exactly as
our own raw captures do. This file is the entire retained artifact.

### What this source is

A Rust CLI for the unofficial Suno web API, actively maintained through
2026-09-28. It is unusually disciplined about its own evidence — its
`API_INTELLIGENCE.md` separates direct observation from bundle evidence, keeps
dated claims, and explicitly retires its own obsolete routes rather than leaving
them to rot. That is a mark of a careful source, not of a reliable one for our
purposes: care about provenance is not provenance.

## 2. Claims that agree with a captured row

These produce **no edit to the master**. They are recorded so that a later
session does not mistake a `[T1]` we already hold for something this source
confirmed, or re-litigate it as if it were new.

| Our row | Their claim | Our disposition |
|---|---|---|
| §5.4 `token_provider` is an **integer**, `1` = hCaptcha, `2` = Turnstile | Independent match. | Unchanged. Already `[T1]` from the 2026-09-30 recon. |
| §7 captured model keys `chirp-hawk`, `chirp-hawk-wild`, `chirp-goose`, `chirp-halibut` | Independent match on all four. | Unchanged. Already `[T1]` response values. |
| §2.2 `Device-Id` and `Browser-Token` are part of Studio request conventions | Independent match. | Unchanged. Already `[T1]`. |
| §4.4 `/api/generate/v2-web/` is the live generation path | Independent match, and consistent with gating plane 7 (`configs.gen-endpoint`). | Unchanged. |
| §4.5 `/api/feed/v3` uses an opaque cursor, and page numbers are not part of the route | Independent match. | Unchanged. |
| §5.4 the legacy `/api/generate/v2/` is dead and creation is routed exclusively through `v2-web` | Independent match, consistent with plane 7. | Unchanged. |
| §1.5 the `loc` chain in a validation error is **not** a required request wrapper | Independent match, and it reaches the same conclusion from the opposite direction — the route it documents returns `body.spec.*` locations while rejecting a `spec`-wrapped body. | Unchanged. This is the strongest corroboration in the set: two sources, two routes, same finding. |
| §4.5 completion is observed through the feed, not a per-clip poll | Independent match, plus a third spelling — see the `GET /api/feed/?ids=` row in section 4. | Unchanged. |

Five agreements, zero master edits. **Agreement is not news.** Rewording a
`[T1]` because a second source says the same thing is how a capture quietly
acquires an unverifiable dependency.

## 3. Claims that disagree with a captured row

Each of these also has a row in the master's Appendix A. None is resolved here.

### 3.1 `media_urls[]` is not the download contract — **the highest-value claim**

This source states plainly that *"playback URLs are not treated as download
contracts"*, and implements downloading as a **two-step preparation**: ask a
preparation route for a short-lived signed URL, then transfer from that URL,
re-preparing once if it expires. Suno's own web bundle describes the same shape.

Our inventory previously called `media_urls[]` "the playback/**download**
authority", and `src/suno/SunoDownloader.cpp` implements exactly that reading.

Both are corrected in the master (section 5.3), on our own captures rather than
on their authority: the captured shape proves the array's *fields*, not its
fitness for download. `mp3` is **zero occurrences in 40 of 40** observed entries
in our 2026-09-30 corpus, and the legacy `audio_url` carried a `/api/forbidden`
sentinel in our 2026-09-24 sample. Neither field is an established download
contract.

The preparation family stays `[LEAD]` until we capture it. It is registered in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) section 4.6 as a written capture target
— which is the real value here: a route family our two existing corpora missed
is now a question we know how to ask.

### 3.2 `/api/edit/stems/{clip_id}` trailing slash

They spell it **without** a trailing slash; our lead carries one. No capture of
ours selects either. Neither may be rewritten into the other. Recorded in
Appendix A.

Note the trap in adjacent rows: path template *parameter names* (`{id}` versus
`{clip_id}`) are **not** on the wire — the segment carries a value. Recording
template-name churn as drift would pad the conflict register with noise and
bury the rows that matter.

### 3.3 Prohibited behaviour

**Its success is not our licence.** One route in this source is documented as
citing a *third* hand — a comment in its stem-separation module attributes the
path to another project, which itself attributed it to a third. That is four
hands from the wire. Our
[promotion criteria](OBSERVED-LEADS.md#promotion-criteria) already reject a row
that cites another row "no matter how confident it reads". The rule generalises:
**a claim's grade is a function of its provenance, not of how many repositories
agree with each other.**

## 4. Paths absent from our catalogue

The authoritative rows live in [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) — this
section is an **index only**, and deliberately carries no Method/Auth/Purpose
columns, because a duplicated row is a second fact with a second owner.

Highest-value capture targets, from this source:

| Path | Registered in |
|---|---|
| `GET? /api/download/clip/{clip_id}?format=` | OBSERVED-LEADS.md §4.6 |
| `GET? /api/studio/clip/{clip_id}/download?format=` | OBSERVED-LEADS.md §4.6 |
| `POST? /api/download/authorize` | OBSERVED-LEADS.md §4.6 |
| `POST? /api/gen/trash` | OBSERVED-LEADS.md §4.6 |
| `GET? /api/feed/?ids=` | OBSERVED-LEADS.md §4.5 |

That last one matters more than its `[LEAD]` grade suggests. Our §5.4 records
completion polling as observed through `POST /api/feed/v3` and
`GET /api/clips/get_songs_by_ids`. This source uses a **third** spelling —
a bare `GET /api/feed/` carrying an `ids=` query, answered with a bare JSON
array rather than a feed envelope. Three spellings across three clients is
exactly the situation where a `[T1]` path template is safer than a hardcoded
path, and it is a cheap capture.

It also independently corroborates four paths we already carry as `[LEAD]` —
`/api/gen/{id}/convert_wav/`, `/api/gen/{id}/wav_file/`,
`/api/generate/concat/v2/`, `/api/edit/stems/{clip_id}` — which is worth
recording and is worth nothing more.

A separate, larger set of route strings was recovered directly from the 2026-09
web bundle corpus at `~/Documents/suno-recon` (outside this repository). Those
are `[LEAD]` on the same terms and are registered in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) under the bundle-corpus evidence
phrase. **Bundle literals are not captures** — §5.3 already records the
distinction for `audiopipe.suno.ai`, which appears in that corpus only inside a
hostname regex and a constructed URL template.

## 5. Host observations — explicitly NOT allowlist candidates

**This section adds nothing to `src/suno/CapturedHosts.cpp`. It exists so that a
future session does not mistake a name below for permission.**

| Host | Where it comes from | Disposition |
|---|---|---|
| `challenges.cloudflare.com` | A captcha challenge endpoint. | **Never allowlisted.** Naming a captcha provider does not add a host (§2.1). |
| `hcaptcha-{endpoint,assets,imgs,reportapi}-prod.suno.com` | hCaptcha service endpoints and assets. | **Never allowlisted.** `raw/README.md` already records them as deliberately excluded. |
| **The host of a prepared download URL** | **Unknown to us.** | **The important one — see below. |

### 5.1 The signed-download host is the worst thing in this material

This source fetches the `download_url` returned by its preparation routes with
**a scheme check and nothing else** — `downloads.rs:161` tests
`parsed.scheme() != "https"` and no host comparison anywhere in the path.

We have never captured either preparation route, so **we have never seen what
host a prepared `download_url` actually points at.** Our CloudFront and
`audiopipe` `[T1]` entries come from `media_urls[]` in feed bodies — *playback*,
a different artifact from a *prepared download*. The two must not be conflated.

Adding a host on the strength of a scheme-only check would convert a fail-closed
client into one that fetches from **any HTTPS host the server names**, carrying
a session cookie and an account-scoped signed URL. Do not.

### 5.2 No other host was introduced

The source names no new `suno.ai` CDN origin, no new S3 bucket, and no new
regional API host. Its base API host is the production Studio host we already
hold as `[T1]`.

## 6. What this source cannot settle

- **The native-callback gate.** It implements **no loopback callback at all**;
  its only `127.0.0.1` uses are local test listeners and a Chrome DevTools
  Protocol endpoint for captcha solving. It takes a Clerk cookie or JWT on
  stdin — a different architecture entirely. This is evidence about nothing on
  the loopback question. `TODO.md` T0145's capture gate, T0166, and every proof
  requirement in
  [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) are unchanged and
  must not be "fixed".
- **T0177's nine drifted spellings.** It is not a systematic negative source —
  its route table has eight rows, and its historical rows deliberately *retain*
  retired paths as recapture leads. **Its silence is about its scope, not about
  Suno's.** A partial enumeration cannot produce a negative, and its own task
  line already says a static negative is weak evidence. It re-orders that queue;
  it closes nothing.
- **Any response shape, status code, or field type.** Request field *names* are
  leads. Bodies are deliberately not reproduced here — pasting a third party's
  request body invites a future reader to diff it against our own recovered
  schema in §5.9 and "correct" ours.
- **Per-account values** — credits, limits, entitlements, model availability.
- **Model retirement.** Its claim that pre-v6 models were retired on 2026-09-09
  is a dated, time-dependent server fact with no captured support from us. §7
  keeps its "do not hardcode a generation key" rule and is not edited.

## 7. Server behaviours this source documents

All `[LEAD]`. Each is a claim about how Suno's server behaves, offered here
because each one is cheap to test and expensive to guess. None is a contract, and
none authorizes an assertion to a user.

### 7.1 A `2xx` is not a success

`POST /api/generate/v2-web/` answers **`HTTP 200` with
`{"status": "error", "clips": []}`** when it refuses work — after the credits
were drawn. The source treats either a `status` of `error` or an empty `clips[]`
as a failure and refuses to report success.

This is the strongest possible argument for our existing §5.9 discipline, and it
arrives from outside: **a success status code and a non-empty result are
different things**, and only the second one means the work happened. Our own
T0143 already refuses to parse a mutation's 2xx; this says the same hazard exists
on a paid generation route.

### 7.2 A JWT can be rejected while its own `exp` says it is valid

The source reports that Suno issues ~1-hour JWTs but the generation endpoint
**rejects tokens older than roughly 30 minutes** with a `Token validation failed`
body, regardless of the token's own expiry claim. It therefore refreshes on a
30-minute threshold rather than on expiry.

Our §3.3 and §6.1 record captured `401` handling but not this staleness window.
It is worth a capture: if true, refreshing on `exp` is not merely late, it is
insufficient — and it would look exactly like a session-expiry bug.

### 7.3 The trash route moved

`POST /api/feed/trash` with `{"ids": [...]}` is reported dead; the replacement is
`POST /api/gen/trash` with a **top-level** `{"clip_ids": [...], "trash": bool}`
answering `{"ids": [...], "is_trashed": bool}`, where `trash: false` restores.

Our `src/suno/SunoEndpoints.hpp` already names `/api/gen/trash` as a `[LEAD]`
bundle string and deliberately omits it from the mutation set. That decision
stands — but the *request shape* is now a written capture target, and the
reported failure mode (`404`, not `405`) is a useful disambiguator when probing.

### 7.4 Word-level lyrics live under `aligned_words`

The aligned-lyrics response carries the array under **`aligned_words`**, with
per-word `word`, `start_s`, `end_s`, `success`, and `p_align`. A missing key is
an error here, not an empty result.

This matters to us specifically: `TODO.md` T0071 records that word-level karaoke
timing is implemented in C++ but **never surfaced in QML**. If the field name is
right, the feature's data source is already known and only the bridge is missing.

### 7.5 Smaller operational notes

- **Feed by-ids is capped.** Four or more identifiers drawn from different
  batches reportedly return only the first two, so the source batches requests in
  pairs. Worth measuring before any library paging work depends on it.
- **`Browser-Token` is generated per request** as
  `{"token":"<base64 of {"timestamp":<unix-ms>}>"}`. Our §2.2 records the
  header and that it encodes a timestamp-shaped JSON value; this is the
  construction, still `[LEAD]`.
- **`can_use` may be absent from the billing response**, not merely false. Our
  §7 records a captured `can_use: false` on the *session* catalogue. These are
  different routes, so this is **not** a conflict and no Appendix A row is
  warranted — it is a question: does the field appear on `/api/billing/info/` at
  all?
- **Pre-v6 generation models are reported retired** as of 2026-09-09. §7's "do
  not hardcode a generation key" rule is unchanged and this adds nothing to it.

## 8. Standing rule for the next source

Before a new implementation is added to section 1, answer three questions:

1. **Is the pin a commit SHA?** A tag or branch name is not a citation.
2. **Does it ship capture artifacts?** A sanitised request/response pair is
   still `[LEAD]` for us, but it is a *different kind* of lead, and the
   distinction is cheap now and expensive to reconstruct later.
3. **Does it name a host we do not hold?** If yes, section 5 gains a row and
   `CapturedHosts.cpp` gains nothing.

If a source is being considered as grounds to **promote** a row, stop: that is
the one use this file forbids, and it is the only use that would need a human
capture to override.