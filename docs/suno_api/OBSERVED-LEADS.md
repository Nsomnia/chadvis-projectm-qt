# Observed but Non-Contractual

**Role:** research and capture planning only
**Status:** non-contractual inventory extracted from the canonical master on
2026-09-26; no row's evidence status changed during that extraction
**Canonical master:**
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md)

## Preamble — this file is not a contract

Nothing in this file is a supported contract, a client contract, or an
implementation source. Every row here is a research lead. It exists so that the
canonical master can stay a spec instead of a junk drawer, and so that the next
capture attempt has a written target list.

The governing rule is the master's own `[LEAD]` definition, copied verbatim from
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) section 1.1:

> **Meaning:** Found in a JS/HTML scan, raw endpoint dump, old inventory,
> reconstructed path, or prose without a direct capture. An unlabeled old row is
> always a lead.
> **Permitted use:** Research and capture planning only.

Therefore:

- **Never a stable contract.** No client may depend on a row here behaving the
  way its Method, Auth, or Purpose column suggests.
- **Never an implementation source.** No row here authorizes a constant, a
  request builder, a QML action, or a retry policy. `src/` is not evidence.
- **Never wire it without a direct capture.** Promotion requires the
  [promotion criteria](#promotion-criteria) below to be met and the row to be
  added to the master with its own evidence label.
- **Nothing here was promoted by being moved.** Extraction is not promotion. The
  evidence label on each row is exactly the label it carried in the master.

### Why this file exists: the "old inventory" pattern

Read the Evidence column before reading anything else. A large share of these
rows cite **`old inventory`** as their *only* source — for example
`` `[LEAD]` old inventory ``, with no scan, capture, or prose of its own.

The master declares older topic prose and superseded inventories non-authoritative
(section 1.2, item 6). So these rows **transcribe a document the master already
declares superseded**, in a second-hand copy whose own provenance is unrecorded.
That is the single most important thing to know about this file: most of it is
a rumour of a rumour.

Consequences to keep in mind while reading:

- Row counts here are a rough measure of historical scraping breadth, not of
  current API surface, and definitely not of supported surface.
- A path appearing here is not evidence that the path ever responded, that it
  ever existed on a current host, or that it is reachable by any account.
- The correlation between "cited old inventory" and "zero capture evidence" is
  near total. Treat an old-inventory-only row as a *question to put to the
  server*, not as a route to call.

Rows whose source is a **raw scan**, **loose normalized scan**, **current
endpoint constants**, or **current endpoint map** are better sourced than
old-inventory rows, but none of them is a capture either. In particular
`current endpoint constants` means "`src/suno/SunoEndpoints.hpp` says so", which
is an implementation mirror and explicitly not evidence.

## Evidence-label legend

The label authority is
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) section 1.1. This file does not
define, extend, or reinterpret a label.

| Label | Meaning (master section 1.1) | Where it belongs |
|---|---|---|
| `[T1]` | Directly present in a cited request/response capture, with secrets redacted. | The master. Not this file. |
| `[LEAD]` | Found in a scan, raw endpoint dump, old inventory, reconstructed path, or prose without a direct capture. An unlabeled old row is always a lead. | Here, as a capture target. |
| `[VERIFY]` | Sources conflict, or one part of the route contract is not captured. | Here, with the conflict recorded in the master's conflict register (Appendix A). |

Trailing-`?` method notation, `Bearer?`, and path-spelling sensitivity carry the
master's section 1.1 meaning unchanged. A `?` on a method means the method came
from prose or reconstruction, not from a capture.

### `Implemented` column

