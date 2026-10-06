#include "SunoLibraryMutations.hpp"

#include "FeatureFlags.hpp"
#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"
#include "core/Logger.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

namespace vc::suno {

namespace {

/// The one constant from §5.9 that is an ENUM with more than one member, and
/// therefore the only vocabulary this file is allowed to validate against.
/// `playlist/v2/{id}/cover-image`'s `type` has a single member and is checked
/// inline at the call site.
[[nodiscard]] constexpr bool isCapturedUpdateType(const PlaylistUpdateType type) noexcept
{
    switch (type) {
        case PlaylistUpdateType::Add:
        case PlaylistUpdateType::Remove:
        case PlaylistUpdateType::RemoveById:
        case PlaylistUpdateType::Reorder:
            return true;
    }
    return false;
}

/// Replace the single `{}` placeholder in a captured path template.
///
/// Rejects a template with no placeholder rather than returning it unchanged: a
/// `{}`-free path would silently address a *collection* instead of one clip,
/// which for `remove` or `reorder` is a bulk operation the user never asked for.
/// Every template this file uses has exactly one placeholder, so requiring it
/// exactly once is cheap and turns a future bad edit into a refusal.
[[nodiscard]] std::optional<QString> fillPath(std::string_view tmpl, const QString& id)
{
    const QString pattern = qstr(tmpl);
    const QString needle = QStringLiteral("{}");
    const qsizetype at = pattern.indexOf(needle);
    if (at < 0 || pattern.indexOf(needle, at + needle.size()) >= 0) {
        return std::nullopt;
    }
    QString filled = pattern;
    filled.replace(at, needle.size(), QUrl::toPercentEncoding(id));
    return filled;
}

/// The reason for a non-2xx, from the ONE response fact §5.9 actually records.
///
/// §1.5: the observed rejections are `{"detail": "<python repr()>"}` — a
/// *string*, and the `loc` chain beside it is a renamed projection rather than
/// a wire format. So a string `detail` is read and passed to the user verbatim,
/// which also means server business rules surface without this client having to
/// restate them ("Cannot make trashed clips public." is the one observed rule,
/// and it was reached by reaching the handler rather than by validation).
///
/// Anything else is deliberately NOT parsed. If `detail` is ever an array or an
/// object — i.e. the platform has moved to a real RFC 9457 problem document — a
/// structured parser would be *needed* and this function would be wrong, so the
/// shape is checked rather than assumed and an unfamiliar one falls back to a
/// status-only sentence. That is the failure mode this file is built to avoid:
/// a parser that reads fields nobody has seen.
[[nodiscard]] QString rejectionReason(const QByteArray& body, const int status)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonValue detail = document.object().value(QStringLiteral("detail"));
        if (detail.isString()) {
            const QString text = detail.toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return QStringLiteral("Suno rejected the request (HTTP %1).").arg(status);
}

[[nodiscard]] QString transportReason(QNetworkReply* reply)
{
    const QString text = reply->errorString();
    return text.isEmpty() ? QStringLiteral("The request could not be completed.") : text;
}

/// Which surface owns a verb. The single place that mapping exists.
///
///  * The three publication / remix controls go to `Sharing`: each is a
///    decision about who *else* may use a clip, which is what sharing is.
///    `share/link` is the fourth — it is the route that actually mints the
///    public link, so grouping it with the visibility flag it exists to publish
///    is the honest reading rather than a separate case.
///  * The feedback reaction goes to `Library`: `disliked` is a captured feed
///    filter key (§5.2), so it is a state on the user's own feed item rather
///    than a sharing act.
///  * The six playlist routes go to `Playlists`.
[[nodiscard]] FeatureGate gateFor(const MutationVerb verb) noexcept
{
    switch (verb) {
        case MutationVerb::SetVisibility:
        case MutationVerb::ToggleRemixPermission:
        case MutationVerb::ToggleShowRemixes:
        case MutationVerb::ShareLink:
            return FeatureGate::Sharing;
        case MutationVerb::UpdateFeedbackState:
            return FeatureGate::Library;
        case MutationVerb::PlaylistSetMetadata:
        case MutationVerb::PlaylistUpdateClips:
        case MutationVerb::PlaylistTracksAdd:
        case MutationVerb::PlaylistTracksRemove:
        case MutationVerb::PlaylistTracksReorder:
        case MutationVerb::PlaylistCoverImage:
            return FeatureGate::Playlists;
    }
    return FeatureGate::Library;
}

} // namespace

SunoLibraryMutations::SunoLibraryMutations(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client)
{
    qRegisterMetaType<vc::suno::MutationResult>("vc::suno::MutationResult");
}

void SunoLibraryMutations::setGateResolver(const GateResolver* gates) noexcept
{
    if (gates_ == gates) {
        return;
    }
    gates_ = gates;
    emit availabilityChanged();
}

bool SunoLibraryMutations::setEnabled(const bool enabled)
{
    if (enabled_ == enabled) {
        return true;
    }
    if (enabled) {
        QString reason;
        if (!everyGatePermits(&reason)) {
            LOG_WARN("SunoLibraryMutations: refused to enable: {}", reason.toStdString());
            return false;
        }
    }
    enabled_ = enabled;
    emit availabilityChanged();
    return true;
}

bool SunoLibraryMutations::isAvailable() const
{
    QString reason;
    return enabled_ && everyGatePermits(&reason);
}

QString SunoLibraryMutations::unavailableReason() const
{
    QString reason;
    if (!everyGatePermits(&reason)) {
        return reason;
    }
    if (!enabled_) {
        return QStringLiteral(
                "Library changes are switched off in this build. The request side "
                "is capture-backed, but no successful response has ever been "
                "captured, so this client cannot yet confirm what Suno does with "
                "the result.");
    }
    return {};
}

/// True only when EVERY surface this class can write to permits a write.
///
/// One representative verb per surface, not all eleven: three probes and three
/// surfaces is the whole set, and `gateFor` is what makes the grouping
/// checkable. The first refusal wins and its sentence names the surface, so a
/// refused enable is diagnosable rather than merely false.
///
/// This deliberately exists as one function used by BOTH `setEnabled` and
/// `isAvailable`. An earlier draft had `isAvailable` probe only the Sharing gate
/// while `setEnabled` probed three, which meant `isAvailable()` could answer
/// true while every playlist verb refused — the property would say "available"
/// over buttons that do nothing. One predicate, two callers, no possible
/// disagreement.
bool SunoLibraryMutations::everyGatePermits(QString* reasonOut) const
{
    static constexpr MutationVerb kRepresentative[]{
        MutationVerb::SetVisibility,       // Sharing
        MutationVerb::UpdateFeedbackState, // Library
        MutationVerb::PlaylistTracksAdd,   // Playlists
    };
    for (const MutationVerb verb : kRepresentative) {
        QString reason;
        if (!gatePermits(verb, reason)) {
            if (reasonOut) {
                *reasonOut = QStringLiteral("%1 is unavailable: %2")
                                      .arg(QString::fromLatin1(surfaceTitleFor(verb)),
                                           reason);
            }
            return false;
        }
    }
    if (reasonOut) {
        reasonOut->clear();
    }
    return true;
}

bool SunoLibraryMutations::gatePermits(const MutationVerb verb, QString& reasonOut) const
{
    if (gates_ == nullptr) {
        reasonOut = QStringLiteral(
                "Library changes need a signed-in Suno account, and no account "
                "capabilities have arrived yet.");
        return false;
    }
    const GateVerdict verdict = gates_->evaluate(gateFor(verb));
    if (verdict.status != GateStatus::Available) {
        reasonOut = verdict.reason;
        return false;
    }
    reasonOut.clear();
    return true;
}

// ── Request bodies ───────────────────────────────────────────────────────────
// Every one is a flat top-level object. §1.5: a nested body fails validation
// where the flat one reaches the handler, despite the server's error naming a
// nested `loc` chain — so these must never be wrapped, and must never be
// reconstructed by walking that error.

QJsonObject SunoLibraryMutations::visibilityBody(const bool isPublic)
{
    QJsonObject body;
    body.insert(QStringLiteral("is_public"), isPublic);
    return body;
}

QJsonObject SunoLibraryMutations::remixPermissionBody(const bool canRemix)
{
    QJsonObject body;
    body.insert(QStringLiteral("can_remix"), canRemix);
    return body;
}

QJsonObject SunoLibraryMutations::showRemixesBody(const bool showRemix)
{
    QJsonObject body;
    body.insert(QStringLiteral("show_remix"), showRemix);
    return body;
}

QJsonObject SunoLibraryMutations::feedbackStateBody(const QString& feedbackReason)
{
    QJsonObject body;
    body.insert(QStringLiteral("feedback_reason"), feedbackReason);
    return body;
}

QJsonObject SunoLibraryMutations::shareLinkBody(const QString& contentId,
                                                const QString& contentType)
{
    QJsonObject body;
    body.insert(QStringLiteral("content_id"), contentId);
    body.insert(QStringLiteral("content_type"), contentType);
    return body;
}

QJsonObject SunoLibraryMutations::playlistMetadataBody(const QString& playlistId,
                                                        const QString& name,
                                                        const QString& description)
{
    QJsonObject body;
    body.insert(QStringLiteral("playlist_id"), playlistId);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("description"), description);
    return body;
}

