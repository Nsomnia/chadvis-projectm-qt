#include <QtTest>
#include "suno/ClipParser.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace vc::suno;
using vc::i64;

namespace {

/// Golden clip fixture built from the captured feed/v3 schema (T1, Aug 2026).
const char* kFullClipJson = "{\n    \"status\": \"complete\",\n    \"id\": \"3f2b8a1e-1111-4ccc-9ddd-222233334444\",\n    \"title\": \"Neon Arch\",\n    \"play_count\": 12345,\n    \"upvote_count\": 678,\n    \"allow_comments\": true,\n    \"is_verified\": false,\n    \"entity_type\": \"song_schema\",\n    \"video_url\": \"\",\n    \"audio_url\": \"https://cdn1.suno.ai/3f2b8a1e.mp3\",\n    \"image_url\": \"https://cdn2.suno.ai/image.jpeg\",\n    \"image_large_url\": \"https://cdn2.suno.ai/image_large.jpeg\",\n    \"major_model_version\": \"v4\",\n    \"model_name\": \"chirp-v4\",\n    \"handle\": \"derek\",\n    \"display_name\": \"Derek V\",\n    \"user_id\": \"user-abc\",\n    \"is_public\": true,\n    \"is_trashed\": false,\n    \"is_liked\": true,\n    \"batch_index\": 2,\n    \"created_at\": \"2026-08-20T12:00:00.000Z\",\n    \"has_hook\": true,\n    \"is_persona_root\": false,\n    \"media_urls\": [\n        {\"url\": \"https://cdn1.suno.ai/stream.m4a\", \"content_type\": \"m4a-opus\",\n         \"delivery\": \"progressive\"},\n        {\"url\": \"https://cdn1.suno.ai/stream.mp3\", \"content_type\": \"mp3\",\n         \"delivery\": \"progressive\", \"encoding\": \"utf-8\"}\n    ],\n    \"metadata\": {\n        \"tags\": \"synthwave, arch\",\n        \"prompt\": \"a song about arch linux\",\n        \"duration\": \"3:21\",\n        \"lyrics\": \"[Verse]\\nbtw\"\n    },\n    \"totally_unknown_field\": {\"nested\": [1, 2, 3]}\n}";

QJsonObject objFrom(const char* json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

} // namespace

class TestClipParser : public QObject {
    Q_OBJECT

private slots:
    void fullClipRoundtrip() {
        auto parsed = ClipParser::parseClip(objFrom(kFullClipJson));
        QVERIFY(parsed.has_value());

        const SunoClip& clip = *parsed;
        QCOMPARE(QString::fromStdString(clip.id),
                 QStringLiteral("3f2b8a1e-1111-4ccc-9ddd-222233334444"));
        QCOMPARE(QString::fromStdString(clip.title), QStringLiteral("Neon Arch"));
        QCOMPARE(clip.play_count, static_cast<i64>(12345));
        QCOMPARE(clip.upvote_count, static_cast<i64>(678));
        QVERIFY(clip.allow_comments);
        QCOMPARE(QString::fromStdString(clip.entity_type), QStringLiteral("song_schema"));
        // video_url "" until ready; audio_url present when complete.
        QVERIFY(clip.video_url.empty());
        QCOMPARE(QString::fromStdString(clip.audio_url),
                 QStringLiteral("https://cdn1.suno.ai/3f2b8a1e.mp3"));
        QCOMPARE(QString::fromStdString(clip.model_name), QStringLiteral("chirp-v4"));
        QCOMPARE(QString::fromStdString(clip.major_model_version), QStringLiteral("v4"));
        QCOMPARE(clip.batch_index, static_cast<i64>(2));
        QVERIFY(clip.is_liked);
        QVERIFY(!clip.is_trashed);
        QVERIFY(clip.has_hook);
        QVERIFY(!clip.is_persona_root);

        // media_urls superset
        QCOMPARE(static_cast<i64>(clip.media_urls.size()), i64{2});
        QCOMPARE(QString::fromStdString(clip.media_urls[0].content_type),
                 QStringLiteral("m4a-opus"));
        QCOMPARE(QString::fromStdString(clip.media_urls[1].delivery),
                 QStringLiteral("progressive"));
        QCOMPARE(QString::fromStdString(clip.media_urls[1].encoding),
                 QStringLiteral("utf-8"));

        // metadata optionals
        QCOMPARE(QString::fromStdString(clip.metadata.tags),
                 QStringLiteral("synthwave, arch"));
        QCOMPARE(QString::fromStdString(clip.metadata.duration), QStringLiteral("3:21"));
        QCOMPARE(QString::fromStdString(clip.metadata.lyrics), QStringLiteral("[Verse]\nbtw"));
        QVERIFY(clip.metadata.negative_tags.empty());
        QVERIFY(clip.metadata.error_message.empty());

        // Unknown fields ignored liberally - no crash is the contract.
    }

