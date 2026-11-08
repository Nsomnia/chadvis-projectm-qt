#include <QtTest>

#include "suno/SunoLibraryMutations.hpp"
#include "suno/SunoClient.hpp"
#include "suno/FeatureFlags.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <string_view>
#include <vector>

// Pins the UI-07 trash contract (ENDPOINT-INVENTORY §4.12): the two new
// verbs' flat bodies and path templates, and their fail-closed refusals.
// No client is attached, so this also pins that a disabled service refuses
// before anything is sent.
//
// T0348/T0363 extend it to the §5.13 success discipline — a 204 with no body is
// a success, a cleared reaction sends `null` and its reply omits
// `reaction_type` — and to the two upload consent records of §5.14.

using namespace vc::suno;
using fake_net::FakeReply;

namespace {

QString fakeBearer()
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(
            QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode(
        {{QStringLiteral("exp"), static_cast<qint64>(4102444800)}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) {
        return CredentialStoreWorker::Outcome{};
    };
}

/// Every catalog flag as an all-`true` map (the pattern in test_FeatureFlags):
/// the strongest probe the gate can be given, so any gate still not `Available`
/// under it was refused by exclusion or evidence floor, not by missing flags.
QJsonObject everyCatalogFlagEnabled()
{
    QJsonObject flags;
    for (const FeatureDefinition& def : featureCatalog()) {
        std::size_t start = 0;
        const std::string_view csv = def.serverFlags;
        while (start <= csv.size()) {
            const std::size_t comma = csv.find(',', start);
            const std::size_t end = comma == std::string_view::npos ? csv.size() : comma;
            const std::string_view token = csv.substr(start, end - start);
            if (!token.empty()) {
                flags.insert(QString::fromStdString(std::string(token)), true);
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
    }
    return flags;
}

GateResolver maximallyPermissiveResolver()
{
    QJsonObject env;
    env.insert(QStringLiteral("flags"), everyCatalogFlagEnabled());
    GateResolver resolver;
    resolver.setCapabilities(parseSessionCapabilities(env));
    for (const FeatureDefinition& def : featureCatalog()) {
        (void)resolver.setLocallyEnabled(def.gate, true);
    }
    return resolver;
}

FakeReply* findCoverReply(const std::vector<QPointer<FakeReply>>& replies)
{
    for (QPointer<FakeReply> reply : replies) {
        if (!reply.isNull() &&
            reply->url().path().endsWith(QStringLiteral("/cover-image"))) {
            return reply;
        }
    }
    return nullptr;
}

} // namespace

class TestSunoLibraryMutations : public QObject {
    Q_OBJECT

private slots:
    void clipTrashBodyIsFlatAndNamed() {
        const QJsonObject trash = SunoLibraryMutations::clipTrashBody(
            {QStringLiteral("clip-1"), QStringLiteral("clip-2")}, true);
        QCOMPARE(trash.size(), 2);
        QVERIFY(trash.value(QStringLiteral("trash")).toBool());
        const QJsonArray ids = trash.value(QStringLiteral("clip_ids")).toArray();
        QCOMPARE(ids.size(), 2);
        QCOMPARE(ids.at(0).toString(), QStringLiteral("clip-1"));

        const QJsonObject restore = SunoLibraryMutations::clipTrashBody(
            {QStringLiteral("clip-1")}, false);
        QVERIFY(!restore.value(QStringLiteral("trash")).toBool());
        QCOMPARE(restore.value(QStringLiteral("clip_ids")).toArray().size(), 1);
    }

    void playlistTrashBodyCarriesUndo() {
        const QJsonObject trash = SunoLibraryMutations::playlistTrashBody(false);
        QCOMPARE(trash.size(), 1);
        QVERIFY(!trash.value(QStringLiteral("undo")).toBool());

        const QJsonObject recover = SunoLibraryMutations::playlistTrashBody(true);
        QVERIFY(recover.value(QStringLiteral("undo")).toBool());
    }

    void endpointTemplatesAreCapturedPaths() {
        QCOMPARE(SunoLibraryMutations::endpointFor(MutationVerb::PlaylistTrashV2,
                                                   QStringLiteral("playlist-1")),
                 QStringLiteral("/playlist/v2/playlist-1/trash"));
        // `gen/trash` is a collection route: no id segment may be invented.
        QVERIFY(SunoLibraryMutations::endpointFor(MutationVerb::ClipTrashRestore,
                                                  QStringLiteral("clip-1"))
                    .isEmpty());
    }

    void emptyBatchIsRefusedLocally() {
        SunoLibraryMutations service(nullptr);
        QVERIFY(!service.isEnabled());
        QVERIFY(!service.setClipsTrashed({}, true));
        QVERIFY(!service.setClipsTrashed({QStringLiteral("")}, true));
        QVERIFY(!service.setPlaylistTrashed({}, true));
    }

    void validArgumentsRefuseWhileDisabled() {
        SunoLibraryMutations service(nullptr);
        QVERIFY(!service.setClipsTrashed({QStringLiteral("clip-1")}, true));
        QVERIFY(!service.setPlaylistTrashed(QStringLiteral("playlist-1"), true));
        QVERIFY(!service.isAvailable());
    }

    void verbNamesAreDistinctAndStable() {
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::ClipTrashRestore)),
                 QStringLiteral("clip-trash-restore"));
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::PlaylistTrashV2)),
                 QStringLiteral("playlist-trash-v2"));
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::UpdateReactionType)),
                 QStringLiteral("update-reaction-type"));
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::PlaylistCreate)),
                 QStringLiteral("playlist-create"));
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::PlaylistTrash)),
                 QStringLiteral("playlist-trash"));
    }

    // ── T0348: §5.13's three remaining verbs ──────────────────────────────

    void reactionBodySendsNullWhenCleared() {
        const QJsonObject like = SunoLibraryMutations::reactionBody(ReactionType::Like);
        QCOMPARE(like.size(), 2);
        QCOMPARE(like.value(QStringLiteral("reaction")).toString(),
                 QStringLiteral("LIKE"));
        QCOMPARE(like.value(QStringLiteral("recommendation_metadata")).toObject().size(), 0);

        const QJsonObject dislike =
                SunoLibraryMutations::reactionBody(ReactionType::Dislike);
        QCOMPARE(dislike.value(QStringLiteral("reaction")).toString(),
                 QStringLiteral("DISLIKE"));

        // §5.13: the clear is `{"reaction":null,...}`, and the reply then OMITS
        // `reaction_type` rather than sending `""`. Present-and-null, never an
        // omitted key and never an empty string.
        const QJsonObject cleared = SunoLibraryMutations::reactionBody(std::nullopt);
        QVERIFY(cleared.contains(QStringLiteral("reaction")));
        QVERIFY(cleared.value(QStringLiteral("reaction")).isNull());
        QCOMPARE(cleared.size(), 2);
    }

    void playlistCreateBodyIsNameAndDescriptionOnly() {
        // §5.13: request `{name, description}`, reply the full 25-key playlist
        // object. The request side is exactly those two keys; the reply is not
        // parsed, so nothing of the 25 is mirrored here.
        const QJsonObject body =
                SunoLibraryMutations::playlistCreateBody(QStringLiteral("Night drive"),
                                                         QStringLiteral("slow ones"));
        QCOMPARE(body.size(), 2);
        QCOMPARE(body.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Night drive"));
        QCOMPARE(body.value(QStringLiteral("description")).toString(),
                 QStringLiteral("slow ones"));
    }

    void legacyPlaylistTrashBodyIsFlat() {
        // §5.13 trap 1: the flat body works and the nested `spec` variant
        // answers 422 naming a field it was given. One key, top level.
        const QJsonObject body =
                SunoLibraryMutations::legacyPlaylistTrashBody(QStringLiteral("playlist-1"));
        QCOMPARE(body.size(), 1);
        QCOMPARE(body.value(QStringLiteral("playlist_id")).toString(),
                 QStringLiteral("playlist-1"));
        QVERIFY(!body.contains(QStringLiteral("spec")));
    }

    void noContentIsSuccessAndNothingIsParsed() {
        // The whole response discipline. A 204 carries no body at all, so a
        // parser would fail on the SUCCESS path; this class reads no field of a
        // 2xx reply, whatever its size (25 keys included).
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(204), MutationOutcome::Accepted);
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(200), MutationOutcome::Accepted);
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(201), MutationOutcome::Accepted);
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(400), MutationOutcome::Rejected);
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(422), MutationOutcome::Rejected);
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(500), MutationOutcome::Failed);
        QCOMPARE(SunoLibraryMutations::outcomeForStatus(0), MutationOutcome::Failed);
    }

    void collectionRoutesInventNoId() {
        QVERIFY(SunoLibraryMutations::endpointFor(MutationVerb::PlaylistCreate,
                                                  QStringLiteral("playlist-1"))
                    .isEmpty());
        QVERIFY(SunoLibraryMutations::endpointFor(MutationVerb::PlaylistTrash,
                                                  QStringLiteral("playlist-1"))
                    .isEmpty());
        QCOMPARE(SunoLibraryMutations::endpointFor(MutationVerb::UpdateReactionType,
                                                   QStringLiteral("gen-1")),
                 QStringLiteral("/gen/gen-1/update_reaction_type/"));
    }

    void newVerbsRefuseWhileDisabled() {
        SunoLibraryMutations service(nullptr);
        QSignalSpy settled(&service, &SunoLibraryMutations::mutationSettled);

        QVERIFY(!service.setReaction(QStringLiteral("gen-1"), std::nullopt));
        QVERIFY(!service.createPlaylist(QStringLiteral("Mix"), {}));
        QVERIFY(!service.trashPlaylist(QStringLiteral("playlist-1")));
        QVERIFY(!service.setReaction({}, ReactionType::Like));

        QCOMPARE(settled.count(), 4);
        const auto last = settled.at(3).at(0).value<MutationResult>();
        QCOMPARE(last.verb, MutationVerb::UpdateReactionType);
        QCOMPARE(last.outcome, MutationOutcome::Refused);
        QVERIFY(!last.reason.isEmpty());
    }

    // ── T0363: the two upload consent records (§5.14) ─────────────────────

    void metadataBodyCarriesUploadTosFlag() {
        ClipMetadataUpdate metadata;
        metadata.title = QStringLiteral("Take 2");
        metadata.imageUrl = QStringLiteral("image-abc");
        metadata.isAudioUploadTosAccepted = true;

        const QJsonObject accepted = SunoLibraryMutations::metadataBody(metadata);
        QCOMPARE(accepted.size(), 3);
        QCOMPARE(accepted.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Take 2"));
        QCOMPARE(accepted.value(QStringLiteral("image_url")).toString(),
                 QStringLiteral("image-abc"));
        // A JSON bool, not the string "true": an upload must never be published
        // under a consent state that rotted into a string on the way out.
        QVERIFY(accepted.value(QStringLiteral("is_audio_upload_tos_accepted")).isBool());
        QVERIFY(accepted.value(QStringLiteral("is_audio_upload_tos_accepted")).toBool());

        metadata.isAudioUploadTosAccepted = false;
        const QJsonObject refused = SunoLibraryMutations::metadataBody(metadata);
        QVERIFY(!refused.value(QStringLiteral("is_audio_upload_tos_accepted")).toBool());
        // The key is still present: "not accepted" is a value, not an omission.
        QVERIFY(refused.contains(QStringLiteral("is_audio_upload_tos_accepted")));
    }

    void audioDescriptionBodyCarriesBothConsentFields() {
        AudioDescriptionConsent consent;
        consent.geminiDescriptionAccepted = true;
        consent.audioContentTypes = {QStringLiteral("Song Demo"), QStringLiteral("Voice"),
                                     QStringLiteral("Sample")};

        const QJsonObject accepted = SunoLibraryMutations::audioDescriptionBody(consent);
        QCOMPARE(accepted.size(), 2);
        QVERIFY(accepted.value(QStringLiteral("gemini_description_accepted")).isBool());
        QVERIFY(accepted.value(QStringLiteral("gemini_description_accepted")).toBool());
        const QJsonArray types = accepted.value(QStringLiteral("audio_content_types")).toArray();
        QCOMPARE(types.size(), 3);
        QCOMPARE(types.at(0).toString(), QStringLiteral("Song Demo"));
        QCOMPARE(types.at(2).toString(), QStringLiteral("Sample"));

        // No validation: §5.14 records the vocabulary as incomplete, so an
        // unfamiliar value is passed through rather than refused, and an empty
        // selection stays an empty array instead of being dropped.
        consent.geminiDescriptionAccepted = false;
        consent.audioContentTypes = {QStringLiteral("Uncaptured")};
        QCOMPARE(SunoLibraryMutations::audioDescriptionBody(consent)
                     .value(QStringLiteral("audio_content_types"))
                     .toArray()
                     .at(0)
                     .toString(),
                 QStringLiteral("Uncaptured"));
        consent.audioContentTypes.clear();
        const QJsonObject none = SunoLibraryMutations::audioDescriptionBody(consent);
        QVERIFY(!none.value(QStringLiteral("gemini_description_accepted")).toBool());
        QVERIFY(none.value(QStringLiteral("audio_content_types")).isArray());
        QCOMPARE(none.value(QStringLiteral("audio_content_types")).toArray().size(), 0);
    }

    // ── T0434: cover-image apply (§5.15) ────────────────────────────────

    void coverImageBodyIsNestedCoverSource() {
        // §5.15: {"cover_source": {"id": <image_s3_id>, "type": "generated"}} —
        // the one nested body in this file, and exactly the id
        // SunoCoverArtService's `imageS3Id` carries.
        const QJsonObject source{{QStringLiteral("id"),
                                  QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70")},
                                 {QStringLiteral("type"), QStringLiteral("generated")}};
        const QJsonObject expected{{QStringLiteral("cover_source"), source}};

        const QJsonObject body = SunoLibraryMutations::playlistCoverImageBody(
            QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70"));
        QCOMPARE(body.size(), 1);
        QCOMPARE(body.value(QStringLiteral("cover_source")).toObject(), source);
        QVERIFY(!body.contains(QStringLiteral("id")));
        QVERIFY(!body.contains(QStringLiteral("type")));
        QCOMPARE(QJsonDocument(body).toJson(QJsonDocument::Compact),
                 QJsonDocument(expected).toJson(QJsonDocument::Compact));
    }

    void coverImageEndpointAndVerbName() {
        QCOMPARE(SunoLibraryMutations::endpointFor(MutationVerb::PlaylistCoverImage,
                                                   QStringLiteral("playlist-1")),
                 QStringLiteral("/playlist/v2/playlist-1/cover-image"));
        QCOMPARE(QString::fromLatin1(toString(MutationVerb::PlaylistCoverImage)),
                 QStringLiteral("playlist-cover-image"));
    }

    void coverImageEmptyArgsAreRefused() {
        SunoLibraryMutations service(nullptr);
        QSignalSpy settled(&service, &SunoLibraryMutations::mutationSettled);

        QVERIFY(!service.setPlaylistCoverImage(
            {}, QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70")));
        QVERIFY(!service.setPlaylistCoverImage(QStringLiteral("playlist-1"), {}));

        QCOMPARE(settled.count(), 2);
        const auto first = settled.at(0).at(0).value<MutationResult>();
        QCOMPARE(first.verb, MutationVerb::PlaylistCoverImage);
        QCOMPARE(first.outcome, MutationOutcome::Refused);
        QCOMPARE(first.httpStatus, 0);
        QVERIFY(!first.reason.isEmpty());
    }

    void coverImageWireSendsNestedBodyAndAccepts() {
        std::vector<QPointer<FakeReply>> replies;
        std::vector<QByteArray> bodies;
        auto factory = [&replies, &bodies](const QNetworkRequest& request,
                                           const std::string& method,
                                           const QByteArray& data) {
            if (request.url().path().endsWith(QStringLiteral("/cover-image"))) {
                bodies.push_back(data);
            }
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        GateResolver resolver = maximallyPermissiveResolver();
        SunoLibraryMutations service(&client);
        service.setGateResolver(&resolver);
        QVERIFY2(service.setEnabled(true), qPrintable(service.unavailableReason()));
        QSignalSpy settled(&service, &SunoLibraryMutations::mutationSettled);

        QVERIFY(service.setPlaylistCoverImage(QStringLiteral("playlist-1"),
                                              QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70")));
        FakeReply* reply = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((reply = findCoverReply(replies)) != nullptr, 15000);
        QCOMPARE(static_cast<int>(reply->operation()),
                 static_cast<int>(QNetworkAccessManager::PostOperation));
        QVERIFY(!bodies.empty());
        QCOMPARE(QJsonDocument::fromJson(bodies.front()).object(),
                 SunoLibraryMutations::playlistCoverImageBody(
                     QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70")));

        // No success body has been captured for this route: a 2xx is `Accepted`
        // and parses nothing, per the class's general discipline.
        reply->respond(QByteArrayLiteral("{}"), 200);
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 15000);
        const auto result = settled.at(0).at(0).value<MutationResult>();
        QCOMPARE(result.verb, MutationVerb::PlaylistCoverImage);
        QCOMPARE(result.subjectId, QStringLiteral("playlist-1"));
        QCOMPARE(result.outcome, MutationOutcome::Accepted);
        QCOMPARE(result.httpStatus, 200);
        QVERIFY(result.reason.isEmpty());
    }

    void coverImageUnknownImageIdRejectsWithServerDetail() {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        GateResolver resolver = maximallyPermissiveResolver();
        SunoLibraryMutations service(&client);
        service.setGateResolver(&resolver);
        QVERIFY2(service.setEnabled(true), qPrintable(service.unavailableReason()));
        QSignalSpy settled(&service, &SunoLibraryMutations::mutationSettled);

        QVERIFY(service.setPlaylistCoverImage(QStringLiteral("playlist-1"),
                                              QStringLiteral("clip-9f10-not-an-image")));
        FakeReply* reply = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((reply = findCoverReply(replies)) != nullptr, 15000);

        // §5.13's captured failure for a non-image id: the string `detail`
        // carries the sentence; the other keys are not interpreted.
        const QJsonObject failure{{QStringLiteral("status_code"), 404},
                                  {QStringLiteral("detail"),
                                   QStringLiteral("Cover source not found.")},
                                  {QStringLiteral("detail_fallback"),
                                   QStringLiteral("Cover source not found.")},
                                  {QStringLiteral("error_type"),
                                   QStringLiteral("cover_source_not_found")},
                                  {QStringLiteral("retryable"), false}};
        reply->respond(QJsonDocument(failure).toJson(QJsonDocument::Compact), 404);

        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 15000);
        const auto result = settled.at(0).at(0).value<MutationResult>();
        QCOMPARE(result.outcome, MutationOutcome::Rejected);
        QCOMPARE(result.httpStatus, 404);
        QCOMPARE(result.reason, QStringLiteral("Cover source not found."));
    }

    void coverImageServerErrorSettlesWithoutRetry() {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        GateResolver resolver = maximallyPermissiveResolver();
        SunoLibraryMutations service(&client);
        service.setGateResolver(&resolver);
        QVERIFY2(service.setEnabled(true), qPrintable(service.unavailableReason()));
        QSignalSpy settled(&service, &SunoLibraryMutations::mutationSettled);

        QVERIFY(service.setPlaylistCoverImage(QStringLiteral("playlist-1"),
                                              QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70")));
        FakeReply* reply = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((reply = findCoverReply(replies)) != nullptr, 15000);
        reply->respond(QByteArrayLiteral("{}"), 500);

        // A mutation must not be replayed: SunoLibraryMutations passes
        // `retryOnUnauthorized=false`, and mayRetryTransientFailure requires
        // that flag for 5xx as well, so a 500 settles Failed on one reply,
        // with no second request. (Reads, which pass true, do retry once.)
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 15000);
        const auto result = settled.at(0).at(0).value<MutationResult>();
        QCOMPARE(result.outcome, MutationOutcome::Failed);
        QCOMPARE(result.httpStatus, 500);
        QVERIFY(result.reason.contains(QStringLiteral("500")));
        // Exactly one cover-image request was ever sent.
        QCOMPARE(replies.size(), std::size_t{1});
    }
};

#include "test_SunoLibraryMutations.moc"

int runTestSunoLibraryMutations(int argc, char** argv) {
    TestSunoLibraryMutations tc;
    return QTest::qExec(&tc, argc, argv);
}