QJsonObject SunoLibraryMutations::playlistUpdateClipsBody(
        const QString& playlistId, const PlaylistUpdateType updateType,
        const QJsonObject& metadata)
{
    QJsonObject body;
    body.insert(QStringLiteral("playlist_id"), playlistId);
    body.insert(QStringLiteral("update_type"), QString::fromLatin1(toString(updateType)));
    // Verbatim. The capture establishes that `metadata` is an object and says
    // nothing about its keys, so nothing inside it is named, defaulted or
    // interpreted here.
    body.insert(QStringLiteral("metadata"), metadata);
    return body;
}

QJsonObject SunoLibraryMutations::playlistClipIdsBody(const QStringList& clipIds)
{
    QJsonObject body;
    QJsonArray array;
    for (const QString& clipId : clipIds) {
        array.append(clipId);
    }
    body.insert(QStringLiteral("clip_ids"), array);
    return body;
}

QJsonObject SunoLibraryMutations::playlistReorderBody(const QJsonArray& positions)
{
    QJsonObject body;
    // Elements forwarded as given. The capture says "positions: array" and
    // nothing else, so there is no element schema to check against.
    body.insert(QStringLiteral("positions"), positions);
    return body;
}

QJsonObject SunoLibraryMutations::playlistCoverImageBody(const QString& imageId)
{
    QJsonObject body;
    body.insert(QStringLiteral("id"), imageId);
    // The only captured single-member enum in §5.9. Spelled here rather than
    // parameterised, because `generated` is the whole member list.
    body.insert(QStringLiteral("type"), QStringLiteral("generated"));
    return body;
}