    void minimalClipDefaults() {
        const QJsonObject obj{{"id", "abc-123"}};
        auto parsed = ClipParser::parseClip(obj);
        QVERIFY(parsed.has_value());
        QCOMPARE(QString::fromStdString(parsed->id), QStringLiteral("abc-123"));
        QVERIFY(parsed->title.empty());
        QVERIFY(parsed->audio_url.empty()); // "" until complete
        QCOMPARE(parsed->play_count, i64{0});
        QVERIFY(!parsed->is_verified);
        QVERIFY(parsed->metadata.prompt.empty());
        QVERIFY(parsed->media_urls.empty());
    }

    void missingIdIsError() {
        QVERIFY(!ClipParser::parseClip(QJsonObject{}).has_value());
        QVERIFY(!ClipParser::parseClip(objFrom("{\"title\": \"no id here\"}")).has_value());
    }

    void wrongTypesAreTolerated() {
        // Wrong types on every scalar field must skip the field, not crash.
        const QJsonObject bad{
            {"id", 42},                       // id as number -> falls back to error
            {"title", 123},
            {"play_count", "not-a-number"},
            {"is_liked", "banana"},
            {"media_urls", "not-an-array"},
            {"metadata", "not-an-object"},
        };
        // id of wrong type -> parse error (cannot dedupe downstream).
        auto parsed = ClipParser::parseClip(bad);
        QVERIFY(!parsed.has_value());

        // Same shape but with a valid id: everything else degrades to defaults.
        QJsonObject tolerable = bad;
        tolerable["id"] = "still-fine";
        auto ok = ClipParser::parseClip(tolerable);
        QVERIFY(ok.has_value());
        QVERIFY(ok->title.empty());
        QCOMPARE(ok->play_count, i64{0});
        QVERIFY(!ok->is_liked);
        QVERIFY(ok->media_urls.empty());
        QVERIFY(ok->metadata.prompt.empty());
    }

    void numericStringFieldsAccepted() {
        // Liberal reading: numeric strings and "True"/"False" strings.
        const QJsonObject obj{
            {"id", "x1"},
            {"play_count", "999"},
            {"is_liked", "True"},
            {"is_trashed", "False"},
        };
        auto parsed = ClipParser::parseClip(obj);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->play_count, i64{999});
        QVERIFY(parsed->is_liked);
        QVERIFY(!parsed->is_trashed);
    }

    void envelopeWithCursor() {
        QJsonArray clips;
        clips.append(objFrom("{\"id\": \"a\"}"));
        clips.append(objFrom("{\"id\": \"b\"}"));
        QJsonObject envelope{
            {"clips", clips},
            {"next_cursor", "uuid-next-page"},
            {"has_more", true},
        };

        auto page = ClipParser::parseFeedEnvelope(envelope);
        QVERIFY(page.has_value());
        QCOMPARE(page->clips.size(), 2);
        QCOMPARE(page->nextCursor, QStringLiteral("uuid-next-page"));
        QVERIFY(page->hasMore);
    }

    void envelopeExhausted() {
        // next_cursor ABSENT when exhausted; has_more false.
        QJsonArray clips;
        clips.append(objFrom("{\"id\": \"last\"}"));
        QJsonObject envelope{
            {"clips", clips},
            {"has_more", false},
        };

        auto page = ClipParser::parseFeedEnvelope(envelope);
        QVERIFY(page.has_value());
        QCOMPARE(page->clips.size(), 1);
        QVERIFY(page->nextCursor.isEmpty());
        QVERIFY(!page->hasMore);

        // has_more absent entirely: fall back to cursor presence.
        QJsonObject noFlag{{"clips", clips}};
        auto fallback = ClipParser::parseFeedEnvelope(noFlag);
        QVERIFY(fallback.has_value());
        QVERIFY(!fallback->hasMore);
    }

    void malformedEntriesSkippedNotFatal() {
        QJsonArray clips;
        clips.append(objFrom("{\"id\": \"good-one\", \"title\": \"Keep\"}"));
        clips.append(QJsonValue(QStringLiteral("just a string"))); // not an object
        clips.append(objFrom("{\"title\": \"missing id\"}"));        // no id
        clips.append(objFrom("{\"id\": \"good-two\", \"audio_url\": \"\"}"));

        auto parsed = ClipParser::parseClipArray(clips);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->size(), 2);
        QCOMPARE(QString::fromStdString((*parsed)[0].id), QStringLiteral("good-one"));
        QCOMPARE(QString::fromStdString((*parsed)[1].id), QStringLiteral("good-two"));
        QVERIFY((*parsed)[1].audio_url.empty()); // empty audio_url handled
    }

    void envelopeWithoutClipsArrayErrors() {
        QVERIFY(!ClipParser::parseFeedEnvelope(QJsonObject{{"foo", 1}}).has_value());
    }
};

int runTestClipParser(int argc, char** argv) {
    TestClipParser tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_ClipParser.moc"
