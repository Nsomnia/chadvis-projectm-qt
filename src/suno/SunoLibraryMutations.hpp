#pragma once
// SunoLibraryMutations.hpp — the ONE owner of every write this client makes
// against a user's own Suno library and playlists.
//
// ── Why this file exists ─────────────────────────────────────────────────────
// Verified 2026-10-05 by grep: `toggle_like`, `like_clip`, `trash_clip` and
// `share_clip` return **zero hits** in `src/`. The client could read a library
// and not change one — `SunoDatabase` persists `is_liked`/`is_trashed`/
// `is_public` (SunoDatabase.cpp:57-59) and the feed *request* already carries
// `liked`/`trashed` filters (master §5.2), so both halves of the vocabulary
// existed and only the verbs were missing. That is the largest product gap in
// the Suno half of the app, and this file is the request side of closing it.
//
// ── THE EVIDENCE SITUATION, and it governs every line below ─────────────────
// Docs: `docs/suno_api/ENDPOINT-INVENTORY.md` §5.9 (the request contracts) and
// §1.5 (why the bodies are flat and why `detail` is not RFC 9457).
//
// **Request side: `[T1]`.** The 2026-09-30 recon probed 64 write routes with an
// empty object and then with deliberately wrong-typed values, and let the
// server's own validator name each required field, its type, and — for enums —
// the complete member list. Nothing was created: every payload was rejected.
//
// **Response side: NOT captured, at all.** Every observed response for every
// route in this file is a *rejection*. There is not one success body in the
// corpus. The consequences are load-bearing and this file is written to them:
//
// **Partly superseded 2026-10-26, and nothing here changed.** §5.13 captured
// success bodies for the reaction and playlist verbs. A 2xx is still `Accepted`
// and still parses nothing, and §5.13 is why: `playlist/trash` succeeds with
// `204 No Content` and no body at all, and `update_reaction_type` OMITS
// `reaction_type` when the reaction is cleared — never `""`. A parser written
// against either fails on the *success* path.
//
//   * A 2xx is `MutationOutcome::Accepted` and NOTHING MORE. It asserts no
//     field, because no field has been seen. There is no parsed response type
//     in this header, no `parseMutationReply`, and no accessor that would let a
//     caller ask "what did the server say?" — because the honest answer is that
//     we do not know, and an accessor shaped like the question would invite
//     someone to answer it with a guess.
//   * Exactly ONE key is ever read, and only on a failure: a **string** `detail`.
//     §1.5: the observed rejections are `{"detail": "<python repr()>"}`, and the
//     `loc` chain beside it is a renamed projection, not a wire format. If
//     `detail` arrives as an array or an object that is a *different* shape from
//     anything captured, and it is ignored in favour of a status-only sentence.
//     No structured/RFC 9457 problem parser is written here on purpose.
//   * `is_public: true` means "we asked for public". It does not mean the clip
//     is now public. The two are separated all the way to the caller: the
//     result carries no new state, and the UI is expected to re-read the
//     library rather than trust a local write it did not observe.
//
// ── What is NOT here, and why ────────────────────────────────────────────────
// **No like/unlike as a path.** `is_liked` exists as a feed filter key and as
// a response ownership field. A like is a reaction write
// (`GEN_UPDATE_REACTION_TYPE`, HAR-11 [T1]), not a like verb — inventing
// `/clips/{}/toggle_like` and grading it [T1] because it sounds right is the
// exact failure AGENTS.md §1 names.
//
// Trash/restore is here now: UI-07 [T1] captured `POST /gen/trash`
// (`ClipTrashRestore`) and `POST /playlist/v2/{id}/trash` (`PlaylistTrashV2`)
// with success bodies (§4.12), so the two verbs below are the whole
// trash story. `POST /api/clips/delete/` was exercised and answered **404**;
// no `DELETE` was probed on any route, so a DELETE spelling is unproved.
//
// ── Fail-closed, in two independent layers ───────────────────────────────────
// A mutation against a user's own library is a *normal* mutation and gets no
// new `FeatureGate`; `Library`, `Playlists` and `Sharing` already cover it. But
// "normal" is not "on": this class refuses to send anything unless BOTH hold.
//
//   1. `isEnabled()`. A C++-only master switch, **false on construction**.
//      There is deliberately NO `Q_INVOKABLE` setter and no QML-reachable
//      switch. A previous round declined one for `GateResolver` for exactly
//      this reason: a runtime toggle re-introduces "one toggle from shipping an
//      unwired surface", and the switch here encodes an *evidence* fact — that a
//      human captured a success response — not a user preference.
//   2. The gate verdict for the verb's owning surface, via an injected
//      `GateResolver`. A null resolver fails closed, so constructing this class
//      before the account manager exists is safe rather than permissive.
//
// Both must permit. Either one refusing is a `MutationOutcome::Refused` naming
// which, emitted through the normal per-subject channel so a UI learns why
// instead of showing a dead button. `[[nodiscard]]` on `setEnabled` for the
// reason `GateResolver::setLocallyEnabled` carries one: a refused enable that
// nobody noticed is indistinguishable from an accepted one.
//
// ── Every request goes through SunoClient ────────────────────────────────────
// `SunoClient::enqueueAuthenticatedRequest` is the only place this tree issues
// an authenticated `/api/*` call. It owns the credential, the request epoch,
// the bounded body reader and the host allowlist, and it accepts **GET and POST
// only** — which happens to be the right shape here, because every captured
// verb is a POST and no DELETE was ever probed. There is no second HTTP path in
// this file and adding one would bypass all four of those protections at once.
//
// ── Local state is never written here ────────────────────────────────────────
// `SunoDatabase` exposes no write API for `is_liked`/`is_trashed`/`is_public` —
// its entire surface is `getClip` and `hasLyrics` — so there is nowhere to
// write even if it were honest to write. Writing the *requested* value locally
// would assert a state the server never confirmed. So this class is transport
// only: it reports what happened and lets the caller re-read the library.

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <string_view>

