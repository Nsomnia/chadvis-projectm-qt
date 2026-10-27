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
| [`Nsomnia/sunox-chadvis-suno-frontend-projectm-qt-research`](https://github.com/Nsomnia/sunox-chadvis-suno-frontend-projectm-qt-research) (`docs/suno/overview.md`) | `8a20930d0ab9adcce802aeaf4ece492821daef69` | 2026-10-27 | MIT | **No.** It describes local HAR/NetLog corpora and bundle scans by filename; none is committed to that repository. |

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

### What `sunox` is

The second source is `docs/suno/overview.md` in
[`Nsomnia/sunox-chadvis-suno-frontend-projectm-qt-research`](https://github.com/Nsomnia/sunox-chadvis-suno-frontend-projectm-qt-research)
(MIT; retrieved 2026-10-27 at `8a20930d0ab9adcce802aeaf4ece492821daef69`, the
only commit touching that path). `sunox` is a Rust CLI for the same unofficial
Suno web API, and the overview is its dated machine-readable endpoint reference.
Like `suno-cli` it is disciplined about provenance — every claim carries a
`LIVE-READ` / `CURRENT-BUNDLE` / `OFFICIAL` / `INFERENCE` label, sourced from an
audited Pro account, through 2026-09-14, with its own superseded routes retired
in place rather than left to rot. Its "live-verified" is its label, not ours:
nothing here is anything but `[LEAD]` for this repository.

**Studio is excluded from this review by instruction.** The source's Studio
render-state and multitrack-export material is outside the register below; this
file carries only its non-Studio surface, and the Studio delta is limited to a
pointer: the source documents a separate Studio export flow in its own Studio
section, which we have not reviewed here.

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

### 2.1 `sunox` claims that agree with a captured row

Same rule, second source: report-only, zero master edits. Where the source's
*route-level* claim matches a `[T1]` row but its request-shape detail goes
beyond anything we captured, only the route match counts here; the shape detail
lives in section 7.6 as `[LEAD]` names.

| Our row | Their claim | Our disposition |
|---|---|---|
| §4.4 `POST /api/generate/v2-web/` is the live generation submission route | Independent match, including pre-submit `POST /api/c/check` with `{"ctype":"generation"}`. | Unchanged. Already `[T1]`. Its task-variant field values are `[LEAD]` shape names (section 7.6), not route facts. |
| §4.4 `POST /api/prompts/upsample` | Independent match, including the `upsampled`/`request_id` response fields our capture held. | Unchanged. |
| §4.5 `POST /api/feed/v3` cursor-based filtering | Independent match, including an `ids` filter (with `presence` values spelled `"True"`/`"False"`) used as a batch post-submit lookup. | Unchanged. The `ids` filter spelling is an extension of the captured envelope and is a `[LEAD]` shape fact, not a route fact. |
| §4.4 `GET /api/gen/{id}/comments` | Independent match. | Unchanged. |
| §4.5 playlist v2 `tracks/add`, `tracks/remove`, `tracks/reorder-by-index` | Independent match to the 2026-09-30 validation-probe rows. | Unchanged. Its remove-batching behaviour note is in section 7.6. |
| §4.7 `GET /api/custom-model/pending/` | Independent match. | Unchanged. |
| §4.9 shared page-load bootstrap — `/api/modals`, `/api/labs/configs`, `/api/challenge/progress`, `/api/cms/nudges/share-nudge`, `/api/cms/nudges/publish-nudge`, `/api/share/stats`, `/api/realtime/discover`, `/api/profiles/pinned-clips`, `/api/video_gen/pending_batches` | Independent match on all nine, observed by them on 2026-06-30 against routes we captured on 2026-08-25. | Unchanged. Two independent reconstructions of one bootstrap set is consistency, not promotion. |
| §4.2/§4.3 `GET /api/project/me`, `GET /api/project/default`, `GET /api/project/default/pinned-clips` | Independent match. | Unchanged. |
| §4.9 `GET /api/personalization/settings` | Independent match. Its POST sibling is new and is a section 4.1 lead below. | Unchanged. |
| §4.4 `/api/gen/{gen_id}/convert_wav/` and `/api/gen/{gen_id}/wav_file/` | They retain the same two routes as "older compatibility" for WAV, matching our `declared-unused` posture. They add two OPUS siblings — see section 4.1. | Unchanged. |
| §7 "model keys and limits are per-account, never hardcoded" | Their model table (display names per `chirp-hawk`, `chirp-hawk-wild`, `chirp-goose`, `chirp-fenix`, …) is re-read from `/api/billing/info/` on their account on 2026-09-11 — the same rule our §7 states. | Unchanged. Values are per-account; nothing here is a constant. |

Eleven agreements of this kind, zero master edits.

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

### 3.4 `sunox` disagreements

Each of these also has a row in the master's Appendix A. None is resolved here.

- **`GET? /api/generate/cowrite-lyrics/models/`.** Our
  [`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) §4.4 row spells the co-write
  model-discovery route `POST?` (loose normalized scan); `sunox` @ `8a20930d`
  (2026-10-27) calls it `GET` and reports a live authenticated read. →
  Appendix A.
- **`POST /api/clips/adjust-speed/`.** Our §4.6 lead says `GET?` — a
  transcription artifact the section already warns is not trusted; `sunox`
  reports a live `POST` with a body carrying `clip_id`, `speed_multiplier`,
  `keep_pitch`, and `title`, answered by a processing clip directly. →
  Appendix A.
- **`POST /api/clips/delete/`.** `sunox` live-verifies permanent clip deletion
  through this route with `{"ids": [...]}` on 2026-07-10; this master's
  2026-10-26 probe of the same spelling answered `404`. → Appendix A.

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

### 4.1 Paths absent — `sunox`

Each row below is a `[LEAD]` claim from `sunox` @ `8a20930d` (2026-10-27),
verified against our catalogue first: what we already carry as `[LEAD]` was not
re-registered. The authoritative rows now live in
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md) in the sections shown.

| Path | Registered in |
|---|---|
| `POST? /api/download/authorize` | OBSERVED-LEADS.md §4.6 |
| `POST? /api/video_gen/text/generate` | OBSERVED-LEADS.md §4.6 |
| `POST? /api/video_gen/history` | OBSERVED-LEADS.md §4.6 |
| `GET? /api/trending/` | OBSERVED-LEADS.md §4.5 |
| `GET? /api/living_radio/{station_id}/song-list` | OBSERVED-LEADS.md §4.5 |
| `GET? /api/gen/{gen_id}/opus_file/` | OBSERVED-LEADS.md §4.4 |
| `POST? /api/gen/{gen_id}/convert_opus` | OBSERVED-LEADS.md §4.4 |
| `PUT? /api/persona/edit-persona/{persona_id}/` | OBSERVED-LEADS.md §4.7 |
| `PUT? /api/persona/set_visibility/{persona_id}/` | OBSERVED-LEADS.md §4.7 |
| `PUT? /api/persona/trash-persona/{persona_id}/` | OBSERVED-LEADS.md §4.7 |
| `PUT? /api/persona/bulk-trash-personas/` | OBSERVED-LEADS.md §4.7 |
| `GET? /api/voice-verification/phrase/` | OBSERVED-LEADS.md §4.7 |
| `POST? /api/voice-verification/` | OBSERVED-LEADS.md §4.7 |
| `GET? /api/voice-verification/{verification_id}` | OBSERVED-LEADS.md §4.7 |
| `GET? /api/processed_clip/{processed_clip_id}` | OBSERVED-LEADS.md §4.7 |
| `GET? /api/profiles/listen-history` | OBSERVED-LEADS.md §4.8 |
| `POST? /api/profiles/block` | OBSERVED-LEADS.md §4.8 |
| `POST? /api/personalization/settings` | OBSERVED-LEADS.md §4.9 |
| `PATCH? /api/lyrics-projects/{project_id}` | OBSERVED-LEADS.md §4.4 |
| `DELETE? /api/lyrics-projects/{project_id}` | OBSERVED-LEADS.md §4.4 |

Twenty rows. Three neighboring claims are **enrichments of routes we already
carry**, not new rows: `GET /api/clip/{clip_id}/stems?page=` (a zero-based page
query on our §4.6 `stems` lead), `PATCH /api/playlist/v2/{playlist_id}`
(cover-image S3 id and `is_public` on our §4.5 v2 playlist lead), and
`DELETE /api/playlist/v2/{playlist_id}/save` (the unsave sibling of our §4.5
save lead). Their shape facts that go beyond our rows are recorded as
`[LEAD]` names in section 7.6 — request **bodies are not reproduced here**, per
the standing rule. `POST /api/download/authorize` extends the download
preparation family in §4.6 of
[`OBSERVED-LEADS.md`](OBSERVED-LEADS.md); the family's written disposition there
(rows and their "none may be added to `CapturedHosts.cpp`" note) is unchanged.

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

### 5.3 `sunox` and the API base host — commentary, not a conflict

`sunox` @ `8a20930d` treats `https://studio-api-prod.suno.com` as its API base
through 2026-09 (its last dated claim is 2026-09-14), which was correct against
everything we had held through 2026-10-22 — including our own §5.12 probe of
that date. Our 2026-10-24 browser HAR and the 2026-10-26 route probe
(`TODO.md` T0333/T0336) changed our picture, not theirs: the signed-in browser
now calls the API on `https://suno.com` under the `/a/api` base, and the old
Studio host answers those routes as a **byte-identical live alias** for the
routes probed. The master's §2.1 host table records this (2026-10-26). This is a
dated difference, not a disagreement about one date: nothing in `sunox`
contradicts our newer evidence, and no Appendix A row is warranted.

Disposition: no host change, no allowlist change, no `src/` change. Both host
names are already `[T1]` in the master's §2.1 with the current characterization
(`studio-api-prod.suno.com` kept in code as a captured host, the new base used
by `API_BASE`). `sunox` names no path under `/a/api` and no other new host, so
section 4 of this review adds no §5 row for it; the captcha-provider hosts it
mentions are already covered by the §5 intro above.

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

### 6.1 What `sunox` cannot settle

- **Entitlements and per-account gates.** Its `aug-creativity` / `mumble-mode` /
  `max-mode` session-flag gating, plan features (`get_stems`, `custom_models`,
  `generate_song_image`, `generate_song_video`), and model length limits were
  all read on **its Pro account**. Our §7 already holds that flags, credit
  limits, and per-account model availability are per-account and never
  hardcoded; nothing here changes that.
- **Its `OFFICIAL` labels.** Stem-tier pricing (Auto Split 12-stem 50 credits;
  Split-from-Mix 10/20; Premier-only Advanced Split) and the 2026-09-03
  download-accounting policy come from Suno Help-Center articles, not wire
  observations. Documented product text is not a request/response contract for
  us; it is carried in section 7.6 only as [LEAD] context.
- **Its request bodies and field types.** Request field *names* are leads and
  are recorded as names only (section 7.6); no third-party body is reproduced,
  and no field type, default, or response shape is adopted as a fact. Its
  "live-verified" claims are its conversations with its own account.
- **Cowrite cost.** It explicitly does not promise `/api/generate/cowrite-lyrics/`
  is free or permanently exempt from anti-abuse checks; neither do we, and our
  row is `declared-unused`.
- **Studio.** Excluded by instruction — everything on the Studio side
  (render-state, multitrack export, Studio-gated stems export) is out of scope
  of this review and adds no row anywhere here.
- **The API base.** See section 5.3: its 2026-09 base-host claim and our
  2026-10 route-probe answer are different dates, not a conflict, and it cannot
  speak to what changed after 2026-09-14.

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

### 7.6 Server behaviours documented by `sunox`

All `[LEAD]`, second source, dates kept from the source (latest 2026-09-14).
Where the claim repeats a `suno-cli` row above, it is corroboration only.

**Repeating an existing row — corroboration, no edit:**

- **A `2xx` is not a success** (§7.1): a second independent client reports
  `POST /api/generate/v2-web/` answering `HTTP 200` with `{"status":"error",
  "clips":[]}` after credits were drawn, and treats `status: "error"` or an
  empty `clips[]` as failure. Our §5.9/T0143 discipline is unchanged.
- **JWT staleness** (§7.2): the same ~30-minute rejection window despite a
  valid `exp`.
- **Trash route shape** (§7.3): identical shape and restore semantics
  (`{"clip_ids":[...], "trash":bool}` → `{"ids":[...], "is_trashed":bool}`).
- **Feed by-ids pair batching** (§7.5): four or more mixed-batch identifiers
  return only the first two, so it batches in pairs.
- **`Browser-Token` construction** (§7.5): base64 of a timestamp-shaped JSON
  wrapper, regenerated per request.

**New behaviours — names only, no bodies reproduced:**

- **`metadata.control_sliders.aug_creativity` must be an integer 0–4.** A
  fractional value returned a deterministic `400` and produced no clip
  (live, 2026-09-11, v6). Integer values were preserved in completed clips.
- **`/api/session/` gates which sliders survive.** The source gates Variety on
  the `aug-creativity` flag, Mumble on `mumble-mode`, and Max Mode on
  `max-mode` (all `[LEAD]` flag names, read from its account). When the
  `mumble-mode` gate is absent, a request carrying `metadata.is_mumble=true`
  was answered — submit and finished clips — with `is_mumble=false`: the server
  clears it, the client cannot force it. `is_max_mode=true` is preserved, but
  output length is **server-authoritative** (one output far exceeded the
  requested duration).
- **`duration` is not honoured in description mode.** A v6 simple/description
  request with top-level `duration: 10` completed at ~73–80 seconds:
  acceptance did not mean compliance. The source's current-Web reading is that
  duration is sent only for v6 Custom mode.
- **`metadata.last_tags_generation` is never fabricated.** The tags/request_id
  carried into a generation submit exist only when `POST /api/prompts/upsample`
  actually ran; `personalization_enabled` is part of the captured submit shape,
  not something the client may inject independently.
- **`captcha_version` is per-request.** Its June HAR held both `required`
  values at `captcha_version: 1`, while its later code maps version `2` to
  Turnstile. Our master resolves `POST /api/c/check` as `[T1]` version `2` =
  Turnstile (Appendix A); the value varies per request/account and must be read
  from the response, not assumed.
- **`/api/persona/` create answered `HTTP 409` means "already exists for this
  clip"** — a useful recovery signal, per the source: read list/detail before
  replaying a lost create response.
- **`/api/generate/concat/v2/` needs original generation history.** A live
  submission from an original generation source completed; one whose source was
  an `edit_fade` product was rejected with `Bad history.`
- **`/api/generate/lyrics-infill/` has a dedicated `400` branch** — detail
  `Lyrics too long to enhance.`
- **Large playlist `tracks/remove` batches can answer `500` while removing the
  same clips one request per clip succeeds.** Removal discipline is per-clip
  with partial-mutation bookkeeping (`requested`/`succeeded`/`failed`/
  `not_attempted` id sets) — a recovery pattern, keyed to the route our master
  already carries as `[T1]`.
- **Legacy `POST /api/playlist/set_metadata` `image_url` can fail with
  "Failed to upload image" for freshly uploaded covers**; the playlist-v2 PATCH
  with `cover_image_s3_id` is the path the source prefers for that case.
- **Downloads are gated on `is_download_unlocked`.** Only a clip whose decoded
  value is exactly `true` may fetch without `POST /api/download/authorize`
  (field names `item_id`, `item_type`); prepared fetches take
  `GET /api/download/clip/{clip_id}?format=mp3|m4a|wav|mp4`. The authorize call
  is metered, not replay-safe, and must never be blind-retried after an
  ambiguous transport result (a redirect is itself ambiguous). Billing
  advertises the consumption: `download_usage` with
  `current_period_downloads_limit`, `current_period_downloads_used`,
  `additional_download_remaining`, plus optional `download_credit_packs` —
  illustrative field names, not runtime constants (master §5.13 rules apply).
  Stems share their parent's authorization: authorize the source once, never
  per stem id.
- **Fade/crop are two-step.** `POST /api/edit/fade/{clip_id}/` and
  `POST /api/edit/crop/{clip_id}/` (carrying `is_crop_remove`) answer with an
  `action_clip_id` to poll at `GET /api/edit/action/{action_clip_id}/` until
  `status: "complete"`, while `/api/clips/adjust-speed/` and
  `/api/clips/reverse-clip/` return a processing clip directly.
- **Stem extraction rides the generation route.** Current web stem extraction
  is a `POST /api/generate/v2-web/` submission, field names `task: "gen_stem"`
  (mv forced `chirp-v3-0`), `stem_type_id`, `stem_type_group_name`,
  `stem_task`, `stem_name`; complement results are classified from
  `stem_task == "remove"` or group `Instrumental`. Result pages are read via
  `GET /api/clip/{clip_id}/stems/pages` and `GET /api/clip/{clip_id}/stems?page=`
  (zero-based). Stem tier and instrument-availability rules are Help-Center
  claims (§6.1): Pro must not be offered Premier-only instruments even though
  the full instrument catalogue exists in the bundle.
- **Cover art has two distinct transports.** A direct `POST /api/gen/prompt_image/`
  whose consumed response field is `image_url`, and a separate
  `POST /api/video_gen/image/generate` batch (name-level: prompt hard-sliced at
  800 UTF-16 units, `quantity` 2, `image_gen_category`, optional `prompt_images`
  and `aspect_ratio` `1:1`) whose status is recovered through the
  `pending_batches`/`poll_batches` pair our master already carries.
- **Remaster parameters exist as names.** `POST /api/generate/upsample`
  carries `variation_category` (`subtle|normal|high`) for
  `chirp-halibut`/`chirp-flounder`/`chirp-carp` and — v6 `chirp-halibut` only —
  `style_profile` (`natural|boost|clarity`); `chirp-bass` omits variation
  entirely and an explicit value is rejected. Two builder-only fields are
  documented by name and rejected by its own account: `tags` (staff-only) and
  `freedom`. Source gates: complete, non-trashed, non-infill, ≤960 s, and the
  server `action_config` must expose Remaster visible and enabled. Names only —
  our §4.4/OBSERVED-LEADS row is unchanged, and no default is inferred.
- **Uploads are guarded before presign.** Accepted audio extensions
  `mp3,m4a,wav,flac,ogg,aac`, maximum 524,288,000 bytes; `upload_type` enum
  `file_upload|studio_file_upload|audio_recording|voice_recording|video_recording|marketplace_submission|stem_mix|external_daw_sample`;
  image uploads (`png|jpg|jpeg|webp`) require `moderation_status: "approved"`
  before the finished bytes are usable. Our §5.6 three-step audio contract and
  the §4.6 upload leads are unchanged.

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