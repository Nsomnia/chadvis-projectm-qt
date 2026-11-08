#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoModelCatalog.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>

#include <cstring>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {


/// A JWT that decodes and carries a future `exp`, so setToken accepts it.
QString fakeBearer()
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

/// The captured `generation_config` shape: 18 capabilities, ordered, plus the
/// chaining matrix and the per-field character limits.
QByteArray catalogueBody()
{
    return QByteArrayLiteral(R"({
        "generation_config": {
            "major_version": "chirp-v4",
            "capabilities": [
                "generate", "extend", "upload_extend", "cover", "cover_extend",
                "artist_consistency", "artist_cover", "artist_extend", "infill",
                "infill_intro", "infill_outro", "artist_infill", "cover_infill",
                "playlist_condition", "underpainting", "overpainting", "vox", "vox_extend"
            ],
            "allowed_condition_combinations": [
                ["cover"],
                ["extend", "cover"],
                ["upload_extend", "cover_extend"]
            ],
            "max_lengths": {
                "title": 80, "prompt": 5000, "tags": 2000,
                "negative_tags": 1000, "gpt_description_prompt": 500
            }
        },
        "items": [
            {
                "name": "chirp-v4",
                "external_key": "chirp-v4",
                "major_version": "chirp-v4",
                "can_use": true,
                "is_default_model": true,
                "capabilities": ["generate", "cover", "upload_extend"],
                "max_lengths": {"title": 80, "prompt": 5000, "tags": 2000}
            }
        ]
    })");
}

} // namespace

class TestSunoModelCatalog : public QObject
{
    Q_OBJECT

private slots:
    void generationConfigKeepsCapabilitiesOrderAndTheChainingMatrix()
    {
        const QJsonDocument document = QJsonDocument::fromJson(catalogueBody());
        QVERIFY(document.isObject());

        const auto config =
                SunoModelCatalog::parseGenerationConfig(document.object());
        QVERIFY(config.has_value());
        QCOMPARE(config->majorVersion, QStringLiteral("chirp-v4"));

        // 18 captured capabilities, order preserved: the server's order is the
        // only ordering evidence there is.
        QCOMPARE(config->capabilities.size(), 18);
        QCOMPARE(config->capabilities.at(0), QStringLiteral("generate"));
        QCOMPARE(config->capabilities.at(2), QStringLiteral("upload_extend"));
        QCOMPARE(config->capabilities.at(4), QStringLiteral("cover_extend"));
        QCOMPARE(config->capabilities.at(17), QStringLiteral("vox_extend"));

        // The matrix is data, never a hard-coded mode list.
        QCOMPARE(config->allowedConditionCombinations.size(), 3);
        QCOMPARE(config->allowedConditionCombinations.at(1),
                 (QStringList{QStringLiteral("extend"), QStringLiteral("cover")}));
        QCOMPARE(config->allowedConditionCombinations.at(2),
                 (QStringList{QStringLiteral("upload_extend"),
                              QStringLiteral("cover_extend")}));

        QCOMPARE(config->maxLengths.title, 80);
        QCOMPARE(config->maxLengths.prompt, 5000);
        QCOMPARE(config->maxLengths.tags, 2000);
        QCOMPARE(config->maxLengths.negative_tags, 1000);
    }

    void missingGenerationConfigIsAnEmptyConfigNotAFailure()
    {
        const auto config = SunoModelCatalog::parseGenerationConfig(QJsonObject{});
        QVERIFY(config.has_value());
        QVERIFY(config->majorVersion.isEmpty());
        QVERIFY(config->capabilities.isEmpty());
        QVERIFY(config->allowedConditionCombinations.isEmpty());
        QCOMPARE(config->maxLengths.prompt, 0);
    }

    void itemsCarryTheirOwnCapabilitiesAndLimits()
    {
        const QJsonDocument document = QJsonDocument::fromJson(catalogueBody());
        const auto items = SunoModelCatalog::parseItems(document.object());
        QVERIFY(items.has_value());
        QCOMPARE(items->size(), 1);
        QCOMPARE(QString::fromStdString(items->at(0).external_key),
                 QStringLiteral("chirp-v4"));
        QVERIFY(items->at(0).can_use);
        QCOMPARE(static_cast<int>(items->at(0).capabilities.size()), 3);
        QCOMPARE(QString::fromStdString(items->at(0).capabilities.at(2)),
                 QStringLiteral("upload_extend"));
        QCOMPARE(items->at(0).max_lengths.title, 80);
    }

    void noClientReportsSignInAndStaysEmpty()
    {
        SunoModelCatalog catalog(nullptr);
        QSignalSpy failed(&catalog, &SunoModelCatalog::failed);
        QSignalSpy changed(&catalog, &SunoModelCatalog::catalogChanged);

        catalog.load();

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(catalog.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(catalog.items().isEmpty());
        QVERIFY(!catalog.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        SunoModelCatalog catalog(&client);
        QSignalSpy failed(&catalog, &SunoModelCatalog::failed);

        catalog.load();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(catalog.isLoading());
        QVERIFY(replies.front()->url().path().endsWith(
                QStringLiteral("/custom-model/shared")));

        replies.front()->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!catalog.isLoading());
        QVERIFY(catalog.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(catalog.items().isEmpty());
        QVERIFY(catalog.generation().capabilities.isEmpty());
    }

    void anHttpErrorDegradesToEmptyWithAReason()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoModelCatalog catalog(&client);
        QSignalSpy failed(&catalog, &SunoModelCatalog::failed);

        catalog.load();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        replies.front()->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is a transient failure: SunoClient re-sends once after its
        // 2 s floor, so the first reply alone never reaches the callback. The
        // second reply is the one whose status the service sees.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        replies.back()->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(catalog.failureReason().contains(QStringLiteral("500")));
        QVERIFY(catalog.items().isEmpty());
    }

    void aGoodBodyFillsEveryField()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoModelCatalog catalog(&client);
        QSignalSpy changed(&catalog, &SunoModelCatalog::catalogChanged);

        catalog.load();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        replies.front()->respond(catalogueBody(), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(catalog.generation().capabilities.size(), 18);
        QCOMPARE(catalog.generation().allowedConditionCombinations.size(), 3);
        QCOMPARE(catalog.generation().maxLengths.prompt, 5000);
        QCOMPARE(catalog.items().size(), 1);
        QVERIFY(catalog.failureReason().isEmpty());
        QVERIFY(!catalog.isLoading());
    }
};

QTEST_MAIN(TestSunoModelCatalog)
#include "test_SunoModelCatalog.moc"