class QNetworkReply;

namespace vc::suno {

class GateResolver;
class SunoClient;

/// A verb whose request contract is `[T1]`. The enumerator set is closed and
/// `gateFor` maps each to its owning surface; a verb with no gate has no path
/// to `Available` and therefore can never be sent.
enum class MutationVerb {
    SetVisibility,           ///< POST /api/gen/{gen_id}/set_visibility/
    ToggleRemixPermission,   ///< POST /api/clips/{clip_id}/toggle_remixes/
    ToggleShowRemixes,       ///< POST /api/clips/{clip_id}/toggle_show_remixes
    UpdateFeedbackState,     ///< POST /api/gen/{gen_id}/update_feedback_state/
    ShareLink,               ///< POST /api/share/link
    PlaylistSetMetadata,     ///< POST /api/playlist/set_metadata
    PlaylistUpdateClips,     ///< POST /api/playlist/update_clips/
    PlaylistTracksAdd,       ///< POST /api/playlist/v2/{id}/tracks/add
    PlaylistTracksRemove,    ///< POST /api/playlist/v2/{id}/tracks/remove
    PlaylistTracksReorder,   ///< POST /api/playlist/v2/{id}/tracks/reorder-by-index
    PlaylistCoverImage,      ///< POST /api/playlist/v2/{id}/cover-image
    ClipTrashRestore,        ///< POST /api/gen/trash — UI-07 [T1] (see GEN_TRASH)
    PlaylistTrashV2,         ///< POST /api/playlist/v2/{id}/trash — UI-07 [T1]
    UpdateReactionType,      ///< POST /api/gen/{gen_id}/update_reaction_type/ — §5.13 [T1]
    PlaylistCreate,          ///< POST /api/playlist/create/ — §5.13 [T1]
    PlaylistTrash,           ///< POST /api/playlist/trash/ — §5.13 [T1], 204
};

/// `const char*` for the same reason as `hosts::toString`, `http::toString`
/// and `toString(FeatureGate)`: Qt's QCOMPARE finds the overload by ADL and
/// requires exactly this return type.
[[nodiscard]] constexpr const char* toString(const MutationVerb verb) noexcept
{
    switch (verb) {
        case MutationVerb::SetVisibility:
            return "set-visibility";
        case MutationVerb::ToggleRemixPermission:
            return "toggle-remix-permission";
        case MutationVerb::ToggleShowRemixes:
            return "toggle-show-remixes";
        case MutationVerb::UpdateFeedbackState:
            return "update-feedback-state";
        case MutationVerb::ShareLink:
            return "share-link";
        case MutationVerb::PlaylistSetMetadata:
            return "playlist-set-metadata";
        case MutationVerb::PlaylistUpdateClips:
            return "playlist-update-clips";
        case MutationVerb::PlaylistTracksAdd:
            return "playlist-tracks-add";
        case MutationVerb::PlaylistTracksRemove:
            return "playlist-tracks-remove";
        case MutationVerb::PlaylistTracksReorder:
            return "playlist-tracks-reorder";
        case MutationVerb::PlaylistCoverImage:
            return "playlist-cover-image";
        case MutationVerb::ClipTrashRestore:
            return "clip-trash-restore";
        case MutationVerb::PlaylistTrashV2:
            return "playlist-trash-v2";
        case MutationVerb::UpdateReactionType:
            return "update-reaction-type";
        case MutationVerb::PlaylistCreate:
            return "playlist-create";
        case MutationVerb::PlaylistTrash:
            return "playlist-trash";
    }
    return "unknown";
}

/// What happened. Four values because "it did not work" is not one thing, and a
/// caller acting on the wrong one either retries a permanent refusal or reports
/// a transport blip as a server decision.
enum class MutationOutcome {
    /// 2xx. The request was accepted. **The response body was not parsed and no
    /// field of it is asserted** — no success body has ever been captured.
    Accepted,
    /// Nothing was sent: the switch is off, the gate refused, or the arguments
    /// would produce a malformed request. `reason` is this client's own
    /// sentence and is always populated.
    Refused,
    /// The server answered non-2xx. `reason` is the server's own `detail`
    /// string when one was observed, otherwise a status-only sentence.
    Rejected,
    /// The request was sent but no verdict could be read: transport error, or a
    /// body past the response cap that `SunoClient` aborted. Distinct from
    /// `Rejected` because retrying may work.
    Failed,
};

[[nodiscard]] constexpr const char* toString(const MutationOutcome outcome) noexcept
{
    switch (outcome) {
        case MutationOutcome::Accepted:
            return "accepted";
        case MutationOutcome::Refused:
            return "refused";
        case MutationOutcome::Rejected:
            return "rejected";
        case MutationOutcome::Failed:
            return "failed";
    }
    return "unknown";
}

/// One settled mutation. `httpStatus` is 0 for `Refused` and `Failed`, and the
/// observed status otherwise.
///
/// There is deliberately no field carrying a new value for the subject — no
/// `isPublic`, no `clipIds`, no share URL. A share link in particular *sounds*
/// like something a client must read out of the response, and inventing one
/// would produce a link that does not work, presented as though it did.
struct MutationResult {
    MutationVerb verb{MutationVerb::SetVisibility};
    /// The clip id, gen id, or playlist id the mutation is about. Never empty
    /// for a `Rejected`/`Accepted`/`Failed`: the argument check that guarantees
    /// it runs before anything is sent.
    QString subjectId;
    MutationOutcome outcome{MutationOutcome::Refused};
    int httpStatus{0};
    /// Empty only for a clean `Accepted`, where there is nothing to explain.
    QString reason;
};

/// The one enum in §5.9 whose COMPLETE member list was captured, so it is the
/// one enum in this file that may be validated against. `cover_source.type` on
/// `playlist/v2/{id}/cover-image` is always sent as `generated` (the image-id
/// space this client can produce, §5.15) and is spelled inline in the body
/// builder rather than given an enum, because a one-value enum is more
/// ceremony than the fact deserves.
enum class PlaylistUpdateType {
    Add,
    Remove,
    RemoveById,
    Reorder,
};

[[nodiscard]] constexpr const char* toString(const PlaylistUpdateType type) noexcept
{
    switch (type) {
        case PlaylistUpdateType::Add:
            return "add";
        case PlaylistUpdateType::Remove:
            return "remove";
        case PlaylistUpdateType::RemoveById:
            return "remove_by_id";
        case PlaylistUpdateType::Reorder:
            return "reorder";
    }
    return "unknown";
}

/// `request.reaction` on `update_reaction_type` (§5.13). The captured vocabulary
/// is exactly two members plus **`null`**, and `null` is the clear rather than a
/// member — which is why the verb takes `std::optional<ReactionType>` and not an
/// enum with a `None` enumerator that would have to be translated on the way out.
enum class ReactionType {
    Like,    ///< `"LIKE"` -> reply carries `reaction_type:"L"`
    Dislike, ///< `"DISLIKE"` -> reply carries `reaction_type:"D"`
};

[[nodiscard]] constexpr const char* toString(const ReactionType type) noexcept
{
    switch (type) {
        case ReactionType::Like:
            return "LIKE";
        case ReactionType::Dislike:
            return "DISLIKE";
    }
    return "unknown";
}

/// The request record for `set_metadata` (§5.13, HAR-11):
/// `{title, image_url, is_audio_upload_tos_accepted}`.
///
/// The TOS flag is a typed `bool` field rather than a key in a loose
/// `QJsonObject` because uploaded audio is a different publication contract from
/// a generated clip — an upload published under a guessed consent state is the
/// failure this record exists to prevent. `false` is the honest default: it is
/// the state "the user has not accepted", never "we did not ask".
struct ClipMetadataUpdate {
    QString title;
    QString imageUrl;
    bool isAudioUploadTosAccepted{false};
};

/// The request record for `set_audio_description` (§5.14, HAR-11):
/// `{gemini_description_accepted, audio_content_types}`.
///
/// Both are first-class typed fields for the same reason as
/// `ClipMetadataUpdate`: this is a consent record, not a set of headers.
struct AudioDescriptionConsent {
    bool geminiDescriptionAccepted{false};
    /// `audio_content_types`. Captured values: `"Song Demo"`, `"Voice"`,
    /// `"Sample"`. That is NOT a complete member list — §5.14 records the rest
    /// of the vocabulary as uncaptured — so nothing validates against it here,
    /// and an unfamiliar value is passed through rather than refused.
    QStringList audioContentTypes;
};

/// The transport for every `[T1]` library/playlist mutation.
///
/// Single-threaded, like every other service in `src/suno/`: it is constructed
/// on the GUI thread and its replies arrive there. It holds a borrowed
/// `SunoClient*` (which must outlive it) and a borrowed `GateResolver*` (which
/// may be null, and failing that is the safe answer).
class SunoLibraryMutations final : public QObject {
    Q_OBJECT

public:
    /// `clip_ids` bounds, verbatim from master §5.9: "**`clip_ids` must hold
    /// between 1 and 100 items.**" Observed by the server's own validator, so
    /// this is a real constraint rather than a guess — and it is enforced here
    /// so a 200-clip batch fails as one clear refusal instead of a server 422
    /// whose `detail` is an opaque Python repr.
    static constexpr int kMinPlaylistClipIds = 1;
    static constexpr int kMaxPlaylistClipIds = 100;

