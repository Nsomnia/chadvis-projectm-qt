#include <QtTest>

#include "suno/SunoGenerationService.hpp"

#include <QJsonArray>
#include <QJsonObject>

using namespace vc::suno;

class TestSunoGenerationService : public QObject
{
    Q_OBJECT

private slots:
    void preflightReadsTheCapturedAnswer()
    {
        // Live 2026-10-30: `POST /c/check {"ctype":"generation"}` ->
        // `{"required": true, "captcha_version": 2}`.
        const auto required = SunoGenerationService::parsePreflight(
                QJsonObject{{QStringLiteral("required"), true},
                            {QStringLiteral("captcha_version"), 2}});
        QVERIFY(required.required);
        QCOMPARE(required.captchaVersion, 2);
        // Turnstile is provider 2; hCaptcha and anything unknown is provider 1.
        QCOMPARE(required.tokenProvider(), 2);
        QCOMPARE(SunoGenerationService::parsePreflight(
                         QJsonObject{{QStringLiteral("required"), true},
                                     {QStringLiteral("captcha_version"), 1}})
                         .tokenProvider(),
                 1);
    }

    void anEmptyPreflightMeansNoChallenge()
    {
        const auto preflight = SunoGenerationService::parsePreflight(QJsonObject{});
        QVERIFY(!preflight.required);
        QCOMPARE(preflight.captchaVersion, 0);
    }

    void theSubmitBodyIsFlatAndCarriesTheCapturedKeys()
    {
        SunoGenerationService::Request request;
        request.prompt = QStringLiteral("[Verse]\nhello");
        request.tags = QStringLiteral("pop, synths");
        request.title = QStringLiteral("Probe");
        request.model = QStringLiteral("chirp-v3-5");

        const QJsonObject body =
                SunoGenerationService::buildSubmitBody(request, QStringLiteral("tx-1"));

        // Flat: the `{params:{...}}` shape is the red herring the live 422 named.
        QVERIFY(!body.contains(QStringLiteral("params")));
        QCOMPARE(body.value(QStringLiteral("task")).toString(), QStringLiteral("generate"));
        QCOMPARE(body.value(QStringLiteral("generation_type")).toString(),
                 QStringLiteral("TEXT"));
        QCOMPARE(body.value(QStringLiteral("mv")).toString(), QStringLiteral("chirp-v3-5"));
        QCOMPARE(body.value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("[Verse]\nhello"));
        QCOMPARE(body.value(QStringLiteral("transaction_uuid")).toString(),
                 QStringLiteral("tx-1"));
        QVERIFY(body.value(QStringLiteral("metadata")).isObject());
        QCOMPARE(body.value(QStringLiteral("metadata"))
                         .toObject()
                         .value(QStringLiteral("create_mode"))
                         .toString(),
                 QStringLiteral("custom"));
        // No token means no provider claim.
        QVERIFY(!body.contains(QStringLiteral("token")));
        QVERIFY(!body.contains(QStringLiteral("token_provider")));
    }

    void aSolvedChallengeIsSentWithItsProvider()
    {
        SunoGenerationService::Request request;
        request.prompt = QStringLiteral("x");
        request.captchaToken = QStringLiteral("solved");
        request.captchaVersion = 2;
        const QJsonObject body =
                SunoGenerationService::buildSubmitBody(request, QStringLiteral("tx"));
        QCOMPARE(body.value(QStringLiteral("token")).toString(), QStringLiteral("solved"));
        QCOMPARE(body.value(QStringLiteral("token_provider")).toInt(), 2);
    }

    void aSubmitResponseNamesItsClips()
    {
        const QJsonObject root{
                {QStringLiteral("id"), QStringLiteral("batch-1")},
                {QStringLiteral("clips"),
                 QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")}},
                            QJsonObject{{QStringLiteral("id"), QStringLiteral("c2")}}}}};
        const auto ids = SunoGenerationService::parseSubmittedClipIds(root);
        QCOMPARE(ids.size(), std::size_t(2));
        QCOMPARE(QString::fromStdString(ids.at(0)), QStringLiteral("c1"));
    }

    void aResponseWithNoClipsFallsBackToTheBatchId()
    {
        const auto ids = SunoGenerationService::parseSubmittedClipIds(
                QJsonObject{{QStringLiteral("id"), QStringLiteral("batch-1")}});
        QCOMPARE(ids.size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(ids.at(0)), QStringLiteral("batch-1"));
    }

    void submitBeforeAPreflightIsRefusedNotGuessed()
    {
        // No client: the refusal must come from the preflight gate, not from a
        // null dereference and not from a submit that spent credits.
        SunoGenerationService service(nullptr);
        QSignalSpy failed(&service, &SunoGenerationService::failed);
        SunoGenerationService::Request request;
        request.prompt = QStringLiteral("x");
        service.submit(request);
        QCOMPARE(failed.count(), 1);
        QVERIFY(service.error().contains(QStringLiteral("preflight")));
    }
};

QTEST_MAIN(TestSunoGenerationService)
#include "test_SunoGenerationService.moc"