// ── Endpoints ────────────────────────────────────────────────────────────────

QString SunoLibraryMutations::endpointFor(const MutationVerb verb, const QString& id)
{
    std::string_view tmpl;
    switch (verb) {
        case MutationVerb::SetVisibility:
            tmpl = endpoints::GEN_SET_VISIBILITY;
            break;
        case MutationVerb::UpdateFeedbackState:
            tmpl = endpoints::GEN_UPDATE_FEEDBACK_STATE;
            break;
        case MutationVerb::ToggleRemixPermission:
            tmpl = endpoints::CLIP_TOGGLE_REMIXES;
            break;
        case MutationVerb::ToggleShowRemixes:
            tmpl = endpoints::CLIP_TOGGLE_SHOW_REMIXES;
            break;
        case MutationVerb::ShareLink:
        case MutationVerb::PlaylistSetMetadata:
        case MutationVerb::PlaylistUpdateClips:
            // Collection routes: no {id} in the template, and none may be
            // invented.
            return {};
        case MutationVerb::PlaylistTracksAdd:
            tmpl = endpoints::PLAYLIST_V2_TRACKS_ADD;
            break;
        case MutationVerb::PlaylistTracksRemove:
            tmpl = endpoints::PLAYLIST_V2_TRACKS_REMOVE;
            break;
        case MutationVerb::PlaylistTracksReorder:
            tmpl = endpoints::PLAYLIST_V2_TRACKS_REORDER;
            break;
        case MutationVerb::PlaylistCoverImage:
            tmpl = endpoints::PLAYLIST_V2_COVER_IMAGE;
            break;
    }
    const auto filled = fillPath(tmpl, id);
    return filled ? *filled : QString{};
}