    explicit SunoLibraryMutations(SunoClient* client, QObject* parent = nullptr);

    /// Borrowed and nullable. Null means every verb refuses, which is the answer
    /// for the window between constructing this class and the account manager
    /// existing. Re-set it when the account is replaced.
    void setGateResolver(const GateResolver* gates) noexcept;

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// The C++-only master switch. **Off on construction.**
    ///
    /// `[[nodiscard]]` because a refused enable that nobody noticed looks
    /// exactly like an accepted one, and the failure is a feature that is
    /// silently dead. Returns false — changing nothing — if `enabled` is true
    /// while the owning gate is not `Available`, so the switch can never claim
    /// more than the evidence floor permits.
    ///
    /// Deliberately NOT a `Q_INVOKABLE`. A QML-reachable switch would let a UI
    /// flip gates at runtime, which is the hazard AGENTS.md §1 exists to
    /// prevent; the previous round declined the same affordance on
    /// `GateResolver` for the same reason and that decision is kept here.
    [[nodiscard]] bool setEnabled(bool enabled);

    /// Enabled **and** the gate permits. The single question a UI asks.
    [[nodiscard]] bool isAvailable() const;

    /// Why `isAvailable()` is false, naming which of the two layers refused.
    /// Empty when available. Shown rather than swallowed, because an
    /// unexplained disabled control is the well-built door to nowhere this
    /// project keeps building.
    ///
    /// **Precedence: the gate is named before the switch**, and that is
    /// deliberate. It mirrors `GateResolver`'s own documented resolution order,
    /// in which the server-flag step precedes the client-switch step, and it is
    /// also the more actionable sentence — telling a signed-out user to flip an
    /// internal build switch would be advice that cannot help them.
    [[nodiscard]] QString unavailableReason() const;

