#pragma once
// SunoEndpoints.hpp - Centralized Suno API endpoint map
// One source of truth for every URL we hit.
//
// Evidence sources (secrets redacted; paths are outside this repository):
// - 2026-08-25 Burp captures: ~/Documents/suno-burp-exports/auth.suno.com and
//   ~/Documents/suno-burp-exports/studio-api-prod.suno.com. Sanitized extracts
//   are indexed by ~/Documents/suno-media-station-glm5.2/docs/captures/raw/
//   burp-session-2026-08/README.md.
// - 2026-08-25 full Burp export: ~/Documents/suno-capture-burp.
// - 2026-09-23 browser HAR: ~/Documents/suno-master-utility-browser-extension-
//   kilo/scratchpad/suno.com.har.
// - 2026-09-22 sanitized OAuth recon: docs/suno_api/raw/
//   sanitized-recon-2026-09-22.json.
// - 2026-11-05/06 full-API browser HAR (signed-in session, 508 suno.com
//   requests): ~/Documents/suno-recon-har-recordings-full-api/
//   suno.com_Archive.har. This is the capture that turned the whole Studio
//   mutation set live and found the upload -> stem -> persona chain. Call it
//   "HAR-11" in comments below.
// - 2026-11-06 persona/voice HAR (signed-in, 96 MB): ~/Documents/
//   suno-master-utility-browser-extension/scratchpad/suno.com_me_persona.har.
//   Call it "persona HAR". It closes voice verification, the Listen-for-Credits
//   family, and the cover-art image id space.
// - 2026-11-06/07 live-browser walk of the /me surface (this file's new block at
//   the end, marked "2026-11-07 live UI capture"): library tabs, trash, playlist
//   trash/recover, persona edit, create-mode routes, upload chain, feature-flags
//   401. Call it "UI-07". Header names wholesale live probes ran through the
//   signed-in Chrome; request bodies are in the browser's HAR, not in-repo.
//
// Evidence tiers:
// - [T1] observed in one of the real captures listed above.
// - [LEAD] recon/research/constructed path only; not present in a real capture.
// Entries are grouped by capture family so provenance stays reviewable.
// Studio-api path constants are relative to API_BASE and MUST NOT start /api.

#include <QString>
#include <string_view>

