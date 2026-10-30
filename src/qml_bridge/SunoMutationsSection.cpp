#include "SunoMutationsSection.hpp"

#include <QJsonArray>
#include <QTimer>
#include <QJsonObject>
#include <QJsonValue>

#include <optional>

#include "core/Logger.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoLibraryMutations.hpp"
#include "ui/controllers/SunoController.hpp"

namespace qml_bridge {

namespace {

using vc::suno::MutationOutcome;
using vc::suno::MutationVerb;
using vc::suno::PlaylistUpdateType;

/// True for the five clip-scoped verbs. The remaining six act on a playlist.
///
/// This is the routing table that keeps `clipMutationSettled` and
/// `playlistMutationSettled` honest: a playlist id must never arrive on a
/// signal a card delegate is listening to, or a clip card would update from a
/// playlist write.
[[nodiscard]] bool isClipScoped(const MutationVerb verb) noexcept {
    switch (verb) {
        case MutationVerb::SetVisibility:
        case MutationVerb::ToggleRemixPermission:
        case MutationVerb::ToggleShowRemixes:
        case MutationVerb::UpdateFeedbackState:
        case MutationVerb::ShareLink:
            return true;
        case MutationVerb::PlaylistSetMetadata:
        case MutationVerb::PlaylistUpdateClips:
        case MutationVerb::PlaylistTracksAdd:
        case MutationVerb::PlaylistTracksRemove:
        case MutationVerb::PlaylistTracksReorder:
        case MutationVerb::PlaylistCoverImage:
            return false;
    }
    return false;
}

/// `update_type` arrives from QML as a string. The four members ARE captured, so
/// this map is evidence-backed — and an unrecognised string returns nullopt
/// rather than defaulting to one of them, because a silent `add` where the user
/// asked for something else is the worst available outcome.
[[nodiscard]] std::optional<PlaylistUpdateType> playlistUpdateTypeFrom(const QString& text) {
    if (text == QLatin1String("add")) {
        return PlaylistUpdateType::Add;
    }
    if (text == QLatin1String("remove")) {
        return PlaylistUpdateType::Remove;
    }
    if (text == QLatin1String("remove_by_id")) {
        return PlaylistUpdateType::RemoveById;
    }
    if (text == QLatin1String("reorder")) {
        return PlaylistUpdateType::Reorder;
    }
    return std::nullopt;
}

} // namespace

SunoMutationsSection::SunoMutationsSection(QObject* parent) : QObject(parent) {}

SunoMutationsSection::~SunoMutationsSection() {
    destroyService();
}

void SunoMutationsSection::attach(vc::suno::SunoController* controller,
                                   vc::suno::SunoClient* client) {
    controller_ = controller;
    client_ = client;
    ensureServiceForController();
}

void SunoMutationsSection::detach() {
    destroyService();
    controller_ = nullptr;
    client_ = nullptr;
}

void SunoMutationsSection::attemptEnable() {
    if (!service_ || service_->isAvailable()) {
        return;
    }
    if (!service_->setEnabled(true)) {
        LOG_DEBUG("SunoMutationsSection: mutation surface stays off: {}",
                  service_->unavailableReason().toStdString());
        return;
    }
    LOG_INFO("SunoMutationsSection: mutation surface enabled");
}

void SunoMutationsSection::onGatesChanged() {
    // The gates arrive AFTER the services are constructed: `wireControllerSignals`
    // builds the mutation section first and enables the resolver's gates later in
    // the same function. So the enable attempted at construction is expected to be
    // refused on a cold start, and this is the retry that actually lands. Logged
    // once per attempt, not once per signal, because a refusal here is the honest
    // state and the reason is already in the sentence.
    attemptEnable();
    emit availabilityChanged();
}

bool SunoMutationsSection::available() const {
    return service_ && service_->isAvailable();
}

QString SunoMutationsSection::unavailableReason() const {
    if (!service_) {
        return QStringLiteral("Library changes need a signed-in Suno account.");
    }
    return service_->unavailableReason();
}

bool SunoMutationsSection::busy() const { return service_ && service_->isBusy(); }

vc::suno::SunoLibraryMutations* SunoMutationsSection::ensureService() {
    if (service_ && client_) {
        return service_;
    }
    ensureServiceForController();
    return service_;
}

void SunoMutationsSection::ensureServiceForController() {
    if (service_) {
        // Re-point rather than construct: the resolver lives inside
        // SunoAccountManager, which is replaced on every sign-in, so a pointer
        // captured at construction would dangle.
        if (controller_ && controller_->accountManager()) {
            service_->setGateResolver(&controller_->accountManager()->gates());
        }

        return;
    }
    if (!client_) {
        return;
    }
    service_ = new vc::suno::SunoLibraryMutations(client_, this);
    // Borrowed, not owned, and it must follow the account: the resolver lives in
    // SunoAccountManager, which is replaced on sign-in. A null one (no account
    // manager yet) fails closed inside the service.
    if (controller_ && controller_->accountManager()) {
        service_->setGateResolver(&controller_->accountManager()->gates());
    }
    // Enabled here, once, and only here: T0011 held this switch off because the
    // wire format was unproven. The 2026-10-26 captures (§5.13) and the owner's
    // 2026-10-30 grant settled that, so the verbs are reachable and the gate
    // resolver decides the rest. A refusal is logged with its reason rather than
    // swallowed -- `isAvailable()` still answers false if a gate says no.
    connect(service_, &vc::suno::SunoLibraryMutations::availabilityChanged, this,
            &SunoMutationsSection::availabilityChanged);
    connect(service_, &vc::suno::SunoLibraryMutations::mutationSettled, this,
            [this](const vc::suno::MutationResult& result) {
                settle(static_cast<int>(result.verb), result.subjectId,
                       static_cast<int>(result.outcome), result.reason);
            });
}

void SunoMutationsSection::destroyService() {
    if (service_) {
        disconnect(service_, nullptr, this, nullptr);
        delete service_;
        service_ = nullptr;
        emit busyChanged();
        emit availabilityChanged();
    }
}

void SunoMutationsSection::settle(const int verb, const QString& subjectId, const int outcome,
                                  const QString& reason) {
    const auto verbEnum = static_cast<MutationVerb>(verb);
    const auto outcomeEnum = static_cast<MutationOutcome>(outcome);
    const QString verbName = QString::fromLatin1(vc::suno::toString(verbEnum));
    const QString outcomeName = QString::fromLatin1(vc::suno::toString(outcomeEnum));

    if (isClipScoped(verbEnum)) {
        emit clipSettled(subjectId, verbName, outcomeName, reason);
    } else {
        emit playlistSettled(subjectId, verbName, outcomeName, reason);
    }

    if (outcomeEnum == MutationOutcome::Accepted) {
        // The one thing an `accepted` can honestly tell the UI: the library must
        // be re-read. Nothing was parsed out of the response and nothing was
        // written locally, so the displayed state is stale until a fetch.
        emit libraryRefreshRecommended();
    } else if (!reason.isEmpty()) {
        emit failure(reason);
    }
    emit busyChanged();
}

// ── The verbs ────────────────────────────────────────────────────────────────
// Each is `if (auto* m = ensureService()) m->…`, so a signed-out app gets the
// service's own one-sentence refusal on the settled signal rather than a
// silent no-op. Returning the `[[nodiscard]] bool` is deliberate and ignored
// here: the result already arrived as a signal, so the bool carries no
// information the caller lacks — and discarding it here is safe precisely
// because the settlement path is unconditional.

void SunoMutationsSection::setClipVisibility(const QString& clipId, const bool isPublic) {
    if (auto* m = ensureService()) {
        (void)m->setClipVisibility(clipId, isPublic);
    }
}

void SunoMutationsSection::setClipRemixPermission(const QString& clipId, const bool canRemix) {
    if (auto* m = ensureService()) {
        (void)m->setClipRemixPermission(clipId, canRemix);
    }
}

void SunoMutationsSection::setClipShowRemixes(const QString& clipId, const bool showRemix) {
    if (auto* m = ensureService()) {
        (void)m->setClipShowRemixes(clipId, showRemix);
    }
}

void SunoMutationsSection::setClipFeedback(const QString& clipId, const QString& reason) {
    if (auto* m = ensureService()) {
        (void)m->setClipFeedbackState(clipId, reason);
    }
}

void SunoMutationsSection::shareClip(const QString& clipId, const QString& contentType) {
    if (auto* m = ensureService()) {
        (void)m->requestShareLink(clipId, contentType);
    }
}

void SunoMutationsSection::setPlaylistMetadata(const QString& playlistId, const QString& name,
                                                const QString& description) {
    if (auto* m = ensureService()) {
        (void)m->setPlaylistMetadata(playlistId, name, description);
    }
}

void SunoMutationsSection::updatePlaylistClips(const QString& playlistId, const QString& updateType,
                                                const QVariantMap& metadata) {
    // The member list already lives here: `playlistUpdateTypeFrom` is the
    // string-side mirror of the captured enum in SunoLibraryMutations.hpp, so
    // refusing here creates no second owner to drift. And the refusal can
    // ONLY happen here: the service takes the enum, so a string that fails to
    // map could be sent only by first being mapped onto some member — which is
    // the guess. The previous fallthrough sent `Add` with empty metadata, a
    // real mutation for a request the caller never made.
    const auto type = playlistUpdateTypeFrom(updateType);
    if (!type) {
        // Through the single emit path, so a bridge-local refusal has exactly
        // the shape of a service-side one: the playlist-scoped settled signal,
        // outcome `refused`, reason also shown in the error channel. Before the
        // service, because a bad argument is bad regardless of session state.
        // The offending string is caller text and is deliberately not echoed.
        settle(static_cast<int>(MutationVerb::PlaylistUpdateClips), playlistId,
               static_cast<int>(MutationOutcome::Refused),
               QStringLiteral("That kind of playlist change is not one of "
                              "add, remove, remove_by_id or reorder, so "
                              "nothing was sent."));
        return;
    }
    auto* m = ensureService();
    if (!m) {
        return;
    }
    // QVariantMap -> QJsonObject, forwarded verbatim: nothing inside is named,
    // defaulted or interpreted, because the capture establishes only that
    // `metadata` is an object.
    QJsonObject json;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        json.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    (void)m->updatePlaylistClips(playlistId, *type, json);
}

void SunoMutationsSection::addClipsToPlaylist(const QString& playlistId,
                                               const QStringList& clipIds) {
    if (auto* m = ensureService()) {
        (void)m->addClipsToPlaylist(playlistId, clipIds);
    }
}

void SunoMutationsSection::removeClipsFromPlaylist(const QString& playlistId,
                                                    const QStringList& clipIds) {
    if (auto* m = ensureService()) {
        (void)m->removeClipsFromPlaylist(playlistId, clipIds);
    }
}

void SunoMutationsSection::reorderPlaylistTracks(const QString& playlistId,
                                                  const QVariantList& positions) {
    auto* m = ensureService();
    if (!m) {
        return;
    }
    // Elements forwarded as given. The capture says `positions` is an array and
    // nothing about its elements, so no element is validated or reshaped here.
    QJsonArray json;
    for (const QVariant& position : positions) {
        json.append(QJsonValue::fromVariant(position));
    }
    (void)m->reorderPlaylistTracks(playlistId, json);
}

void SunoMutationsSection::setPlaylistCoverImage(const QString& playlistId,
                                                  const QString& imageId) {
    if (auto* m = ensureService()) {
        (void)m->setPlaylistCoverImage(playlistId, imageId);
    }
}

} // namespace qml_bridge