    /// True while at least one mutation is waiting for its reply.
    ///
    /// Not a throttle — `SunoClient` owns the request queue and its rate
    /// limiting — just the honest answer to "is that button still working?", and
    /// the number a UI needs to avoid leaving a card on "Saving" forever.
    [[nodiscard]] bool isBusy() const noexcept { return inFlight_ > 0; }

    /// Replies still outstanding. Diagnostics only; a UI wants `isBusy()`.
    [[nodiscard]] int inFlight() const noexcept { return inFlight_; }

    // ── The verbs ──────────────────────────────────────────────────────────
    // Each returns whether the request was ENQUEUED. Returning false means it
    // settled as `Refused` immediately, with the reason already delivered on
    // `mutationSettled` — so ignoring the bool cannot strand a UI on a pending
    // state. `[[nodiscard]]` anyway: it catches the caller who ignores the
    // outcome of a write they just asked for.
    //
    // Note what no parameter defaults to. A bool verb takes the target state
    // explicitly rather than "toggle what it thinks the current state is",
    // because this client cannot read the current state from a mutation reply
    // (no reply has been captured) and a toggle-by-guess is how a user's
    // visibility gets silently inverted.

    /// POST /api/gen/{gen_id}/set_visibility/ — body `is_public: boolean`.
    [[nodiscard]] bool setClipVisibility(const QString& genId, bool isPublic);