namespace vc::suno {

/// Convert a std::string_view endpoint constant to QString without the
/// fromUtf8/data()/size() boilerplate repeated at every call site.
inline QString qstr(std::string_view sv) {
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

} // namespace vc::suno

// Top-level block: writing vc::suno::endpoints INSIDE namespace vc::suno
// would create vc::suno::vc::suno::endpoints and leave the outer namespace
// unclosed, poisoning every include that follows.
namespace vc::suno::endpoints {

// ── Base URLs ──────────────────────────────────────────────
// T1 bases: Burp 2026-08-25 / browser HAR 2026-09-23. MODAL_BASE is LEAD.
//
// T0336: `studio-api-prod` is a live alias, not a dead host -- route-probe with
// this app's own bearer gets byte-identical 200s from both bases for feed/v3 and
// unified/explore. The browser moved to `suno.com/a/api` on 2026-10-24 (HAR),
// and `unified/library` / `notification/v3` exist only there. This base follows
// the browser so a future removal of the alias is not a break:
constexpr std::string_view API_BASE = "https://suno.com/a/api"; // [T1]
constexpr std::string_view MODAL_BASE = "https://suno-ai--orpheus-prod-web.modal.run"; // [LEAD]
constexpr std::string_view CDN_BASE = "https://cdn1.suno.ai"; // [T1]
// HAR-11 [T1]: cdn2 answered 592 requests to cdn1's 65 -- artwork, images, and
// the m4a media bodies live on cdn2 now. Any new media URL should prefer the
// value the API returned and treat these bases as fallbacks only.
constexpr std::string_view CDN2_BASE = "https://cdn2.suno.ai"; // [T1]
constexpr std::string_view CDN_CLOUDFRONT_BASE = "https://d2lwuy8qc234o3.cloudfront.net"; // [T1]
constexpr std::string_view WEB_BASE = "https://suno.com"; // [T1]
// Static app assets (favicons, persona-shape.svg, sil-100.mp3). Cache only.
constexpr std::string_view STATIC_BASE = "https://cdn-o.suno.com"; // [T1]

// Clerk auth endpoints/versions live in suno/auth/ClerkAuthClient.hpp
// (AUTH_BASE, CLERK_API_VERSION, CLERK_JS_VERSION).

// ── Studio API: Generation ─────────────────────────────────
// T1: generation-v2-web.md + cowrite-lyrics.md (Burp 2026-08-25).
constexpr std::string_view GENERATE = "/generate/v2-web/"; // [T1] POST
constexpr std::string_view CAPTCHA_CHECK = "/c/check"; // [T1] POST
constexpr std::string_view COWRITE_LYRICS = "/generate/cowrite-lyrics/"; // [T1] POST
// Persona HAR (2026-11-06): returns `[]` for this account. Empty is a valid
// answer, not a failure.
constexpr std::string_view COWRITE_LYRICS_MODELS = "/generate/cowrite-lyrics/models/"; // [T1] GET

// ── Studio API: Generation follow-up (per-clip writes) ──────
// HAR-11 [T1]. Each of these was previously either absent or on the
// "deliberately ABSENT" list; every one is now a captured 200 with a body.
// Bodies are the site's own, so a caller can copy the shape:
//   listen_milestone  {"milestone":"5s"|"30s"|"60s"}   -- playback telemetry
//   increment_play    {"sample_factor":1}              -- play counter
//   update_reaction   {"reaction":"LIKE"|"DISLIKE"|null,"recommendation_metadata":{}}
//   set_metadata      {"title":…,"image_url":…,"is_audio_upload_tos_accepted":bool}
//   set_audio_desc    {"gemini_description_accepted":true,
//                      "audio_content_types":["Song Demo","Voice","Sample"]}
//
// `set_metadata` and `update_reaction_type` return the full clip/state, per
// ENDPOINT-INVENTORY §5.13. The telemetry pair is fire-and-forget: the site
// sends it, nothing in the response matters, and this client does not call it.
constexpr std::string_view GEN_LISTEN_MILESTONE = "/gen/{}/listen_milestone"; // [T1] POST
constexpr std::string_view GEN_INCREMENT_PLAY_COUNT = "/gen/{}/increment_play_count/v2"; // [T1] POST
constexpr std::string_view GEN_UPDATE_REACTION_TYPE = "/gen/{}/update_reaction_type/"; // [T1] POST
constexpr std::string_view GEN_SET_METADATA = "/gen/{}/set_metadata/"; // [T1] POST
constexpr std::string_view GEN_SET_AUDIO_DESCRIPTION = "/gen/{}/set_audio_description"; // [T1] POST

// ── Studio API: Lyrics / analysis ───────────────────────────
// T1: aligned-lyrics-v2.md (Burp 2026-08-25). WAV paths are LEAD.
constexpr std::string_view ALIGNED_LYRICS = "/gen/{}/aligned_lyrics/v2/"; // [T1] GET
constexpr std::string_view DOWNBEATS_STREAMING = "/gen/{}/downbeats_streaming/v2"; // [T1] POST
constexpr std::string_view WAVEFORM_AGGREGATES = "/gen/{}/waveform-aggregates"; // [T1] GET
constexpr std::string_view CONVERT_WAV = "/gen/{}/convert_wav/"; // [LEAD] POST
constexpr std::string_view WAV_FILE = "/gen/{}/wav_file/"; // [LEAD] GET

// ── Studio API: Lyrics projects / prompts ───────────────────
// T1: lyrics-projects-crud.md, styles-prompts-v2.md, cowrite-lyrics.md.
constexpr std::string_view LYRICS_PROJECTS = "/lyrics-projects"; // [T1] GET/POST
constexpr std::string_view LYRICS_PROJECTS_FLUSH = "/lyrics-projects/{}/flush"; // [T1] POST
// HAR-11 [T1]: DELETE answers 204 with an empty body, so success has no shape
// to parse. This is the only captured DELETE verb in the whole corpus.
constexpr std::string_view LYRICS_PROJECTS_BY_ID = "/lyrics-projects/{}"; // [T1] DELETE
constexpr std::string_view PROMPTS_V2 = "/prompts/v2"; // [T1] GET/POST
constexpr std::string_view PROMPTS_SUGGESTIONS = "/prompts/suggestions"; // [T1] GET
constexpr std::string_view PROMPTS_UPSAMPLE = "/prompts/upsample"; // [T1] POST
constexpr std::string_view LYRICISTS = "/lyricists"; // [T1] GET

// ── Studio API: Library / Feed ─────────────────────────────
// T1: feed-v3-library-listing.md (Burp 2026-08-25); full Burp export also
// captured POST /api/unified/homepage.
constexpr std::string_view LIBRARY_FEED = "/feed/v3"; // [T1] POST
// HAR-11 [T1] filter vocabulary (23 distinct filter keys seen across one
// session, all inside `filters`): `disliked`, `public`, `trashed`, `unlocked`,
// `upload`, `fullSong`, `persona` (object: {presence, personaId}),
// `user` ({presence, userId}), `workspace` ({presence, workspaceId}, where
// `"default"` is the fallback workspace), `stem`/`stemComplement`
// ({presence}), `fromStudioProject` ({presence}),
// `sort` ({sortBy ∈ upvote_count|created_at, sortDirection ∈ asc|desc}).
// Values are the STRINGS "True"/"False", not booleans. Paging is a cursor:
// send the last clip id as `cursor` with `limit` (20 observed).
constexpr std::string_view UNIFIED_FEED = "/unified/feed"; // [T1] POST
// Persona HAR (2026-11-06) [T1]: the library listing the /me page calls.
// Body `{"cursor":null,"customization":{"include_synthetic":true}}`; response is
// `{feeds:[{feed_id:"library_playlists", feed_title, feed_direct_link, items[]}]}`
// — nested feeds, not a flat clip list. `page_size` is ignored (T0336).
constexpr std::string_view UNIFIED_LIBRARY = "/unified/library"; // [T1] POST
constexpr std::string_view UNIFIED_HOMEPAGE = "/unified/homepage";
// Verified live 2026-10-24 as `/unified/explore`. An OPTIONS preflight for
// `/unified/homepage/explore` also appears in the 2026-10-14 auth-flow HAR, so
// the page may use the longer path; only a preflight was captured, so do not
// switch this constant without a request/response.
constexpr std::string_view UNIFIED_EXPLORE = "/unified/explore";
// Explore/search page routes, [T1] 2026-11-08 (ENDPOINT-INVENTORY §4.5).
// Capture records request/response field names and types only; no observed
// value for suggestion_size, content_size, or completion_method.
constexpr std::string_view SEARCH_SUGGEST = "/unified/search/suggest"; // [T1] POST
constexpr std::string_view SEARCH_OMNISEARCH = "/unified/search/omnisearch"; // [T1] POST
constexpr std::string_view SOCIAL_FOLLOWING_FEED = "/social/following-feed";
constexpr std::string_view NOTIFICATION_V2_READ = "/notification/v2/read";
constexpr std::string_view UPLOADS_AUDIO = "/uploads/audio/";
constexpr std::string_view AUDIO_UPLOAD_STORAGE_HOST = "suno-uploads.s3.amazonaws.com";
constexpr std::string_view UPLOADS_AUDIO_UPLOAD_FINISH = "/uploads/audio/{}/upload-finish/"; // [T1] POST
constexpr std::string_view SESSION = "/session/"; // [T1] GET
constexpr std::string_view SESSION_CATALOG = "/session/"; // [T1] alias for SESSION

// ── Studio API: Audio upload -> stem -> persona chain ──────
// HAR-11 [T1]. This is the chain behind "upload your own audio" for
// cover / mashup / inspiration, and it was not in any earlier capture:
//
//   1. POST /uploads/audio/                    {"extension":"wav",
//                                               "upload_type":"audio_recording"}
//      -> {id, url, fields, is_file_uploaded}  -- direct S3 POST target
//   2. POST <url> multipart, `fields` verbatim -> 204  (bytes land in S3)
//   3. POST /uploads/audio/{id}/initialize-clip/ {"user_reviewed_tags":true}
//      -> {clip_id, rights_clearance_available}
//   4. POST /uploads/audio/{id}/upload-finish/  {"upload_type":…,
//      "upload_filename":…, "agreed_to_vip_upload_terms":bool}
//   5. POST /clip/{clip_id}/vox-stem  {"vocal_start_s","vocal_end_s"}
//      -> {id, status, source_clip_id, vocal_start_s, vocal_end_s}
//   6. poll GET /processed_clip/{pack_id} -> the same keys plus
//      `vocal_audio_url` (the extracted vocal stem, also served from
//      suno-data-uploads.s3.amazonaws.com/studio/uploads/…)
//   7. POST /persona/create/ with root_clip_id + vox_audio_id + vocal range
//      -> persona, which later generation can condition on.
//
// `upload_type` values observed: "audio_recording" and "file_upload" (UI-07
// used the latter for a picked MP3). The `audio_content_types` vocabulary on
// set_audio_description names the rest: "Song Demo", "Voice", "Sample".
// `initialize-clip/` answers
// `{clip_id, rights_clearance_available}` and `false` is a real, observed
// answer (UI-07: sine probe) — callers must branch on it, not treat it as an
// error. `upload-finish/` answers an empty object `{}` on 200, so success has
// no shape to parse and must be inferred from the status alone.
constexpr std::string_view UPLOADS_AUDIO_INITIALIZE_CLIP = "/uploads/audio/{}/initialize-clip/"; // [T1] POST
constexpr std::string_view PROCESSED_UPLOAD_STORAGE_HOST = "suno-data-uploads.s3.amazonaws.com";
constexpr std::string_view CLIP_VOX_STEM = "/clip/{}/vox-stem"; // [T1] POST
constexpr std::string_view PROCESSED_CLIP = "/processed_clip/{}"; // [T1] GET, poll
constexpr std::string_view PROCESSED_CLIP_WAVEFORM_AGGREGATES = "/processed_clip/{}/waveform-aggregates"; // [T1] GET

// ── Studio API: Clip read ──────────────────────────────────
// HAR-11 [T1]: a single clip with no feed envelope. 40 top-level keys, the same
// clip schema the feed carries, minus paging. Use it for a share/deep-link
// resolve; the feed remains the paged path.
constexpr std::string_view CLIP_BY_ID = "/clip/{}"; // [T1] GET

// ── Studio API: Playlists ───────────────────────────────────
// T1: playlists.md (Burp 2026-08-25).
constexpr std::string_view PLAYLIST_ME = "/playlist/me"; // [T1] GET
// One playlist and its clips. Verified read, probe 2026-10-26:
//   GET https://suno.com/a/api/playlist/{id}/?page=1   200, playlist object at
//   top level, `playlist_clips[] = {clip, relative_index, created_at}`.
// Paging is a 1-based page number with NO `has_more`: a page past the end is a
// 200 carrying an empty `playlist_clips`, so "finished" is an empty array or
// `current_page * page_size >= num_total_results`. Both spellings matter here --
// the capture has a slash before `?page=`, and a probe is not a licence to tidy
// it away. Owner: SunoPlaylistService.
constexpr std::string_view PLAYLIST_BY_ID = "/playlist/{}"; // [T1] GET

// HAR-11 [T1] + §5.13: the playlist write set, now fully captured.
//   POST /playlist/create/        {name, description} -> full playlist object
//   POST /playlist/trash/         {"playlist_id":…} FLAT -> 204, empty body
//   POST /playlist/set_metadata/  {playlist_id, name, description}
//   PATCH /playlist/v2/{id}       {"metadata":{"name","is_public"},
//                                  "bio":{"description"}} -> 200 playlist
// The PATCH is the only captured PATCH verb and is the modern edit path;
// set_metadata is the older POST twin that returns a thinner object.
// Trash's flat body is load-bearing: the nested variant answers 422 (§5.13).
constexpr std::string_view PLAYLIST_CREATE = "/playlist/create/"; // [T1] POST
constexpr std::string_view PLAYLIST_TRASH = "/playlist/trash/"; // [T1] POST -> 204
constexpr std::string_view PLAYLIST_V2_BY_ID = "/playlist/v2/{}"; // [T1] PATCH
// Two trash routes exist and they are NOT the same contract. Use this one.
//   POST /playlist/v2/{id}/trash  {"undo":false}
//        -> 200 {success:true, updated_fields:["is_trashed"], errors:{}}
//   POST /playlist/trash/         {"playlist_id":…}  -> 204, empty body
// The v2 route reports per-field outcome and is the one the 2026-11-06 persona
// session used; the legacy route returns nothing, so a caller cannot tell a
// partial write from a success. `{"undo":true}` is the un-trash spelling and is
// UNTESTED — do not ship it without a capture.
constexpr std::string_view PLAYLIST_V2_TRASH = "/playlist/v2/{}/trash"; // [T1] POST

// ── Studio API: Library & playlist MUTATIONS ────────────────
// [T1] REQUEST contracts only, recovered 2026-09-30 by reading server
// validation errors during a write-route probe. Docs:
// docs/suno_api/ENDPOINT-INVENTORY.md §5.9 (master) and §1.5 (why the bodies
// must stay flat).
//
// READ THAT SECTION BEFORE USING ANY CONSTANT HERE. Two facts govern all of it:
//
//   1. NO SUCCESS RESPONSE BODY WAS EVER CAPTURED for any route below. Every
//      observed response is a *rejection*. A caller must therefore treat a 2xx
//      as "accepted, shape unverified" and must not assert any returned field.
//      The client-side owner is SunoLibraryMutations, which reads exactly one
//      key — a string `detail`, and only on a failure.
//
//   2. A required-field list is a MINIMUM, not a maximum. Unlisted optional
//      fields may exist. Do not read the absence of a key as "the server does
//      not accept it".
constexpr std::string_view GEN_SET_VISIBILITY = "/gen/{}/set_visibility/"; // [T1] POST
constexpr std::string_view GEN_UPDATE_FEEDBACK_STATE = "/gen/{}/update_feedback_state/"; // [T1] POST
constexpr std::string_view CLIP_TOGGLE_REMIXES = "/clips/{}/toggle_remixes/"; // [T1] POST
// Captured WITHOUT a trailing slash and reproduced exactly that way. A trailing
// slash is a different path, and no capture shows which spelling the router
// accepts, so "tidying" this one would be an unevidenced guess. The two
// toggle routes above/below it are the only pair in the file where this differs,
// which is itself a hint that it was copied by hand rather than generated.
constexpr std::string_view CLIP_TOGGLE_SHOW_REMIXES = "/clips/{}/toggle_show_remixes"; // [T1] POST
constexpr std::string_view SHARE_LINK = "/share/link"; // [T1] POST
constexpr std::string_view PLAYLIST_SET_METADATA = "/playlist/set_metadata"; // [T1] POST
constexpr std::string_view PLAYLIST_UPDATE_CLIPS = "/playlist/update_clips/"; // [T1] POST
constexpr std::string_view PLAYLIST_V2_TRACKS_ADD = "/playlist/v2/{}/tracks/add"; // [T1] POST
constexpr std::string_view PLAYLIST_V2_TRACKS_REMOVE = "/playlist/v2/{}/tracks/remove"; // [T1] POST
constexpr std::string_view PLAYLIST_V2_TRACKS_REORDER =
        "/playlist/v2/{}/tracks/reorder-by-index"; // [T1] POST
constexpr std::string_view PLAYLIST_V2_COVER_IMAGE = "/playlist/v2/{}/cover-image"; // [T1] POST

// ── Deliberately ABSENT from the mutation set above ─────────
// Named so a future reader does not "complete" the list from a bundle string.
// Every one of these is the shape AGENTS.md §1 exists to refuse:
//
//   * LIKE / UNLIKE as a path.  There is still no `/toggle_like` in any
//     capture, and adding one would be constructing a path. HAR-11 [T1] found
//     the real mechanism instead: GEN_UPDATE_REACTION_TYPE above, with
//     `{"reaction":"LIKE"|"DISLIKE"|null}`. A like is a reaction write, not a
//     like verb. Use that, never an invented toggle.
//   * CLIP TRASH / RESTORE.  UI-07 (2026-11-06/07) captured it live:
//     `POST /a/api/gen/trash` `{"trash":false,"clip_ids":[…]}` ->
//     `{"ids":[…],"is_trashed":false}` (restore direction); `trash:true` is
//     the twin. GEN_TRASH above is the constant, and `ClipTrashRestore` in
//     SunoLibraryMutations is the verb. `POST /a/api/clips/delete/` was
//     exercised earlier and answered 404. `is_trashed` is also a response
//     field and a feed filter key.
//   * PLAYLIST REORDER-BY-ID (only `tracks/reorder-by-index` is captured),
//     `gen/{id}/set_visibility` variants beyond the captured `is_public`
//     boolean, and the custom-model TRAINING / VOICE-CLONE writes. HAR-11
//     captured persona creation from an uploaded vocal stem, but not training
//     a model, and not the image-id space `playlist/v2/{id}/cover-image`
//     needs.
//
// CLEARED from this list on 2026-11-06 by HAR-11: playlist create, playlist
// trash, playlist edit (`PATCH /playlist/v2/{id}`), `gen/{id}/set_metadata/`,
// `gen/{id}/update_reaction_type/`, and persona creation. They are constants
// above, each with the request body the site itself sent.
//
// If a capture lands for one of the rest, add the constant HERE with its
// evidence label — never in the calling file — and add the verb to
// SunoLibraryMutations with the same request/response discipline the existing
// eleven carry.

// ── Studio API: Billing ────────────────────────────────────
// T1: billing-suite.md (Burp 2026-08-25).
constexpr std::string_view BILLING_INFO = "/billing/info/"; // [T1] GET
constexpr std::string_view BILLING_ELIGIBLE_DISCOUNTS = "/billing/eligible-discounts"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLANS = "/billing/usage-plans"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLAN_COMPARISON = "/billing/usage-plan-web-table-comparison/"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLAN_FAQ = "/billing/usage-plan-faq/"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLAN_DESCRIPTIONS = "/billing/usage-plan-descriptions/"; // [T1] GET
constexpr std::string_view BILLING_AUTO_RELOAD_NUDGE_CHECK = "/billing/auto-reload/nudge-check"; // [T1] POST
constexpr std::string_view BILLING_CONVERSION_TRACKING = "/billing/conversion-tracking"; // [T1] GET

// ── Studio API: User / Profile / Account ────────────────────
// T1: session-user-config.md, playlists.md (Burp 2026-08-25).
constexpr std::string_view USER_CONFIG = "/user/user_config/"; // [T1] POST
constexpr std::string_view USER_TOS_ACCEPTANCE = "/user/tos_acceptance"; // [T1] GET
constexpr std::string_view USER_GET_SESSION_ID = "/user/get_user_session_id/"; // [T1] GET
constexpr std::string_view USER_METADATA = "/user/metadata"; // [T1] GET
constexpr std::string_view PROFILES_INFO = "/profiles/{}/info"; // [T1] GET
constexpr std::string_view PROFILES_PINNED_CLIPS = "/profiles/pinned-clips"; // [T1] GET
constexpr std::string_view ONBOARDING_CURRENT = "/onboarding/current"; // [T1] GET

// ── Studio API: Projects ───────────────────────────────────
// T1: feed-v3-library-listing.md + browser HAR 2026-09-23.
constexpr std::string_view PROJECT_DEFAULT = "/project/default"; // [T1] GET
constexpr std::string_view PROJECT_ME = "/project/me"; // [T1] GET
constexpr std::string_view PROJECT_BY_ID = "/project/{}"; // [T1] GET
constexpr std::string_view PROJECT_PINNED_CLIPS = "/project/default/pinned-clips"; // [T1] GET
// HAR-11 [T1]: the workspace is a project, and it has its own pin read and
// clip membership write. `default` is only one project id.
constexpr std::string_view PROJECT_PINNED_CLIPS_BY_ID = "/project/{}/pinned-clips"; // [T1] GET
// {"update_type":"add","metadata":{"clip_ids":[…]}} -> 204, empty body. The
// body nests under `metadata` unlike the flat playlist writes, and `remove` is
// the untested twin of `add`.
constexpr std::string_view PROJECT_CLIPS = "/project/{}/clips"; // [T1] POST -> 204

// ── Studio API: Clips / relations ───────────────────────────
// T1: clips-relations.md + feed-v3-library-listing.md.
constexpr std::string_view CLIP_ATTRIBUTION = "/clips/{}/attribution"; // [T1] GET
constexpr std::string_view CLIP_PARENT = "/clips/parent"; // [T1] GET
constexpr std::string_view CLIP_REMIXES = "/clips/remixes"; // [T1] GET
constexpr std::string_view CLIP_REMIXES_COUNT = "/clips/remixes/count"; // [T1] GET
constexpr std::string_view CLIP_GET_SIMILAR = "/clips/get_similar/"; // [T1] GET
constexpr std::string_view CLIP_GET_SONGS_BY_IDS = "/clips/get_songs_by_ids"; // [T1] GET
constexpr std::string_view GEN_COMMENTS = "/gen/{}/comments"; // [T1] GET

// ── Studio API: Video generation ───────────────────────────
// T1: video-render-status.md (Burp 2026-08-25). The `video_gen` image/video
// family below is HAR-11 [T1]: Suno's cover-art generator, which is the likely
// source of a generated playlist/album image.
constexpr std::string_view VIDEO_GENERATE_STATUS = "/video/generate/{}/status/"; // [T1] GET
constexpr std::string_view VIDEO_GEN_PENDING_BATCHES = "/video_gen/pending_batches"; // [T1] POST
constexpr std::string_view VIDEO_GEN_MODEL_CONFIGS = "/video_gen/model-configs"; // [T1] GET
constexpr std::string_view VIDEO_GEN_COST_IMAGE = "/video_gen/cost/image"; // [T1] POST
constexpr std::string_view VIDEO_GEN_HISTORY = "/video_gen/history"; // [T1] POST
constexpr std::string_view VIDEO_GEN_IMAGE_GENERATE = "/video_gen/image/generate"; // [T1] POST
constexpr std::string_view VIDEO_GEN_POLL_BATCHES = "/video_gen/poll_batches"; // [T1] POST
// Persona HAR (2026-11-06) [T1]: 204 with an empty body. One body shape covers
// every entity type, so unused fields are sent as null:
//   {"entity_type":"video","entity_id":…,"action":"seen","batch_id":null,
//    "clip_id":null,"watch_ms":null,"surface":null,"cover_art_session_id":null}
// `action` values observed: "seen". `watch_ms` and `cover_art_session_id` are
// the watch/cover-art telemetry slots — untested values, present because the
// site always sends the full object.
constexpr std::string_view VIDEO_GEN_ACTION = "/video_gen/action"; // [T1] POST -> 204
// The single-image cover-art generator (distinct from image/generate's batches).
// Body `{prompt}` -> `{image_url, image_s3_id, upload_id}`. `image_s3_id` is the
// image id that `playlist/v2/{id}/cover-image` expects — this route is where that
// id space comes from.
constexpr std::string_view GEN_PROMPT_IMAGE = "/gen/prompt_image/"; // [T1] POST

// ── Studio API: Persona ────────────────────────────────────
// HAR-11 [T1]. This family was [LEAD] (T0349) until the capture: the reads and
// the create are now observed 200s. Reads are idempotent, so they are safe to
// wire first; `create/` spends credits and needs the failure discipline.
//   create body: {root_clip_id, name, description, is_public, persona_type,
//                 vox_audio_id, vocal_start_s, vocal_end_s, user_input_styles,
//                 clips:[…]}  -- every field the site sends, some optional.
// `vox_audio_id` is a processED clip id (the vocal stem), not a gen id.
constexpr std::string_view PERSONA_GET = "/persona/get-persona/{}/"; // [T1] GET
constexpr std::string_view PERSONA_LIST = "/persona/get-personas/"; // [T1] GET
constexpr std::string_view PERSONA_CREATE = "/persona/create/"; // [T1] POST

// ── Studio API: Voice verification (persona voice chain) ───
// Persona HAR (2026-11-06) [T1]. Completes the write half T0349 called
// capture-gated. The chain verifies that the singer of a "verification"
// recording is the same person as the voice being enrolled:
//
//   1. GET  /voice-verification/phrase/
//        -> {phrase_id, phrase_text}
//           ("Singing voices drift softly through a peaceful midnight chorus")
//   2. Upload the verification take, then
//      POST /processed_clip/voice-vox-stem
//        {"upload_id":…, "recording_type":"verification"}
//        -> {id, status:"submitted", voice_recording_id, …}
//      or, for a timed slice of an upload:
//        {"upload_id":…, "vocal_start_s":0, "vocal_end_s":39.48}
//        -> the same shape, no `recording_type`
//   3. POST /voice-verification/
//        {"voice_recording_id":…, "verification_recording_id":…, "phrase_id":…}
//   4. GET  /voice-verification/{id}  -> poll: status "approved" (also the only
//      observed terminal value), plus `verification_prompt_text`.
//
// Two stem extractors exist and are NOT interchangeable:
//   * /processed_clip/voice-vox-stem takes an UPLOAD id (this chain).
//   * /clip/{id}/vox-stem takes a GEN clip id (the persona-from-existing-song path).
// A client must pick by what it holds; passing the wrong id kind is a 404/422,
// not a fallback.
constexpr std::string_view PROCESSED_CLIP_VOICE_VOX_STEM = "/processed_clip/voice-vox-stem"; // [T1] POST
constexpr std::string_view VOICE_VERIFICATION_PHRASE = "/voice-verification/phrase/"; // [T1] GET
constexpr std::string_view VOICE_VERIFICATION_CREATE = "/voice-verification/"; // [T1] POST
constexpr std::string_view VOICE_VERIFICATION_BY_ID = "/voice-verification/{}"; // [T1] GET, poll

// ── Studio API: Rights ─────────────────────────────────────
// T1: clips-relations.md (Burp 2026-08-25); response shape reproduced and the
// route exercised live 2026-10-22 (ENDPOINT-INVENTORY §5.11-5.12). This is the
// PLAYBACK licence: the site's own service worker calls it on every clip start,
// one request per second at most, and caches the key for an hour. It is not the
// metered download product (`/api/download/clip/`), which this client never
// calls. Wired at SunoClient::requestContentKey.
constexpr std::string_view MANGO_RIGHTS = "/mango/rights"; // [T1] POST

// ── Deliberately NOT wired: Suno's download product ────────
// `GET /api/download/clip/{clip_id}?format=mp3` answers with a `status` and,
// when ready, a `download_url`; the site's Download button and its "download
// credits" / "top up" upsell are built on it. It is the route that is METERED.
// This client deliberately does not call it: it fetches the same progressive
// asset the account already streams and decrypts it locally (MANGO_RIGHTS +
// MangoCrypto), which is the path the site's own player uses. Do not add these
// constants to make the download surface look more official -- it would spend
// the very allowance the streaming path exists to avoid.

// ── Studio API: Notifications ───────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25); v3 from the 2026-10-24 browser HAR.
// The browser lists on v3 and still counts/mutates on v2: one session made 25
// `/a/api/notification/v3?include_hooks=true` calls alongside 25 v2 badge-count
// calls. There is no v3 badge route (T0335).
constexpr std::string_view NOTIFICATION_V3 = "/notification/v3"; // [T1] GET
constexpr std::string_view NOTIFICATION_V2 = "/notification/v2"; // [T1] GET
constexpr std::string_view NOTIFICATION_BADGE_COUNT = "/notification/v2/badge-count"; // [T1] GET

// ── Studio API: Custom Models ──────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view CUSTOM_MODEL_PENDING = "/custom-model/pending/"; // [T1] GET
// HAR-11 [T1]: the model catalogue, and the only place the server states what a
// model can DO. Response = {generation_config, items};
// `generation_config` = {major_version, capabilities, allowed_condition_combinations,
// max_lengths}. Captured `capabilities` (18) include `generate`, `extend`,
// `upload_extend`, `cover`, `cover_extend`, `artist_cover`, `artist_extend`,
// `infill`, `cover_infill`, `playlist_condition`, `underpainting`,
// `overpainting`, `vox`, `vox_extend`. That is the authoritative answer to
// "which model can do a cover / mashup / upload-extend", and it names
// `allowed_condition_combinations`, which is the real constraint on chaining
// those modes. Read this before hard-coding a mode list anywhere.
constexpr std::string_view CUSTOM_MODEL_SHARED = "/custom-model/shared"; // [T1] GET

// ── Studio API: Personalization ─────────────────────────────
// T1: session-user-config.md (Burp 2026-08-25).
constexpr std::string_view PERSONALIZATION_MEMORY = "/personalization/memory"; // [T1] GET
constexpr std::string_view PERSONALIZATION_SETTINGS = "/personalization/settings"; // [T1] GET

// ── Studio API: Prompts (legacy) ───────────────────────────
// Recon-only; retained for source compatibility.
constexpr std::string_view PROMPTS = "/prompts/"; // [LEAD]

// ── Studio API: Contests ───────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view CONTESTS = "/contests/"; // [T1] GET

// ── Studio API: Music Player ────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view MUSIC_PLAYER_PLAYBAR_STATE = "/music_player/playbar_state"; // [T1] GET/POST

// ── Studio API: Modals ─────────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view MODALS = "/modals"; // [T1] GET

// ── Studio API: Statsig / Orpheus ──────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view STATSIG_EXPERIMENT = "/statsig/experiment/"; // [T1] POST

// ── Studio API: CMS Nudges ─────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view CMS_NUDGES_PUBLISH = "/cms/nudges/publish-nudge"; // [T1] GET
constexpr std::string_view CMS_NUDGES_SHARE = "/cms/nudges/share-nudge"; // [T1] GET
// HAR-11 [T1]: CMS-driven cover-art prompt suggestions. Read-only and free;
// the natural prompt source for a generated playlist/album image.
// Response = {items:[{slug, label, prompt}]}, where label/prompt are objects.
constexpr std::string_view CMS_COVER_ART_SUGGESTIONS = "/cms/cover-art-suggestions"; // [T1] GET

// ── Studio API: Sharing ────────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view SHARE_STATS = "/share/stats"; // [T1] GET

// ── Realtime (Ably SSE) ────────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view REALTIME_DISCOVER = "/realtime/discover"; // [T1] GET
// HAR-11 [T1]: the browser READS /realtime/discover and then POSTs here for the
// Ably token. Request is a capability list, one per subscribed channel:
//   {"capabilities":[{"channel_pattern":"user:{user_id}","subscribe":true}]}
// Response is the Ably token request. Without this the SSE stream has no
// credential, so discover alone is not enough to open one.
constexpr std::string_view REALTIME_TOKEN = "/realtime/token"; // [T1] POST

// ── Studio API: Challenges / rewards ───────────────────────
// HAR-11 [T1]: daily-challenge state, including the credit reward.
// Response keys: current_level, challenges, total_credits, earned_credits,
// user_created_at, all_bonus, has_next_refresh, challenge_mode, reward_method,
// challenge_set. Read-only.
constexpr std::string_view CHALLENGE_PROGRESS = "/challenge/progress"; // [T1] GET

// ── Studio API: Reporting / refunds ────────────────────────
// HAR-11 [T1]: the copy the refund dialog shows before a user reports a bad
// generation. Response = {variant, refund_amount}. Read-only; this client does
// not file reports.
constexpr std::string_view REPORT_V2_REFUND_PROMPT = "/report_v2/refund_prompt"; // [T1] GET

// ── Studio API: Listen for Credits (preferences) ───────────
// Persona HAR (2026-11-06) [T1]. A human-labelling program that PAYS credits:
// rate clips for prompt-following, get paid per accepted task. It is the only
// route family found so far that adds to the balance rather than spending it.
// `GET /labs/configs` lists it as the `listen-and-rank` lab.
//
//   summary   {tasks_completed, credits_processed, credits_waiting,
//              tasks_available, daily_submit_limit_reached, daily_submit_cap:200,
//              next_available_in_seconds, rater_profile_required, …}
//   tasks     {task_id, review_items:[{key, url, …}], project, project_category}
//   submit    {task_id, preference:{<item_key>:<item_key>}, feedback,
//              listen_time_seconds:{<key>:<seconds>}}  -> 403
//             `verification_required` until the rater profile + captcha are done
//   rewards   streak program: {current_streak_days, tasks_today,
//              min_tasks_per_streak_day:5, claimable[], next_reward{reward_key,
//              credits, interval_days, days_remaining}, …}
//   stats     {tasks_completed, credits_earned, listening_seconds, …}
//   profile   GET -> {} until filled; PUT with the creator profile -> {profile:{…}}
//   leaderboard {window, metric, window_start, entries:[{rank, handle, …}]}
//   release-buffer  POST, empty body -> 200
//
// `submit` is captcha-gated in practice (`verification_required`). A GUI that
// pretends to submit without the owner's browser handoff would earn nothing.
constexpr std::string_view PREFERENCES_SUMMARY = "/preferences/summary"; // [T1] GET
constexpr std::string_view PREFERENCES_TASKS = "/preferences/tasks"; // [T1] GET
constexpr std::string_view PREFERENCES_SUBMIT = "/preferences/submit"; // [T1] POST
constexpr std::string_view PREFERENCES_TASKS_RELEASE_BUFFER = "/preferences/tasks/release-buffer"; // [T1] POST
constexpr std::string_view PREFERENCES_REWARDS = "/preferences/rewards"; // [T1] GET
constexpr std::string_view PREFERENCES_STATS = "/preferences/stats"; // [T1] GET
constexpr std::string_view PREFERENCES_PROFILE = "/preferences/profile"; // [T1] GET/PUT
constexpr std::string_view PREFERENCES_LEADERBOARD = "/preferences/leaderboard"; // [T1] GET
// Post-task survey, one slug per flow. Observed: "listen_and_rank".
constexpr std::string_view SURVEY_QUESTIONS = "/survey/survey-questions/{}"; // [T1] GET

// ── Studio API: Labs, integrations, chat, visuals ──────────
// Persona HAR (2026-11-06) [T1] unless noted. Three of these are FEATURE-GATED
// for this account and answer 403 `feature_disabled`; record them so nobody
// reads a 403 as a broken client.
//
//   labs/configs  -> [{lab_id, name_override, description_override,
//                      cover_image_url, staff_only, enabled_ga, has_statsig_segment}]
//                    lab ids observed: live-radio, genre-wheel, listen-and-rank, milo
//   v2/integrations -> {connections:[]}   -- the in-app connector list; this is
//                    where an MCP connection would appear. Empty for this account.
//   suno_chat/rooms POST {title} -> 403 feature_disabled "Suno Chat isn't available"
//   visual/models -> 403 feature_disabled "Visuals aren't available right now."
constexpr std::string_view LABS_CONFIGS = "/labs/configs"; // [T1] GET
constexpr std::string_view INTEGRATIONS_V2 = "/v2/integrations"; // [T1] GET
constexpr std::string_view SUNO_CHAT_ROOMS = "/suno_chat/rooms"; // [T1] POST, 403 gated
constexpr std::string_view VISUAL_MODELS = "/visual/models"; // [T1] GET, 403 gated

// ── 2026-11-07 live UI capture [T1] ─────────────────────────
// Signed-in Chrome walk of the /me surface, library tabs, trash, playlists,
// persona page, create modes, upload chain; request/response bodies confirmed
// from the owner's 507 MB DevTools HAR (suno.com4.har). Bodies quoted below
// are the site's own.
//   GET  /a/api/video/hooks/user_hooks?start_index=0&page_size=10&user_handle=<h>
//   GET  /a/api/video/hooks/me/liked/v2?start_index=0&page_size=10&user_handle=
//   GET  /a/api/video/hooks/suggested_clips?page_size=30&start_index=0
//   GET  /a/api/profiles/listen-history?limit=20
//   GET  /a/api/clips/trashed_v2?limit=20
//   GET  /a/api/playlist/me?page=1&show_trashed={false|true}&show_sharelist=false
//   GET  /a/api/project/me?page=1&sort=max_created_at_last_updated_clip&show_trashed=false&exclude_shared=false
//   GET  /a/api/lyrics-projects?limit=100&sort=updated_at
//   POST /a/api/unified/feed  {"feed_id":"library_albums"|"library_playlists",
//        "cursor":null,"page_size":12,"request_metadata":{"term":"",
//        "library_scope":"all"}[,"customization":{"include_synthetic":true}]}
//        -> {feed:{feed_id,feed_title,feed_direct_link,items[],presentation,item_count}}
//   POST /a/api/unified/homepage/explore  {"cursor":"3"}   -- cursor-paged
//   POST /a/api/gen/trash  {"trash":false,"clip_ids":[…]} -> {"ids":[…],
//        "is_trashed":false}   -- restore-to-library; trash:true is the twin
//   POST /a/api/playlist/v2/{id}/trash  {"undo":false}|{"undo":true} ->
//        {"success":true,"updated_fields":["is_trashed"],"errors":{}}
//        Both directions sent live; the playlist round-tripped back to active.
//   PUT  /a/api/persona/edit-persona/{id}/  {persona_id,name,description,
//        is_public,persona_type:"vox",user_input_styles,vox_audio_id,
//        vocal_start_s,vocal_end_s} -> 200
//   GET  /a/api/persona/get-persona-paginated/{id}/?page=1
//   POST /a/api/persona/{id}/toggle_love/  (fires on page load with no gesture)
//   POST /a/api/c/check  {"ctype":"generation"} -> {required:false,captcha_version:2}
//   POST /a/api/generate/v2-web/  shares one envelope for every create mode:
//        speech  -> metadata.is_speech=true, backing_music, mv:"chirp-goose"
//        sounds  -> task:"sound", mv:"chirp-hawk", metadata.sound_configs.user_loop
//        simple  -> task:"agentic_thinking" + gpt_description_prompt
//   Studio (multitrack) surface:
//   GET  /a/api/studio/list-projects?limit=16&name=&archived=false&sort_order=desc
//   POST /a/api/studio/create-project?title=…&major_version=2   (query, no body)
//   GET  /a/api/studio/project/{id} -> {state:{tracks[],timing,…},latest_version_id,…}
//   GET  /a/api/agent/conversation/{project_id}?limit=100&include_internal=true
//   GET  /a/api/plugins/catalog?include_archived=true
//   GET  /a/api/plugins/{id}/presets
//   POST /a/api/prompts/suggestions/contextual  {signature:{has_audio_condition,
//        attached_song_count,source_genres[],image_count,has_video,has_lyrics},count}
//        -> {suggestions[],transform,is_genre_conditioned}
//   Image upload mirror of the audio chain:
//   POST /a/api/uploads/image/ {"extension":"png"} -> {id,url,fields,is_file_uploaded}
//   POST /a/api/uploads/image/{id}/upload-finish/ -> {upload_id,moderation_status}
//   GET  /a/api/statsig/experiment/forked-onboarding -> {onboarding_group,reason}
// Feature flags: the app never calls https://suno.com/api/feature-flags; browser
// answers 401 there (gates are statsig — see STATSIG_EXPERIMENT). Authed shape
// remains unseen.
// Environment tiers: the bundle picks Datadog env by which base is allowed —
// `studio-api-prod.suno.com` = prod, `studio-api-staging.suno.com` = staging;
// the browser surface has moved to suno.com/a/api.
constexpr std::string_view VIDEO_HOOKS_USER_HOOKS = "/video/hooks/user_hooks"; // [T1] GET
constexpr std::string_view VIDEO_HOOKS_ME_LIKED = "/video/hooks/me/liked/v2"; // [T1] GET
constexpr std::string_view VIDEO_HOOKS_SUGGESTED_CLIPS = "/video/hooks/suggested_clips"; // [T1] GET
constexpr std::string_view PROFILES_LISTEN_HISTORY = "/profiles/listen-history"; // [T1] GET
constexpr std::string_view CLIPS_TRASHED_V2 = "/clips/trashed_v2"; // [T1] GET
constexpr std::string_view UNIFIED_HOMEPAGE_EXPLORE = "/unified/homepage/explore"; // [T1] POST
constexpr std::string_view PERSONA_EDIT = "/persona/edit-persona/{}/"; // [T1] PUT
constexpr std::string_view PERSONA_GET_PAGINATED = "/persona/get-persona-paginated/{}/"; // [T1] GET
constexpr std::string_view PERSONA_TOGGLE_LOVE = "/persona/{}/toggle_love/"; // [T1] POST
constexpr std::string_view GEN_TRASH = "/gen/trash"; // [T1] POST
constexpr std::string_view STUDIO_LIST_PROJECTS = "/studio/list-projects"; // [T1] GET
constexpr std::string_view STUDIO_CREATE_PROJECT = "/studio/create-project"; // [T1] POST (query)
constexpr std::string_view STUDIO_PROJECT_BY_ID = "/studio/project/{}"; // [T1] GET
constexpr std::string_view AGENT_CONVERSATION = "/agent/conversation/{}"; // [T1] GET
constexpr std::string_view PLUGINS_CATALOG = "/plugins/catalog"; // [T1] GET
constexpr std::string_view PLUGINS_PRESETS = "/plugins/{}/presets"; // [T1] GET
constexpr std::string_view UPLOADS_IMAGE = "/uploads/image/"; // [T1] POST
constexpr std::string_view UPLOADS_IMAGE_UPLOAD_FINISH = "/uploads/image/{}/upload-finish/"; // [T1] POST
constexpr std::string_view PROMPTS_SUGGESTIONS_CONTEXTUAL = "/prompts/suggestions/contextual"; // [T1] POST
constexpr std::string_view STATSIG_FORKED_ONBOARDING = "/statsig/experiment/forked-onboarding"; // [T1] GET

// ── Legacy base (NOT under API_BASE) ───────────────────────
// `GET https://suno.com/api/feature-flags` answered 401 `{"error":"Unauthorized"}`
// anonymously in the 2026-10-24 capture. Absolute on purpose: the `/api` prefix
// is part of the captured path, and no `/a/api/feature-flags` request exists, so
// joining this to API_BASE would invent a route.
constexpr std::string_view FEATURE_FLAGS_LEGACY_URL = "https://suno.com/api/feature-flags"; // [T1] 401 anon

// ── B-Side / Orchestrator ──────────────────────────────────
// Recon/research only. These are joined to MODAL_BASE, not API_BASE.
constexpr std::string_view ORCHESTRATOR_CHAT = "/v1/orchestrator/chat"; // [LEAD]
constexpr std::string_view ORCHESTRATOR_HISTORY = "/v1/orchestrator/history"; // [LEAD]

// ── CDN ────────────────────────────────────────────────────
// HAR-11 [T1]: the m4a progressive body is served from
// CDN_CLOUDFRONT_BASE + this path -- four distinct clips answered 200 with it.
// The mp3 spelling below is still a constructed fallback.
constexpr std::string_view CDN_CLIP_MP3 = "/{}.mp3"; // [LEAD]
constexpr std::string_view CDN_CLIP_M4A = "/1/clip/{}.m4a"; // [T1]

} // namespace vc::suno::endpoints
