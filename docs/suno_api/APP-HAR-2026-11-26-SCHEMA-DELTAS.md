# App HAR 2026-11-26 — field-level deltas against this tree

Second pass over the iOS capture recorded by **T0473** (`ENDPOINT-INVENTORY.md` §4.13/§5.16).
That pass took the *routes*. This one takes the *fields*, because a route with a constant and a
parser that reads the wrong type is still a broken read.

Capture identity (unchanged): client `Suno/534`, `X-Suno-Client: iOS 1.98.0-534`, host
`studio-api-prod.suno.com` under `/api`, cookies `__client_uat` + `__session`, no `Browser-Token`,
no `Device-Id`. Two exports of one session were supplied — a DevTools-shaped HAR and a Proxyman
dump whose bodies are base64. Same bytes, one session; neither adds a route the other lacks.

## 1. Wired in this pass

| Wire fact | Was | Now |
| :--- | :--- | :--- |
| `metadata.duration` is a **number** (`14.318666666666667`) | Read through the string-only accessor ⇒ **always ""**. `ClipCard.formatDuration` rendered `0:00`; `lineage::parseDurationSeconds` returned 0, so every lineage pair scored duration 0 | `json::stringValue` ⇒ `"14.318…"`. Both consumers already accept bare numbers as seconds, so nothing downstream changed |
| `metadata.bpm` / `key` / `model_id` | string-only read, same trap if a body ever sends a number | same tolerant read |
| Sliders are **nested**: `metadata.control_sliders.{audio_weight, style_weight, weirdness_constraint, aug_creativity}` | Only the *flat* `metadata.style_weight` / `metadata.weirdness` spelling was read, so a v6 clip arrived with both at 0 | nested-first, flat fallback. Both spellings were captured, so both are read; `weirdness_constraint` is the nested name |
| `is_past_due`, `subscription_platform`, `subscription_anchor` on `/billing/info` | unread (not in any earlier body) | parsed into `SunoBillingInfo`; `is_past_due` exposed to QML as `SunoBridge.billingPastDue` — a past-due account still answers `is_active`, so `credits` alone cannot say "lapsed" |
| `download_credit_packs` / `visual_credit_packs` | unread | counted |

**`download_credit_packs` settles the download-product question.** The captured offers are
1 / 3 / 5 / 10 *downloads* at 2.99 / 8.95 / 14.95 / 29.90 USD — a separate meter from generation
`credits`, which is what `SunoBillingInfo` had been calling `[LEAD]`-grade. It is `[T1]` now.
Nothing consumes it: this client still fetches the progressive asset the account already streams
(MANGO_RIGHTS + MangoCrypto), so it never spends a download.

## 2. Recorded, deliberately not wired

| Observation | Why not |
| :--- | :--- |
| `/gen/{id}/aligned_lyrics/v2` → `aligned_words[{word, success, start_s, end_s, p_align}]` | `SunoLyrics` already reads `aligned_words` timing. `success` and `p_align` are per-word confidence; the karaoke path has no use for them and a low score is not "no lyric" |
| Request headers `session-id` (64-hex), `X-Suno-Client`, `X-Suno-Timezoome`, `X-Suno-Region`, `anonymous-id` | Per-install, server-issued values. The `/a/api` base answers 200 without them (T0336 route-probe), so synthesising one is the unverified-header mistake the project already reverted once |
| `POST /clerk/v1/client/sessions/{sid}/tokens?_is_native=true`, cookie-only, **no** `Authorization` | A third token variant; recorded in `SunoEndpoints.hpp`. `tokens`-first with `touch` fallback stays this client's order |
| `visual/assets/uploads` 201, body 4 131 bytes | Body not reproduced in the capture, so its key set is unknown. The follow-up `/{id}/complete` shape *is* recorded |
| `playlist/sync/v2` returns the token it was sent | Echo, not a demonstrated version. Nothing to store that the caller did not already hold |
| `/clips/clip_roots` → `{clips:[], clip_attribution_type:"original"}` | Wired by T0473 (`SunoClipDetailService`) |
| App Attest (`/device_attestation/nonce` + `Suno-AppAttest-*`) | Hardware-backed, per-install, bound to that app's bundle id. A desktop client cannot produce one. Permanent boundary, not missing work |

## 3. The `local` library schema handed over with this capture

A proposed desktop-library record (song_schema + a `local` block) came with the capture. It is a
*product* shape, not a wire shape, so it does not belong in `SunoEndpoints.hpp`. Mapped against
what exists:

| `local.*` | Status |
| :--- | :--- |
| `media_files.{m4a,mp3}` | Covered, differently: `SunoDownloader` writes one file per clip and `SunoDatabase` caches `media_urls[]`; there is no per-format table, and no `wav`/`flac`/`webm` local render |
| `download.{preferred_format,status,remote_urls}` | `preferred_format` is `SunoDownloader`'s format argument; `status`/`error` are the queue's, in memory only |
| `artwork_files.{cover,cover_large,custom}` | URLs are cached on the clip; no downloaded artwork file, no `custom` override |
| `file_hash`, `offline` | absent — no content hashing, no offline flag |
| `rating`, `notes`, `tags_local`, `folder`, `play_count_local`, `last_played_at` | absent — the library cache is a mirror of server state plus local play counts only |

Read that table as a work list, not a contract: none of the missing rows has a consumer in this
tree yet, and inventing columns before a surface asks for them is how a schema rots.

## 4. Reconcile against the route list

`check-endpoints.py` baseline is **111** dead constants as of T0473 and is unchanged here: this
pass adds **no** constant. Every route it touches already had one.