    /// POST /api/clips/{clip_id}/toggle_remixes/ — body `can_remix: boolean`.
    [[nodiscard]] bool setClipRemixPermission(const QString& clipId, bool canRemix);

    /// POST /api/clips/{clip_id}/toggle_show_remixes — body `show_remix: boolean`.
    [[nodiscard]] bool setClipShowRemixes(const QString& clipId, bool showRemix);

    /// POST /api/gen/{gen_id}/update_feedback_state/ — body
    /// `feedback_reason: string`.
    ///
    /// The *members* of that string were not captured — only its type — so
    /// `feedbackReason` is passed through verbatim and never checked against a
    /// list. Guessing the member set would reject valid values, and the capture
    /// supports no member set at all.
    [[nodiscard]] bool setClipFeedbackState(const QString& genId,
                                            const QString& feedbackReason);

    /// POST /api/share/link — body `content_id: string`, `content_type: string`.
    ///
    /// Both are bare `string` in the capture. The allowed values of
    /// `content_type` were **not** observed, so `contentType` is not
    /// constrained to any guessed vocabulary; an empty *identifier* is refused
    /// because it would produce a meaningless request, but the type string is
    /// the server's to judge.
    ///
    /// Nothing reads a link out of the response, because no response was
    /// captured. This mints a link the server will honour and this client
    /// cannot yet name.
    [[nodiscard]] bool requestShareLink(const QString& contentId,
                                        const QString& contentType);