const char* SunoLibraryMutations::surfaceTitleFor(const MutationVerb verb) noexcept
{
    return toString(gateFor(verb));
}

// ── Send path ────────────────────────────────────────────────────────────────

bool SunoLibraryMutations::refuse(const MutationVerb verb, const QString& subjectId,
                                  const QString& reason)
{
    // Not logged here: `settle` is the single reporting path and logs every
    // outcome. Logging in both places printed each refusal twice, which trains a
    // reader to skim past the line that matters.
    settle(verb, subjectId, MutationOutcome::Refused, 0, reason);
    return false;
}

bool SunoLibraryMutations::dispatch(const MutationVerb verb, const QString& subjectId,
                                    const QString& endpoint, const QJsonObject& body)
{
    QString reason;
    if (!enabled_) {
        return refuse(verb, subjectId,
                      QStringLiteral("Library changes are switched off in this build."));
    }
    if (!gatePermits(verb, reason)) {
        return refuse(verb, subjectId, reason);
    }
    if (endpoint.isEmpty()) {
        return refuse(verb, subjectId,
                      QStringLiteral("This change has no captured route."));
    }
    if (client_ == nullptr) {
        return refuse(verb, subjectId,
                      QStringLiteral("Library changes are unavailable because no Suno "
                                     "client is attached."));
    }
    // Checked here because `enqueueAuthenticatedRequest` does NOT call the
    // callback on this path — it reports through `SunoClient::errorOccurred`
    // instead — so without this check a request made while signed out would
    // leave the caller waiting for a settlement that never arrives.
    //
    // The other early-return in `enqueueAuthenticatedRequest` is
    // `resolveStudioApiUrl` failing, i.e. a path that is not a rooted `/api/…`
    // path or a host the allowlist refuses. Every endpoint this file builds is
    // a rooted path off the `API_BASE` constant and its host is fixed, so that
    // branch is unreachable by construction rather than by check. Guarding it
    // would be a test for a condition nothing can produce.
    if (!client_->isAuthenticated()) {
        return refuse(verb, subjectId,
                      QStringLiteral("Sign in to Suno to change your library."));
    }

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    ++inFlight_;
    QPointer<SunoLibraryMutations> guard(this);
    client_->enqueueAuthenticatedRequest(
            endpoint, "POST", payload,
            [guard, verb, subjectId](QNetworkReply* reply) {
                if (guard) {
                    guard->handleReply(reply, verb, subjectId);
                } else if (reply) {
                    reply->deleteLater();
                }
            },
            // No 401 retry: a mutation must not be replayed. If the bearer has
            // gone stale the server refused it, and the caller re-issues after
            // sign-in rather than this silently doubling a write.
            false);
    return true;
}