Route tables in this file carry the master's `Implemented` enum, defined in
[`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) section 1.3:

- `wired` — the route constant exists **and** is referenced from `src/`
- `declared-unused` — the constant exists in `src/suno/SunoEndpoints.hpp` but has
  zero references in `src/`
- `not-in-code` — no constant exists; documented from a scan or prose only

`Implemented` records the state of *this repository's* code, which is not
evidence of anything Suno serves. `declared-unused` is a code-hygiene fact, not a
promotion. No row in this file may be `wired` for an auth reason: a route with
capture evidence belongs in the master regardless of whether the client calls it.

The flag-name and page-route tables below have no `Implemented` column at all,
because they are not routes.

## 4.1 Billing and account commerce — leads

Master section: 4.1. No captured billing mutation exists; see master 5.8.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET? | `/api/billing/default-currency` | Bearer? | Default/account currency | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/billing/get-discount-offer` | Bearer? | Personalized discount offer | `[LEAD]` old inventory/scan | not-in-code |
| GET? | `/api/billing/get-churn-survey-options` | Bearer? | Cancellation survey options | `[LEAD]` old inventory/scan | not-in-code |
| GET? | `/api/billing/tax-info` | Bearer? | Tax metadata | `[LEAD]` old inventory/scan | not-in-code |
| GET? | `/api/billing/change-plan/preview/` | Bearer? | Plan-change preview | `[LEAD]` old inventory/scan | not-in-code |
| GET? | `/api/billing/purchase-info/{purchase_id}/` | Bearer? | Purchase/checkout information | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/create-session/` | Bearer? | Create checkout/subscription session | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/create-portal/` | Bearer? | Open customer billing portal | `[LEAD]` raw scan | not-in-code |
| POST? | `/api/billing/change-plan/` | Bearer? | Change subscription plan | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/cancel-sub/` | Bearer? | Cancel subscription | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/cancel-sub/undo/` | Bearer? | Undo pending cancellation | `[LEAD]` raw scan | not-in-code |
| POST? | `/api/billing/pause-sub/` | Bearer? | Pause subscription | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/unpause-sub/` | Bearer? | Resume subscription | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/submit-survey/` | Bearer? | Submit churn/cancellation survey | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/billing/accept-sub-coupon/` | Bearer? | Accept/apply subscription coupon | `[LEAD]` raw scan | not-in-code |
| POST? | `/api/billing/set-default-payment-method/` | Bearer? | Set default payment method | `[LEAD]` old inventory/topic prose | not-in-code |
| `?` | `/api/billing/auto-reload` | Bearer? | Auto-reload root surface | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/billing/auto-reload/enable` | Bearer? | Enable auto-reload | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/billing/auto-reload/disable` | Bearer? | Disable auto-reload | `[LEAD]` loose normalized scan | not-in-code |

The three `auto-reload` rows have no method at all — not even a reconstructed
one. The captured `POST /api/billing/auto-reload/nudge-check` is a different
route and is in the master.

## 4.2 User, backend session, onboarding, and account — leads

Master section: 4.2.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET | `/api/auth/verify-token` | Bearer? | Backend token verification | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/user/me` | Bearer? | Current user profile | `[LEAD]` old inventory/scan | not-in-code |
| POST? | `/api/user/update_user_config/` | Bearer? | Update user config | `[LEAD]` old inventory/topic prose; mutation schema unproved | not-in-code |
| POST? | `/api/user/reset_onboarding/` | Bearer? | Reset onboarding | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/user/accept_timbaland_terms/` | Bearer? | Accept feature-specific terms | `[LEAD]` old inventory/topic prose | not-in-code |
| DELETE? | `/api/user/delete-account/` | Bearer? | Destructive account deletion | `[LEAD]` old inventory/topic prose; do not call accidentally | not-in-code |
| `?` | `/api/user/vip_program_acceptance` | Bearer? | VIP-program acceptance surface | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/signout/` | Bearer?/session? | Sign-out surface | `[LEAD]` raw/loose scan | not-in-code |
| `?` | `/api/onboarding/submit` | Bearer? | Onboarding submission | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/onboarding/complete` | Bearer? | Onboarding completion | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/onboarding/skip` | Bearer? | Onboarding skip | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/onboarding/back` | Bearer? | Onboarding back navigation/state | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/onboarding/audio-upload/abort` | Bearer? | Abort onboarding upload | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/onboarding/audio-upload/remove` | Bearer? | Remove onboarding upload | `[LEAD]` loose normalized scan | not-in-code |

### 4.2.1 Deleted: the `/api/me/*` v1 + v2 transcription block

The master previously carried 33 consecutive `[LEAD]` rows transcribed verbatim
from the old inventory — `/api/me/`, `/api/me/v2`, `/api/me/v2/cover-art`,
`/api/me/v2/history`, `/api/me/v2/hooks`, `/api/me/v2/personas`,
`/api/me/v2/playlists`, `/api/me/v2/studio-projects`, `/api/me/v2/trash`,
`/api/me/v2/workspaces`, `/api/me/cover-art`, `/api/me/external_accounts`,
`/api/me/followers`, `/api/me/following`, `/api/me/history`, `/api/me/hooks`,
`/api/me/liked-hooks`, `/api/me/liked-playlists`, `/api/me/lyrics`,
`/api/me/organization_invitations`, `/api/me/organization_memberships`,
`/api/me/organization_suggestions`, `/api/me/passkeys`, `/api/me/personas`,
`/api/me/playlists`, `/api/me/sessions`, `/api/me/sessions/active`,
`/api/me/styles`, `/api/me/totp`, `/api/me/totp/attempt_verification`,
`/api/me/trash`, `/api/me/workspaces`, and `/api/me/studio-projects` — every one
of them sourced to `old inventory` and nothing else.

That block was **deleted outright** on 2026-09-26 rather than moved here, because
it had zero capture evidence, zero independent source, and zero value as a
capture target: the paths are a flat alphabetical dump of one superseded
inventory, with no observed method, auth, or schema even claimed. Reproducing 33
more rows here would only make this file look authoritative.

Recorded here as a deletion, not a lead. If a future capture ever surfaces a
genuine `/api/me/*` contract, it enters the master as a new `[T1]` row; the old
inventory's spellings are not a prior to match against.

## 4.3 Projects and Studio — leads

Master section: 4.3. The four captured project routes stay in the master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET? | `/api/project` | Bearer? | Project listing/creation-family surface | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/project` | Bearer? | Create project | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/trash` | Bearer? | Trashed projects | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/invites` | Bearer? | Project invitations | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/clips` | Bearer? | Project clips | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/metadata` | Bearer? | Project metadata | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/pinned-clips` | Bearer? | Project pinned clips | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/collaborators` | Bearer? | Collaborator list | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/collaborators/me` | Bearer? | Current user's collaborator state | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/ably-token` | Bearer? | Realtime collaboration token | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/{project_id}/ably-client-id` | Bearer? | Realtime client identifier | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/project/{project_id}/invite` | Bearer? | Invite collaborator | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/project/{project_id}/ably-update` | Bearer? | Publish collaboration update | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/project/feed` | Bearer? | Project feed | `[LEAD]` loose normalized scan | not-in-code |
| GET? | `/api/project/library/images` | Bearer? | Project image library | `[LEAD]` loose normalized scan | not-in-code |
| GET? | `/api/project/library/videos` | Bearer? | Project video library | `[LEAD]` loose normalized scan | not-in-code |
| POST? | `/api/studio/create-project` | Bearer?/entitlement? | Create Studio project | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/studio/save-project` | Bearer?/entitlement? | Save Studio project state | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/studio/render-state` | Bearer?/entitlement? | Studio render state | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/studio/render-state-multitrack` | Bearer?/entitlement? | Multitrack render state | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/studio/project-version/{id}` | Bearer?/entitlement? | Studio project version | `[LEAD]` old topic prose | not-in-code |
| `?` | `/api/studio/` | Web session? | Claimed Studio access/page surface on API host | `[VERIFY]`; may be a page route, not a stable API call | not-in-code |
| `?` | `/api/studio/{slug}` | Web session? | Claimed Studio-by-slug surface | `[VERIFY]`; colon-form and slash-form scan artifacts are not canonical | not-in-code |

The last two rows are `[VERIFY]` because the artifact itself is ambiguous: a
`/api/studio/` prefix on an API host may be a page route, and `/api/studio/{slug}`
was reconstructed from a colon-form scan artifact. Do not normalize either into
an API call.

## 4.4 Generation, lyrics, prompts, and analysis — leads

Master section: 4.4. All captured generation, captcha, lyrics-project, prompt,
and analysis routes stay in the master. Generation **submission** is additionally
disabled in the client for a separate reason — the CAPTCHA token flow and durable
processing contract are unimplemented — which is an implementation decision, not
an evidence statement.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| POST? | `/api/generate/v2/` | Bearer? | Non-web generation variant | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/generate/cowrite-lyrics/models/` | Bearer? | Co-writing model discovery | `[LEAD]` loose normalized scan | not-in-code |
| POST? | `/api/generate/lyrics/` | Bearer? | Start lyrics-generation job | `[LEAD]` old topic prose; not exercised in reviewed capture | not-in-code |
| GET? | `/api/generate/lyrics/{id}` | Bearer? | Poll lyrics-generation job | `[LEAD]` old topic prose; not exercised in reviewed capture | not-in-code |
| POST? | `/api/generate/lyrics-infill` | Bearer? | Lyrics infill, no trailing slash | `[LEAD]` raw scan | not-in-code |
| POST? | `/api/generate/lyrics-infill/` | Bearer? | Lyrics infill, trailing slash | `[LEAD]` raw scan; do not alias automatically | not-in-code |
| POST? | `/api/generate/lyrics-mashup` | Bearer? | Lyrics mashup | `[LEAD]` raw scan/topic prose | not-in-code |
| POST? | `/api/generate/lyrics-pair` | Bearer? | Paired lyrics | `[LEAD]` raw scan/topic prose | not-in-code |
| POST? | `/api/generate/lyrics-pair/rate` | Bearer? | Rate lyrics pair | `[LEAD]` raw scan/topic prose | not-in-code |
| POST? | `/api/generate/concat/v2/` | Bearer? | Concatenate generated segments | `[LEAD]` raw scan; not exercised in reviewed capture | not-in-code |
| POST? | `/api/generate/merge/` | Bearer? | Merge generated segments | `[LEAD]` raw scan/topic prose | not-in-code |
| POST? | `/api/extend_audio` | Bearer? | Alternate extension path | `[LEAD]` old topic prose | not-in-code |
| POST? | `/api/upload-and-cover/` | Bearer? | Upload-and-cover path | `[LEAD]` old topic prose; multipart fields unproved | not-in-code |
| POST? | `/api/generate/upsample` | Bearer? | Audio upsampling | `[LEAD]` raw scan/topic prose | not-in-code |
| POST? | `/api/generate/get_recommend_styles` | Bearer? | Style recommendation surface | `[LEAD]` raw scan/topic prose | not-in-code |
| POST? | `/api/generate/matrix` | Bearer? | Generation matrix | `[LEAD]` raw scan | not-in-code |
| POST? | `/api/generate/sum/` | Bearer? | Generation sum/session surface | `[LEAD]` raw scan/loose normalized scan | not-in-code |
| GET? | `/api/gen/{id}` | Bearer? | Claimed generation-status poll | `[LEAD]`; captured generation flow used feed/get-songs instead | not-in-code |
| GET? | `/api/clip/{id}` | Bearer? | Claimed clip-status/detail poll | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/gen/{id}/convert_wav/` | Bearer? | Convert generation/clip to WAV | `[LEAD]` current endpoint map/topic prose | declared-unused |
| GET? | `/api/gen/{id}/wav_file/` | Bearer? | WAV artifact retrieval | `[LEAD]` current endpoint map | declared-unused |
| POST? | `/api/gen/bulk_increment_play_counts/v2` | Bearer? | Bulk play-count telemetry | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/gen/increment_action_counts/` | Bearer? | Bulk action-count telemetry | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/gen/prompt_image/` | Bearer? | Prompt-image job | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/gen/trash` | Bearer? | Trash generated items | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/gen/set_metadata/` | Bearer? | Set generation metadata | `[LEAD]` old inventory | not-in-code |
| GET? | `/api/prompts/` | Bearer? | Legacy prompt collection | `[LEAD]` old inventory/current endpoint map | declared-unused |
| POST? | `/api/prompts/delete/` | Bearer? | Delete prompt | `[LEAD]` raw scan | not-in-code |
| POST? | `/api/generate/stems` | Bearer? | Stem-generation surface | `[LEAD]` old upload topic prose | not-in-code |
| GET? | `/api/instruments` | Bearer? | Instrument selection surface | `[LEAD]` loose normalized scan | not-in-code |
| `?` | `/api/instrument/describe-doodle` | Bearer? | Instrument description surface | `[LEAD]` loose normalized scan | not-in-code |
| POST? | `/api/lyricists` | Bearer? | Lyricist creation surface | `[LEAD]`; only `GET /api/lyricists` is captured | not-in-code |

`/api/gen/{id}/convert_wav/` and `/api/gen/{id}/wav_file/` are the two rows where
the client holds constants that exist only because the endpoint mirror carried
them. They are `declared-unused` and must stay unused: WAV conversion is a
disabled surface, and the master records no capture for it.

## 4.5 Library, feed, search, and playlists — leads

Master section: 4.5. The captured feed, explore, playlist, and get-songs routes
stay in the master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET? | `/api/feed/` | Bearer? | Legacy feed | `[LEAD]` old library prose | not-in-code |
| GET? | `/api/feed/v2` | Bearer? | Legacy v2 feed | `[LEAD]` old library prose | not-in-code |
| GET? | `/api/feed/v3/offset` | Bearer? | Offset feed variant | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/unified/homepage/explore` | Bearer? | Explore homepage feed | `[LEAD]` old library prose | not-in-code |
| GET? | `/api/unified/homepage/explore/mobile` | Bearer? | Mobile explore feed | `[LEAD]` old library prose | not-in-code |
| GET? | `/api/unified/search/omnisearch` | Bearer? | Unified search | `[LEAD]` old library/loose scan | not-in-code |
| GET? | `/api/unified/search/suggest` | Bearer? | Unified search suggestions | `[LEAD]` loose normalized scan | not-in-code |
| GET? | `/api/unified/search/suggest/history` | Bearer? | Search-suggestion history | `[LEAD]` loose normalized scan | not-in-code |
| GET? | `/api/search/` | Bearer? | General search | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/search/history` | Bearer? | Search history | `[LEAD]` old library/loose scan | not-in-code |
| GET? | `/api/search/users` | Bearer? | User search | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/discover/shortcuts_songs` | Bearer? | Discover shortcut songs | `[LEAD]` old inventory | not-in-code |
| GET? | `/api/trending/metaplaylist/` | Bearer? | Trending metaplaylist | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/tags/recommend` | Bearer? | Recommended tags | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/recommend/hide-creator` | Bearer? | Hide creator from recommendations | `[LEAD]` old inventory/social prose | not-in-code |
| POST? | `/api/playlist/create/` | Bearer? | Create playlist | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/playlist/set_metadata` | Bearer? | Set playlist metadata | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/playlist/trash/` | Bearer? | Trash playlist | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/playlist/update_clips/` | Bearer? | Add/remove/reorder playlist clips | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/playlist/{playlist_id}/` | Bearer? | Playlist detail | `[LEAD]` old library/topic prose | not-in-code |
| GET? | `/api/playlist/{playlist_id}/tracks` | Bearer? | Playlist tracks | `[LEAD]` old library/topic prose | not-in-code |

The search rows are the worst offenders in this file: six of them claim a
server-side search surface that **no reviewed capture supports**. The master
records that `searchText` was never observed in any feed request, so a client
must filter locally. Do not treat these rows as a licence to add a remote search
field.

## 4.6 Clips, media, download, upload, and processing — leads

Master section: 4.6. Captured clip-relation, rights, video-status, and the
three-leg audio upload stay in the master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET? | `/api/clip/{clip_id}` | Bearer? | Clip detail | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/adjust-speed/` | Bearer? | Adjust playback speed | `[LEAD]` old inventory/topic prose; mutation-by-GET is not trusted | not-in-code |
| GET? | `/api/clips/aligned_clips` | Bearer? | Aligned clips | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/aligned_clip_siblings` | Bearer? | Aligned sibling clips | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/autoplay/` | Bearer? | Autoplay state/surface | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/delete/` | Bearer? | Delete surface | `[LEAD]` old inventory/topic prose; destructive and method-unproved | not-in-code |
| GET? | `/api/clips/direct_children` | Bearer? | Direct child clips | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/direct_children_by_user/` | Bearer? | User-scoped direct children | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/direct_children_count` | Bearer? | Direct-child count | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/displayable_remixes` | Bearer? | Displayable remixes | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/displayable_remixes_by_user/` | Bearer? | User-scoped displayable remixes | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/displayable_remixes_count` | Bearer? | Displayable-remix count | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/displayable_user_remixes_for_clip/` | Bearer? | Displayable user remixes for a clip | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/user_remixes_for_clip/` | Bearer? | User remixes for a clip | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clips/clip_roots` | Bearer? | Clip roots | `[LEAD]` loose normalized scan | not-in-code |
| POST? | `/api/clips/reverse-clip/` | Bearer? | Reverse clip | `[LEAD]` loose normalized scan | not-in-code |
| GET? | `/api/clip/{clip_id}/stems` | Bearer? | Stem metadata | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/clip/{clip_id}/stems/pages` | Bearer? | Paged stem data | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/edit/crop/{clip_id}/` | Bearer? | Crop clip | `[LEAD]` old library/topic prose | not-in-code |
| POST? | `/api/edit/stems/{clip_id}/` | Bearer? | Edit stems | `[LEAD]` old library/topic prose | not-in-code |
| GET? | `/api/billing/clips/{clip_id}/download/` | Bearer? | Billing-gated clip download | `[LEAD]` old inventory/topic prose; no download schema canonicalized | not-in-code |
| `?` | `/api/download/clips/zip/prepare` | Bearer? | Prepare clip ZIP download | `[VERIFY]` method/payload not captured | not-in-code |
| `?` | `/api/openai-speech/` | Bearer? | Claimed speech compatibility surface | `[VERIFY]` GET claim is scan-only; no method/schema contract | not-in-code |
| GET? | `/api/deepgram-token` | Bearer? | Claimed transcription token | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/uploads/audio/{id}/initialize-clip/` | Bearer? | Initialize clip from uploaded audio | `[LEAD]` old inventory/topic prose; not captured in 2026-09-24 export | not-in-code |
| GET? | `/api/uploads/audio/{id}/` | Bearer? | Audio upload/processing status | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/uploads/audio/{id}/convert_wav/` | Bearer? | Convert uploaded audio to WAV | `[LEAD]` old upload topic prose | not-in-code |
| POST? | `/api/uploads/image` | Bearer? | Image upload, no trailing slash | `[LEAD]` raw scan; multipart fields unproved | not-in-code |
| POST? | `/api/uploads/image/` | Bearer? | Image upload, trailing slash | `[LEAD]` raw scan; do not alias automatically | not-in-code |
| GET? | `/api/uploads/video` | Bearer? | Video upload info, no trailing slash | `[VERIFY]` spelling/method not captured | not-in-code |
| GET? | `/api/uploads/video/` | Bearer? | Video upload info, trailing slash | `[VERIFY]` spelling/method not captured | not-in-code |
| POST? | `/api/uploads/video/{upload_id}/upload-finish/` | Bearer? | Finalize video upload | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/video/hooks/create` | Bearer? | Create video hook | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/video/hooks/feed` | Bearer? | Video-hook feed | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/video/hooks/{hook_id}/flag` | Bearer? | Flag video hook | `[LEAD]` old inventory/topic prose; mutation-by-GET is not trusted | not-in-code |
| `?` | `/api/video/hooks/fetch_hook_lyrics` | Bearer? | Fetch hook lyrics | `[LEAD]` raw scan | not-in-code |
| `?` | `/api/video/hooks/suggested_clips` | Bearer? | Suggested video-hook clips | `[LEAD]` raw scan | not-in-code |
| `?` | `/api/video_gen/poll_batches` | Bearer? | Poll video batches | `[LEAD]` old inventory/loose scan | not-in-code |

The four `/api/uploads/...` spelling pairs are deliberately preserved as separate
rows with no aliasing. Trailing-slash differences and the
`/api/uploads/video` vs `/api/uploads/video/` split are recorded in the master's
conflict register (Appendix A); a capture must select one spelling before either
is used.

## 4.7 Persona, custom models, and voice — leads

Master section: 4.7. The captured pending-custom-model route stays in the master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| POST? | `/api/persona/create/` | Bearer?/entitlement? | Create persona | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/persona/get-personas/` | Bearer? | User personas | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/persona/get-followed-personas/` | Bearer? | Followed personas | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/persona/get-loved-personas/` | Bearer? | Loved personas | `[LEAD]` old inventory/topic prose | not-in-code |
| GET? | `/api/persona/get-persona-paginated/{id}/` | Bearer? | Paginated persona surface | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/custom-model/create/` | Bearer?/entitlement? | Create custom model | `[LEAD]` old inventory/topic prose; training schema unproved | not-in-code |
| POST? | `/api/custom-model/archive/` | Bearer?/entitlement? | Archive custom model | `[LEAD]` old inventory/topic prose | not-in-code |
| POST? | `/api/processed_clip/voice-vox-stem` | Bearer? | Voice/stem processing | `[LEAD]` old inventory/topic prose | not-in-code |

Every row in this table is gated on an **entitlement** that no capture
establishes. The master records that no plan matrix, verification requirement,
per-account model limit, or voice-clone availability statement is canonical
without a current capture. Capturing the route would not by itself prove an
account may use it.

## 4.8 Social, sharing, comments, notifications, and rights — leads

Master section: 4.8. Captured profile, notification, share, following-feed, and
notification-read routes stay in the master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| GET? | `/api/profiles/` | Bearer? | Profile listing/search | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/profiles/{handle}` | Bearer? | Profile by handle | `[LEAD]` old inventory/social prose | not-in-code |
| GET? | `/api/profiles/follow` | Bearer? | Follow action | `[LEAD]` old inventory/social prose; mutation-by-GET is not trusted | not-in-code |
| GET? | `/api/profiles/mutual-followers` | Bearer? | Mutual followers | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/comment/{comment_id}` | Bearer? | Comment detail | `[LEAD]` old inventory/social prose | not-in-code |
| POST? | `/api/comment/block-user` | Bearer? | Block user | `[LEAD]` old inventory/social prose | not-in-code |
| POST? | `/api/comment/unblock-user` | Bearer? | Unblock user | `[LEAD]` old inventory/social prose | not-in-code |
| POST? | `/api/comment/{comment_id}/reaction` | Bearer? | React to comment | `[LEAD]` old inventory/social prose | not-in-code |
| POST? | `/api/comment/{comment_id}/replies` | Bearer? | Reply to comment | `[LEAD]` old inventory/social prose | not-in-code |
| POST? | `/api/comment/{comment_id}/report` | Bearer? | Report comment | `[LEAD]` old inventory/social prose | not-in-code |
| GET? | `/api/share/attribute/` | Bearer? | Share attribution | `[LEAD]` old inventory/social prose | not-in-code |
| GET? | `/api/share/event` | Bearer? | Share event | `[LEAD]` old inventory/social prose; mutation-by-GET is not trusted | not-in-code |
| GET? | `/api/share/link` | Bearer? | Share-link surface | `[LEAD]` old inventory/social prose | not-in-code |
| `GET?` or `POST?` | `/api/song_copy/send-song` | Bearer? | Send/copy song action | `[VERIFY]` direct method conflict | not-in-code |
| GET? | `/api/invite/` | Bearer? | Invitation surface | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/survey/survey-responses` | Bearer? | Survey response | `[LEAD]` raw/loose scan | not-in-code |

Three separate rows here are **mutations described with a `GET?` method**
(`/api/profiles/follow`, `/api/comment/{id}/reaction` aside,
`/api/share/event`, `/api/clips/*` in 4.6). The method is a transcription
artifact, not an invitation. Mutation-by-GET is not trusted; a capture must show
the real method before any of these is called.

## 4.9 Feature gates, telemetry, app chrome, and experimental — leads

Master section: 4.9. The captured Statsig, Labs config, modals, CMS nudge,
realtime, challenge, contest, playbar, and personalization routes stay in the
master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| `?` | `/api/labs/marketplace` | Bearer? | Historical Marketplace probe path; availability/method unproved | `[VERIFY]`; do not confuse with `/labs/marketplace` page or Marketplace artifacts | not-in-code |
| GET? | `/api/ably/simple-mode-token` | Bearer? | Realtime simple-mode token | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/cms` | Bearer? | CMS content surface | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/cms/paywall/plan-options` | Bearer? | Paywall plan options | `[LEAD]` loose scan after `/marketplace/` normalization | not-in-code |
| POST? | `/api/moderation/ack-copyright-warning` | Bearer? | Acknowledge copyright warning | `[LEAD]` old inventory/raw scan | not-in-code |
| GET? | `/api/preferences/clip-review/pending` | Bearer? | Clip-review preference state | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/preferences/clip-review/submit` | Bearer? | Submit clip review | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/preferences/clip-review/opt-out` | Bearer? | Opt out of clip review | `[LEAD]` old inventory/raw scan | not-in-code |
| POST? | `/api/labs/verse/messages/` | Bearer? | Labs Verse messages | `[LEAD]` raw scan | not-in-code |

`/api/cms/paywall/plan-options` exists only because a loose scan produced
`/marketplace/api/cms/paywall/plan-options` and the frontend prefix was stripped
back to `/api/...`. That normalization is documented in the master; it is a
reconstruction, not an observation.

### 4.9.1 Client feature-flag names — localStorage keys, not routes

**These are not routes and must never be called, parsed, or sent.** They are
client-side flag and cache key names observed in scans. Evidence: **scan output
only. Zero capture evidence, zero server contract, zero authorization meaning.**

| Flag/cache name family | Observed names |
|---|---|
| Orpheus flags | `orpheus_is_enabled`, `orpheus_is_auto_mode`, `orpheus_is_canvas_enabled`, `orpheus_default_to_chat`, `orpheus_mobile_web_enabled`, `orpheus_group` |
| Marketplace flags | `marketplace_enabled`, `marketplace_access`, `labs_marketplace` |
| Cover/labs wildcards | `gen-video-covers`, `labs_*` |
| Credit/UI banners | `hide-credits-enabled`, `hide-credits-for-subscribers-enabled`, `out-of-credits-banner*`, `free_*`, `can_buy_credit_top_up` |
| Cache bypasses | `bypass_hook_feed_caches`, `bypass_unified_feed_caches` |
| Statsig cache | `statsig.cached.evaluations.*` |

The `*` suffixes are wildcard prefixes preserved from the scan, not glob
patterns to expand. They are listed to be ignored, not resolved.

**Forcing a client-side flag is not proof that the backend has enabled a route,
plan, model, or entitlement.** A cached Statsig evaluation is a stale local
cache of a server decision; it is not the decision. No client may read these keys
as permission, and none may be used to justify calling a `[LEAD]` route.

### 4.9.2 B-Side and Labs page-route leads — frontend pages, not API paths

> **Do not infer an underlying `/api/...` path from a page route.** A page route
> is a browser navigation target. It says nothing about which API the page calls,
> whether the API is `/api/...` at all, or whether either is authorized. A
> `/b-side/...` path and a `/api/b-side/...` path are unrelated strings that
> happen to share a name. Do not construct one from the other.

These are frontend page routes found in scans, not proven API contracts. All are
`[LEAD]`; method is normally browser navigation (`GET`) and access may require a
web session, entitlement, or internal role.

| Family | Paths preserved from the corpus |
|---|---|
| B-Side index/exploration | `/b-side`, `/b-side/explore`, `/b-side/nux`, `/b-side/labs-control`, `/b-side/personalization` |
| Account/moderation/admin | `/b-side/account-moderation`, `/b-side/contests`, `/b-side/impersonate`, `/b-side/song-moderation`, `/b-side/trending-moderation`, `/b-side/user-activity` |
| Audio/DSP/evaluation | `/b-side/audible-magic`, `/b-side/cover-art-eval`, `/b-side/describe-clip`, `/b-side/dsp-diag`, `/b-side/dsp-engine-talk`, `/b-side/lyrics-eval`, `/b-side/lyrics-eval-reports/{slug*}`, `/b-side/lyrics-eval/{slug*}`, `/b-side/stem-extract-test` |
| Creation/lyrics | `/b-side/hook-song-gen`, `/b-side/hooks-explorer/{slug*}`, `/b-side/lyrics-gen`, `/b-side/lyrics-viewer`, `/b-side/simple-remix/{slug*}` |
| Recommendations/search | `/b-side/because-you-like`, `/b-side/isthisus`, `/b-side/search-lens/*`, `/b-side/music-soulmate`, `/b-side/music-soulmate-talk`, `/b-side/user-mix`, `/b-side/user-similarity` |
| Search-lens subroutes | `/b-side/search-lens/crate`, `/b-side/search-lens/crate/browse`, `/b-side/search-lens/crate/listening-room`, `/b-side/search-lens/crate/listening-room/mock-needle`, `/b-side/search-lens/crate/similar`, `/b-side/search-lens/crate/workbench`, `/b-side/search-lens/playground` |
| Projects/Studio | `/b-side/agentic-transcript`, `/b-side/agentic-transcript/{clipId}`, `/b-side/playlist-copier`, `/b-side/project-state-tour`, `/b-side/studio-access` |
| Commerce/account experiments | `/b-side/billing/revcat`, `/b-side/vip` |
| Visual/video/voice | `/b-side/video-gen`, `/b-side/visual-art`, `/b-side/visual-art/{type}/{id}`, `/b-side/voice-verification` |
| Misc experiments | `/b-side/api-explorer`, `/b-side/banner`, `/b-side/bucket-viewer`, `/b-side/creators`, `/b-side/feature-flags`, `/b-side/hipster`, `/b-side/milo`, `/b-side/music-video`, `/b-side/onboarding-survey`, `/b-side/on-repeat`, `/b-side/orpheus`, `/b-side/reward-model`, `/b-side/sse-demo`, `/b-side/style-synth`, `/b-side/sunshine-list` |
| Labs pages | `/labs/canvas`, `/labs/divisi`, `/labs/genre-wheel`, `/labs/listen-and-rank`, `/labs/live-radio`, `/labs/marketplace`, `/labs/milo`, `/labs/pedalboard`, `/labs/splashpad`, `/labs/suno-jr`, `/labs/suno-jr/beats`, `/labs/suno-jr/divvy`, `/labs/suno-jr/lullaby`, `/labs/suno-jr/playlists`, `/labs/suno-jr/visuals`, `/labs/suno-mania`, `/labs/turntable`, `/labs/turntable/{roomId}`, `/labs/verse` |

`{slug*}` and `*` are preserved corpus wildcard shapes, not patterns to expand.
Several of these pages are internal tooling (`/b-side/api-explorer`,
`/b-side/feature-flags`, `/b-side/impersonate`, `/b-side/bucket-viewer`); their
presence in a scan is not authorization to browse them, and none is a contract.

## 3.4 Auth and OAuth route catalog — leads

Master section: 3.4. The captured Clerk and social rows stay in the master.

| Method | Path | Auth | Purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| `GET?` or `POST?` | `/v1/client/verify` | Clerk cookies? | Method and purpose conflict; do not use as generic session verification | `[VERIFY]` | not-in-code |
| GET? | `/v1/client/sync` | Clerk cookies? | Claimed client-state synchronization | `[LEAD]` old auth prose/scan | not-in-code |
| GET? | `/v1/event` | Clerk cookies? | Claimed client telemetry/event surface | `[LEAD]` old auth prose/scan | not-in-code |
| GET? | `/v1/logs` | Clerk cookies? | Claimed client log retrieval | `[LEAD]` old auth prose/scan | not-in-code |
| POST? | `/v1/tickets/accept` | Clerk cookies? | Claimed ticket/policy acceptance | `[LEAD]` old auth prose/scan | not-in-code |
| `GET?` or `POST?` | `/v1/verify` | Clerk cookies? | Method and semantics conflict; not observed | `[VERIFY]` | not-in-code |
| `GET?` or `POST?` with `?_method=PATCH` | `/v1/client` | Clerk cookies? | Claimed client mutation via method override; transport method not captured | `[VERIFY]` | not-in-code |
| GET? | `/sso-callback` | Web session | Frontend SSO completion target seen in reconstructed sign-in data | `[LEAD]` | not-in-code |
| GET? | `/oauth-redirect` | Web session | Generic OAuth redirect page | `[LEAD]` scan | not-in-code |
| GET? | `/oauth-redirect-custom` | Web session | Custom OAuth redirect page | `[LEAD]` scan | not-in-code |
| GET? | `/oauth-redirect-staff` | Web session/admin? | Staff OAuth redirect page | `[LEAD]` scan | not-in-code |
| GET? | `/oauth-redirect-v2` | Web session | OAuth redirect v2 page | `[LEAD]` scan | not-in-code |
| GET? | `/link-account` | Clerk session | Account-linking page | `[LEAD]` scan | not-in-code |
| GET? | `/auth/session-recovery` | Clerk context | Session-recovery page | `[LEAD]` scan | not-in-code |
| GET? | `/auth/birthday` | Clerk session | Birthday/age gate page | `[LEAD]` scan | not-in-code |
| GET? | `/auth/error` | Clerk context | Auth error page | `[LEAD]` scan | not-in-code |
| GET? | `/auth/verify` | Clerk context | Auth verification page | `[LEAD]` scan | not-in-code |

`/v1/client/verify` and `/v1/verify` are separate routes and separate conflicts;
both are in the master's conflict register (Appendix A). Do not merge them, and do
not use `/v1/client/verify` as a bearer-refresh or generic preflight.

The `?_method=PATCH` row is a trap worth naming: it reuses the *captured*
`/v1/client` path with an unobserved transport method. The fact that
`GET /v1/client` is `[T1]` says nothing about a PATCH-shaped mutation. Do not
synthesize that request.

The `/auth/*` and `/oauth-redirect*` rows are **web page routes**, not API
paths — same warning as section 4.9.2 applies.

## 4.10 Orpheus leads

No Orpheus request/response contract is canonical. Every row below is research
material only. The only cited sources are the old B-Side prose, the current
endpoint constants in `src/suno/SunoEndpoints.hpp`, and the old inventory — and
`src/suno/SunoEndpoints.hpp` is an **implementation mirror, which is not
evidence**. Neither is a capture.

| Method | Path on claimed Modal host | Auth | Claimed purpose | Evidence | Implemented |
|---|---|---|---|---|---|
| `?` | `/session-history` | Unconfirmed | Session history | `[LEAD]` old B-Side prose | not-in-code |
| `?` | `/v1/orchestrator/chat` | Unconfirmed | Orchestrator chat | `[LEAD]` current endpoint constants; no captured method/schema | declared-unused |
| `?` | `/v1/orchestrator/history` | Unconfirmed | Orchestrator history | `[LEAD]` current endpoint constants; no captured method/schema | declared-unused |
| POST? | `/api/v1/chat/completions` | Unconfirmed | Claimed OpenAI-compatible completion | `[VERIFY]` old inventory claim only | not-in-code |
| GET? | `/api/v1/models` | Unconfirmed | Claimed model listing | `[VERIFY]` old inventory claim only | not-in-code |

The two `declared-unused` rows are held as `ORCHESTRATOR_CHAT` and
`ORCHESTRATOR_HISTORY` in the endpoint mirror, joined to the claimed Modal base
rather than the Studio base. Holding a constant is not a contract: it exists so
the mirror matches the scan, and it must stay unreferenced. Orpheus is
disabled in the client regardless of any local UI flag.

### 4.10.1 Orpheus model-name disclaimer

The names `orpheus-0.1` through `orpheus-0.5`, "OpenAI-compatible," chat-driven
generation, auto mode, and canvas semantics are `[LEAD]` claims. They are not
a supported API, a verified model catalog, or a client contract.

This disclaimer is retained deliberately, because the model names are the most
persuasive and most misleading part of the Orpheus material. A version-numbered
model family reads like a server fact and is actually scan text. Do not surface
these names in a UI model list, do not send them in a generation request, and do
not treat their existence as evidence that the other four rows are real.

## Promotion criteria

A row in this file becomes eligible for the master only when **all** of the
following hold. A row that fails any of them stays here.

1. **A direct request/response item exists** and was reviewed by a human, not
   inferred from a bundle, a log filename, prose, or another row. A client-side
   constant, a decoded route string, a cached Statsig evaluation, or an
   `old inventory` citation does **not** satisfy this criterion, no matter how
   confident it reads.
2. **The exact method is captured** — no `GET?`/`POST?` reconstruction, and no
   `_method` override that was not itself observed on the wire. If sources still
   disagree, the row stays `[VERIFY]`.
3. **The exact host and path spelling are captured**, including trailing slash
   and query-key names. A spelling variant from a scan is not the captured
   spelling. If two spellings remain unreconciled, both stay separate rows.
4. **The auth requirement is captured**, not inferred: which of the bearer,
   `Device-Id`, `Browser-Token`, `Origin`/`Referer`, `User-Agent`, or Clerk
   cookies the route actually received. "Bearer is plausible" is not captured
   auth.
5. **The content type is captured** for any request with a body. JSON, form
   encoding, and multipart are not interchangeable and must not be assumed.
6. **A redacted request and response shape is recorded** — field names, types,
   and status, with values omitted. A method-only observation is not enough for
   a mutation; record what the response looked like too.
7. **Provenance is recorded**: capture timestamp, source filename, SHA-256, and
   a hash manifest entry in [`raw/README.md`](raw/README.md). The raw artifact
   itself stays outside the repository if it carries secrets.

Promotion then means: add the row to the master's relevant catalog table with its
`[T1]` label, cite the capture and its date, and delete the row here. The
`[T1]` label is assigned **per observed contract**, not per route: a capture of
one variant of a path does not promote its siblings, its other methods, or its
neighbouring paths in the same bundle.

Conversely, a capture can also **retire** a row. If a directly captured request
returns a 404 on a route listed here, that is evidence the row is dead, not
evidence that the spelling was wrong. Record the 404 in the master's error
section (section 6) and drop the row.

## Related live documents

- [`ENDPOINT-INVENTORY.md`](ENDPOINT-INVENTORY.md) — the sole API-spec master:
  evidence labels, precedence, hosts, request conventions, `[T1]` route catalog,
  capture-backed contracts, error/status limits, and runtime notes.
- [`OAUTH_REDIRECT_ANALYSIS.md`](OAUTH_REDIRECT_ANALYSIS.md) — the observed
  Google web request sequence, the Clerk cookie families relevant to it, and the
  binding native-callback gate. Consult it before anything in section 3.4.
- [`raw/README.md`](raw/README.md) — provenance, SHA-256 hashes, and handling
  rules for evidence. Never cite it as a contract.