    /// POST /api/playlist/set_metadata — body `name`, `description`,
    /// `playlist_id`, all strings.
    [[nodiscard]] bool setPlaylistMetadata(const QString& playlistId,
                                           const QString& name,
                                           const QString& description);

    /// POST /api/playlist/update_clips/ — body `playlist_id: string`,
    /// `update_type` (one of four, validated), and `metadata: object`.
    ///
    /// **The contents of `metadata` were not captured.** Only that it is an
    /// object. So it is forwarded verbatim and no key inside it is invented or
    /// interpreted. This makes the call reachable and honest but not yet usable
    /// as a playlist editor, and that is the correct state until someone
    /// captures what `metadata` contains.
    [[nodiscard]] bool updatePlaylistClips(const QString& playlistId,
                                           PlaylistUpdateType updateType,
                                           const QJsonObject& metadata);

    /// POST /api/playlist/v2/{id}/tracks/add — body `clip_ids`, 1–100.
    [[nodiscard]] bool addClipsToPlaylist(const QString& playlistId,
                                          const QStringList& clipIds);

    /// POST /api/playlist/v2/{id}/tracks/remove — body `clip_ids`, 1–100.
    [[nodiscard]] bool removeClipsFromPlaylist(const QString& playlistId,
                                               const QStringList& clipIds);

    /// POST /api/playlist/v2/{id}/tracks/reorder-by-index — body `positions`.
    ///
    /// The capture establishes that `positions` is an array and nothing about
    /// its elements. Elements are therefore forwarded as given, and their shape
    /// is explicitly not validated: inventing an element schema here would be
    /// the same fabrication as inventing a response schema.
    [[nodiscard]] bool reorderPlaylistTracks(const QString& playlistId,
                                             const QJsonArray& positions);

    /// POST /api/gen/trash — body `trash: boolean`, `clip_ids: array`.
    ///
    /// UI-07 [T1]: `{"trash":false,"clip_ids":[…]}` -> `{"ids":[…],
    /// "is_trashed":false}` (restore-to-library); `trash:true` is the twin
    /// direction, same route. Batch: the capture shows arrays, so this takes
    /// a list. Only non-emptiness is enforced — the 1–100 `clip_ids` bound
    /// is captured for the playlist tracks routes, not for this one.
    [[nodiscard]] bool setClipsTrashed(const QStringList& clipIds, bool trashed);

    /// POST /api/playlist/v2/{id}/trash — body `undo: boolean`.
    ///
    /// UI-07 [T1]: `{"undo":false}` (trash) and `{"undo":true}` (recover)
    /// both -> `{"success":true,"updated_fields":["is_trashed"],"errors":{}}`.
    /// Prefer over legacy `playlist/trash/` (204, no outcome). Takes the
    /// *trashed* state so callers read like every other verb; `undo` is
    /// derived as `!trashed`.
    [[nodiscard]] bool setPlaylistTrashed(const QString& playlistId, bool trashed);

    /// POST /api/playlist/v2/{id}/cover-image — body
    /// `{"cover_source": {"id": <image_s3_id>, "type": "generated"}}` (§5.15).
    ///
    /// The id is an *image* id (`image_<uuid>`), the `image_s3_id` that
    /// `SunoCoverArtService` generates — passing a clip id earns a 404
    /// `cover_source_not_found`. No success body has been captured for this
    /// route, so like every other verb a 2xx settles `Accepted` and parses
    /// nothing; a non-2xx settles `Rejected` with the reply's string `detail`.
    [[nodiscard]] bool setPlaylistCoverImage(const QString& playlistId,
                                             const QString& imageId);

