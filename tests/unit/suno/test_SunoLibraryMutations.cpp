#include <QtTest>

#include "suno/SunoLibraryMutations.hpp"

#include <QJsonArray>

// Pins the UI-07 trash contract (ENDPOINT-INVENTORY §4.12): the two new
// verbs' flat bodies and path templates, and their fail-closed refusals.
// No client is attached, so this also pins that a disabled service refuses
// before anything is sent.
//
// T0348/T0363 extend it to the §5.13 success discipline — a 204 with no body is
// a success, a cleared reaction sends `null` and its reply omits
// `reaction_type` — and to the two upload consent records of §5.14.

using namespace vc::suno;

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
};

#include "test_SunoLibraryMutations.moc"

int runTestSunoLibraryMutations(int argc, char** argv) {
    TestSunoLibraryMutations tc;
    return QTest::qExec(&tc, argc, argv);
}