void SunoLibraryMutations::handleReply(QNetworkReply* reply, const MutationVerb verb,
                                       const QString& subjectId)
{
    if (inFlight_ > 0) {
        --inFlight_;
    }
    if (reply == nullptr) {
        settle(verb, subjectId, MutationOutcome::Failed, 0,
               QStringLiteral("The request produced no response."));
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString transport = transportReason(reply);

    // Body before transport error, and the order is load-bearing for the same
    // reason as everywhere else this accessor is used: `SunoClient` arms every
    // reply with a readyRead-driven BodyReader, so a body past the studio-API
    // cap was aborted mid-transfer and `errorString()` says only "Operation
    // canceled". The refusal that names the limit is the one this read finds.
    //
    // The body is read ONLY to recover a failure reason. On a 2xx it is read and
    // discarded: there is no captured success shape, so there is nothing it
    // could honestly be interpreted as.
    QByteArray body;
    if (client_ != nullptr) {
        auto read = client_->readTrackedBody(reply);
        if (read) {
            body = *read;
        } else {
            reply->deleteLater();
            settle(verb, subjectId, MutationOutcome::Failed, status, read.error());
            return;
        }
    } else {
        reply->deleteLater();
        settle(verb, subjectId, MutationOutcome::Failed, status,
               QStringLiteral("The response could not be read because no Suno client "
                              "is attached."));
        return;
    }
    reply->deleteLater();

    if (status >= 200 && status < 300) {
        // Accepted, and nothing further is claimed. No parsed reply, no field
        // read, no local state written. The caller re-reads the library.
        settle(verb, subjectId, MutationOutcome::Accepted, status, {});
        return;
    }
    if (!transport.isEmpty()) {
        settle(verb, subjectId, MutationOutcome::Failed, status, transport);
        return;
    }
    settle(verb, subjectId, MutationOutcome::Rejected, status,
           rejectionReason(body, status));
}

void SunoLibraryMutations::settle(const MutationVerb verb, const QString& subjectId,
                                  const MutationOutcome outcome, const int httpStatus,
                                  const QString& reason)
{
    MutationResult result;
    result.verb = verb;
    result.subjectId = subjectId;
    result.outcome = outcome;
    result.httpStatus = httpStatus;
    result.reason = reason;

    if (outcome == MutationOutcome::Accepted) {
        LOG_INFO("SunoLibraryMutations: {} accepted for {} (HTTP {}); response shape "
                 "unverified, no field read",
                 toString(verb), subjectId.toStdString(), httpStatus);
    } else if (httpStatus == 0) {
        // A local refusal has no HTTP status, and printing "HTTP 0" beside it
        // reads like a server answered zero rather than that nothing was sent.
        LOG_WARN("SunoLibraryMutations: {} {} for {}: {}", toString(outcome),
                 toString(verb), subjectId.toStdString(), reason.toStdString());
    } else {
        LOG_WARN("SunoLibraryMutations: {} {} for {} (HTTP {}): {}", toString(outcome),
                 toString(verb), subjectId.toStdString(), httpStatus,
                 reason.toStdString());
    }
    emit mutationSettled(result);
}

// ── The verbs ────────────────────────────────────────────────────────────────

bool SunoLibraryMutations::setClipVisibility(const QString& genId, const bool isPublic)
{
    if (genId.isEmpty()) {
        return refuse(MutationVerb::SetVisibility, genId,
                      QStringLiteral("This clip has no identifier, so its visibility "
                                     "cannot be changed."));
    }
    const QString endpoint = endpointFor(MutationVerb::SetVisibility, genId);
    return dispatch(MutationVerb::SetVisibility, genId, endpoint, visibilityBody(isPublic));
}

bool SunoLibraryMutations::setClipRemixPermission(const QString& clipId, const bool canRemix)
{
    if (clipId.isEmpty()) {
        return refuse(MutationVerb::ToggleRemixPermission, clipId,
                      QStringLiteral("This clip has no identifier, so remix permission "
                                     "cannot be changed."));
    }
    const QString endpoint = endpointFor(MutationVerb::ToggleRemixPermission, clipId);
    return dispatch(MutationVerb::ToggleRemixPermission, clipId, endpoint,
                    remixPermissionBody(canRemix));
}

bool SunoLibraryMutations::setClipShowRemixes(const QString& clipId, const bool showRemix)
{
    if (clipId.isEmpty()) {
        return refuse(MutationVerb::ToggleShowRemixes, clipId,
                      QStringLiteral("This clip has no identifier, so its remix list "
                                     "cannot be changed."));
    }
    const QString endpoint = endpointFor(MutationVerb::ToggleShowRemixes, clipId);
    return dispatch(MutationVerb::ToggleShowRemixes, clipId, endpoint,
                    showRemixesBody(showRemix));
}

bool SunoLibraryMutations::setClipFeedbackState(const QString& genId,
                                                const QString& feedbackReason)
{
    if (genId.isEmpty()) {
        return refuse(MutationVerb::UpdateFeedbackState, genId,
                      QStringLiteral("This clip has no identifier, so a reaction cannot "
                                     "be recorded."));
    }
    // `feedbackReason` is NOT checked against a member list. The capture
    // established `feedback_reason: string` and observed no member values, so
    // there is no vocabulary to validate against; the server decides.
    const QString endpoint = endpointFor(MutationVerb::UpdateFeedbackState, genId);
    return dispatch(MutationVerb::UpdateFeedbackState, genId, endpoint,
                    feedbackStateBody(feedbackReason));
}

bool SunoLibraryMutations::requestShareLink(const QString& contentId,
                                            const QString& contentType)
{
    if (contentId.isEmpty()) {
        return refuse(MutationVerb::ShareLink, contentId,
                      QStringLiteral("There is nothing to share: no content "
                                     "identifier was given."));
    }
    // `contentType` is passed through whatever the caller has. Its allowed
    // values were not captured, so restricting it to a guessed vocabulary would
    // reject valid requests on the strength of an assumption.
    return dispatch(MutationVerb::ShareLink, contentId, qstr(endpoints::SHARE_LINK),
                    shareLinkBody(contentId, contentType));
}

bool SunoLibraryMutations::setPlaylistMetadata(const QString& playlistId,
                                               const QString& name,
                                               const QString& description)
{
    if (playlistId.isEmpty()) {
        return refuse(MutationVerb::PlaylistSetMetadata, playlistId,
                      QStringLiteral("No playlist was named, so it cannot be renamed."));
    }
    return dispatch(MutationVerb::PlaylistSetMetadata, playlistId,
                    qstr(endpoints::PLAYLIST_SET_METADATA),
                    playlistMetadataBody(playlistId, name, description));
}

bool SunoLibraryMutations::updatePlaylistClips(const QString& playlistId,
                                               const PlaylistUpdateType updateType,
                                               const QJsonObject& metadata)
{
    if (playlistId.isEmpty()) {
        return refuse(MutationVerb::PlaylistUpdateClips, playlistId,
                      QStringLiteral("No playlist was named, so its clips cannot be "
                                     "changed."));
    }
    if (!isCapturedUpdateType(updateType)) {
        return refuse(MutationVerb::PlaylistUpdateClips, playlistId,
                      QStringLiteral("That kind of playlist change is not one Suno's "
                                     "captured contract accepts."));
    }
    return dispatch(MutationVerb::PlaylistUpdateClips, playlistId,
                    qstr(endpoints::PLAYLIST_UPDATE_CLIPS),
                    playlistUpdateClipsBody(playlistId, updateType, metadata));
}

bool SunoLibraryMutations::addClipsToPlaylist(const QString& playlistId,
                                              const QStringList& clipIds)
{
    return playlistClipIdsMutation(MutationVerb::PlaylistTracksAdd,
                                   endpoints::PLAYLIST_V2_TRACKS_ADD, playlistId, clipIds,
                                   QStringLiteral("added to"));
}

bool SunoLibraryMutations::removeClipsFromPlaylist(const QString& playlistId,
                                                   const QStringList& clipIds)
{
    return playlistClipIdsMutation(MutationVerb::PlaylistTracksRemove,
                                   endpoints::PLAYLIST_V2_TRACKS_REMOVE, playlistId, clipIds,
                                   QStringLiteral("removed from"));
}

bool SunoLibraryMutations::playlistClipIdsMutation(const MutationVerb verb,
                                                   const std::string_view tmpl,
                                                   const QString& playlistId,
                                                   const QStringList& clipIds,
                                                   const QString& verbPhrase)
{
    if (playlistId.isEmpty()) {
        return refuse(verb, playlistId,
                      QStringLiteral("No playlist was named, so no clip can be %1 it.")
                          .arg(verbPhrase));
    }
    // The 1–100 bound is a captured server rule (§5.9: "**`clip_ids` must hold
    // between 1 and 100 items.**"), enforced here so an oversized batch fails as
    // one clear local refusal rather than a 422 whose `detail` is an opaque
    // Python repr. The count is of *requested ids*, and empty ids are refused
    // separately below, so a list of 100 blanks cannot pass as 100 clips.
    if (clipIds.size() < kMinPlaylistClipIds) {
        return refuse(verb, playlistId,
                      QStringLiteral("No clips were selected, so nothing was %1 the "
                                     "playlist.")
                          .arg(verbPhrase));
    }
    if (clipIds.size() > kMaxPlaylistClipIds) {
        return refuse(verb, playlistId,
                      QStringLiteral("Suno accepts between %1 and %2 clips per request; "
                                     "%3 were selected.")
                          .arg(kMinPlaylistClipIds)
                          .arg(kMaxPlaylistClipIds)
                          .arg(clipIds.size()));
    }
    for (const QString& clipId : clipIds) {
        if (clipId.isEmpty()) {
            return refuse(verb, playlistId,
                          QStringLiteral("One of the selected clips has no identifier, "
                                         "so nothing was %1 the playlist.")
                              .arg(verbPhrase));
        }
    }
    const auto filled = fillPath(tmpl, playlistId);
    if (!filled) {
        return refuse(verb, playlistId,
                      QStringLiteral("This change has no captured route."));
    }
    return dispatch(verb, playlistId, *filled, playlistClipIdsBody(clipIds));
}

bool SunoLibraryMutations::reorderPlaylistTracks(const QString& playlistId,
                                                 const QJsonArray& positions)
{
    if (playlistId.isEmpty()) {
        return refuse(MutationVerb::PlaylistTracksReorder, playlistId,
                      QStringLiteral("No playlist was named, so its order cannot be "
                                     "changed."));
    }
    if (positions.isEmpty()) {
        return refuse(MutationVerb::PlaylistTracksReorder, playlistId,
                      QStringLiteral("No new order was given, so the playlist was not "
                                     "reordered."));
    }
    const QString endpoint = endpointFor(MutationVerb::PlaylistTracksReorder, playlistId);
    return dispatch(MutationVerb::PlaylistTracksReorder, playlistId, endpoint,
                    playlistReorderBody(positions));
}

bool SunoLibraryMutations::setPlaylistCoverImage(const QString& playlistId,
                                                 const QString& imageId)
{
    if (playlistId.isEmpty()) {
        return refuse(MutationVerb::PlaylistCoverImage, playlistId,
                      QStringLiteral("No playlist was named, so its cover cannot be "
                                     "changed."));
    }
    if (imageId.isEmpty()) {
        return refuse(MutationVerb::PlaylistCoverImage, playlistId,
                      QStringLiteral("No image was named, so the playlist cover was not "
                                     "changed."));
    }
    const QString endpoint = endpointFor(MutationVerb::PlaylistCoverImage, playlistId);
    return dispatch(MutationVerb::PlaylistCoverImage, playlistId, endpoint,
                    playlistCoverImageBody(imageId));
}

} // namespace vc::suno