    /// POST /api/gen/{gen_id}/update_reaction_type/ — body `reaction`,
    /// `recommendation_metadata: object`.
    ///
    /// §5.13: `{"reaction":"LIKE"|"DISLIKE"|null,
    /// "recommendation_metadata":{}}` -> `{play_count, skip_count, flagged,
    /// reaction_type, clip, updated_at}`, where `reaction_type` is the request
    /// value's initial and is **absent — never `""` — when the request sent
    /// `null`**. A caller reads a cleared reaction as "the key is missing", so
    /// nothing here parses it; the verb settles `Accepted` like every other.
    [[nodiscard]] bool setReaction(const QString& genId,
                                   std::optional<ReactionType> reaction);

    /// POST /api/playlist/create/ — body `name`, `description`.
    ///
    /// §5.13: the reply is the full 25-key playlist object. It is not parsed,
    /// for the reason in this file's header, and `MutationResult` carries no
    /// playlist: a caller that wants the new object re-reads `/playlist/me`.
    [[nodiscard]] bool createPlaylist(const QString& name,
                                      const QString& description);

    /// POST /api/playlist/trash/ — body `playlist_id`, **flat**.
    ///
    /// §5.13: success is `204 No Content` with an empty body, so there is
    /// nothing to parse even though a shape was captured. The nested
    /// `{"spec":{"playlist_id":…}}` variant answers 422 while naming
    /// `spec.playlist_id` — the route accepts more than one body schema and the
    /// error reports the wrong one — which is why this body is flat.
    ///
    /// `setPlaylistTrashed` (v2) is the better verb: it reports per-field
    /// outcome, where this route reports nothing at all.
    [[nodiscard]] bool trashPlaylist(const QString& playlistId);

    // ── Body and endpoint builders ──────────────────────────────────────────
    // Public and static so the request side can be exercised without a network.
    // Every one is flat by construction — a top-level QJsonObject with no
    // wrapper — because §1.5 records that a nested body fails validation where
    // the flat one reaches the handler, even though the server's own error
    // message reports a `loc` chain implying nesting. That error is a renamed
    // projection and must never be walked to build a payload. The single
    // exception is `playlistCoverImageBody`, nested under `cover_source` per
    // §5.15's captured body.

    [[nodiscard]] static QJsonObject visibilityBody(bool isPublic);
    [[nodiscard]] static QJsonObject remixPermissionBody(bool canRemix);
    [[nodiscard]] static QJsonObject showRemixesBody(bool showRemix);
    [[nodiscard]] static QJsonObject feedbackStateBody(const QString& feedbackReason);
    [[nodiscard]] static QJsonObject shareLinkBody(const QString& contentId,
                                                  const QString& contentType);
    [[nodiscard]] static QJsonObject playlistMetadataBody(const QString& playlistId,
                                                          const QString& name,
                                                          const QString& description);
    [[nodiscard]] static QJsonObject playlistUpdateClipsBody(
            const QString& playlistId, PlaylistUpdateType updateType,
            const QJsonObject& metadata);
    [[nodiscard]] static QJsonObject playlistClipIdsBody(const QStringList& clipIds);
    [[nodiscard]] static QJsonObject playlistReorderBody(const QJsonArray& positions);
    [[nodiscard]] static QJsonObject playlistCoverImageBody(const QString& imageId);
    [[nodiscard]] static QJsonObject clipTrashBody(const QStringList& clipIds, bool trash);
    [[nodiscard]] static QJsonObject playlistTrashBody(bool undo);
    [[nodiscard]] static QJsonObject reactionBody(std::optional<ReactionType> reaction);
    [[nodiscard]] static QJsonObject playlistCreateBody(const QString& name,
                                                        const QString& description);
    [[nodiscard]] static QJsonObject legacyPlaylistTrashBody(const QString& playlistId);
    [[nodiscard]] static QJsonObject metadataBody(const ClipMetadataUpdate& metadata);
    [[nodiscard]] static QJsonObject audioDescriptionBody(
            const AudioDescriptionConsent& consent);

    /// The status-only verdict `handleReply` settles on, extracted so the
    /// discipline §5.13 made testable can be pinned without a network: **a 204
    /// with no body is a success.** Nothing else about the reply participates.
    [[nodiscard]] static MutationOutcome outcomeForStatus(int httpStatus) noexcept;

    /// Path templates with the `{id}` placeholder filled from `id`.
    ///
    /// The segment is percent-encoded. Captured ids are UUIDs, for which
    /// encoding is the identity, so this changes nothing for real traffic — but
    /// an id arriving from a feed body is server-controlled text, and an
    /// unencoded `/` or `..` in it would reshape the route rather than select a
    /// clip. This is defence in depth, not a claim that the ids are hostile.
    [[nodiscard]] static QString endpointFor(MutationVerb verb, const QString& id);

    /// The owning `FeatureGate` for a verb, for the resolver query. Implemented
    /// in the .cpp because it needs `FeatureFlags.hpp`.
    [[nodiscard]] static const char* surfaceTitleFor(MutationVerb verb) noexcept;

signals:
    /// One mutation reached a terminal state, carrying the id it is about.
    ///
    /// One signal per subject rather than an aggregate string, for the reason
    /// `clipSaveRefused` was added: a batch that collapses N outcomes into one
    /// unattributable sentence leaves every failing card stuck on "Saving"
    /// while one of them silently did nothing. A UI binds this and updates the
    /// card whose id arrived.
    void mutationSettled(const vc::suno::MutationResult& result);

    /// `isAvailable()`/`unavailableReason()` may have changed — the resolver's
    /// verdict moved when the server flag map arrived, or the switch changed.
    void availabilityChanged();

private:
    /// The single send path. Every verb funnels through here, which is what
    /// makes "no second HTTP path" a structural property rather than a promise.
    [[nodiscard]] bool dispatch(MutationVerb verb, const QString& subjectId,
                                const QString& endpoint, const QJsonObject& body);

    void handleReply(QNetworkReply* reply, MutationVerb verb, const QString& subjectId);

    /// The one and only emit path, so a result can never be reported twice or
    /// not at all.
    void settle(MutationVerb verb, const QString& subjectId, MutationOutcome outcome,
                int httpStatus, const QString& reason);

    /// The gate query. Returns false with a populated `reasonOut` when the
    /// owning surface is not `Available`, including when `gates_` is null.
    ///
    /// Split out as a bool because `GateVerdict` is declared in
    /// `FeatureFlags.hpp`, and this header is included by a QML bridge that has
    /// no other reason to pull the whole feature catalog in.
    [[nodiscard]] bool gatePermits(MutationVerb verb, QString& reasonOut) const;

    /// Every surface this class can write to permits a write — one
    /// representative verb per surface. Used by BOTH `setEnabled` and
    /// `isAvailable`, deliberately: two different predicates would let the switch
    /// report success while `isAvailable()` reported the opposite, and a QML
    /// binding would then show enabled buttons over verbs that refuse.
    [[nodiscard]] bool everyGatePermits(QString* reasonOut) const;

    /// Shared body for the two `clip_ids` verbs, which differ only in route.
    /// Enforces the captured 1–100 bound here, once, so the two call sites
    /// cannot drift on it. `verbPhrase` completes the refusal sentence
    /// ("could not be added to").
    [[nodiscard]] bool playlistClipIdsMutation(MutationVerb verb, std::string_view tmpl,
                                               const QString& playlistId,
                                               const QStringList& clipIds,
                                               const QString& verbPhrase);

    /// Refuse locally with `reason` and report it. Returns false so a verb can
    /// `return refuse(...)`.
    [[nodiscard]] bool refuse(MutationVerb verb, const QString& subjectId,
                              const QString& reason);

    SunoClient* client_{nullptr};
    const GateResolver* gates_{nullptr};
    bool enabled_{false};
    /// In-flight count, for a read-only "busy" property. Not a queue limit —
    /// `SunoClient` owns the request queue and its rate limiting — just the
    /// number of replies this object is still waiting on.
    int inFlight_{0};
};

} // namespace vc::suno

Q_DECLARE_METATYPE(vc::suno::MutationResult)