#include <QtTest>

#include "suno/SunoAccountManager.hpp"
#include "suno/SunoClient.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <string>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {

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

QByteArray onboardingBody()
{
    const QJsonObject root{{QStringLiteral("step"), QStringLiteral("intro")},
                           {QStringLiteral("completed_steps"), QJsonArray{QStringLiteral("profile")}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

struct Rig {
    Rig()
        : client(QStringLiteral("test-device"), nullptr, emptyBackend(),
                 [this](const QNetworkRequest& request, const std::string& method,
                        const QByteArray& body) {
                     auto* reply = new FakeReply(request, method);
                     requests.push_back(request);
                     methods.push_back(method);
                     bodies.push_back(body);
                     replies.push_back(reply);
                     return reply;
                 })
    {
        client.setToken(fakeBearer().toStdString());
    }

    SunoClient client;
    std::vector<QNetworkRequest> requests;
    std::vector<std::string> methods;
    std::vector<QByteArray> bodies;
    std::vector<FakeReply*> replies;
};

} // namespace

class TestSunoAccountManagerOnboarding : public QObject
{
    Q_OBJECT

private slots:
    void successful200KeepsRawEnvelope()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::onboardingCurrentReady);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);

        account.refreshOnboardingCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("GET"));
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/onboarding/current")));

        rig.replies.front()->respond(onboardingBody(), 200);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QCOMPARE(errors.count(), 0);
        QVERIFY(account.hasOnboardingState());
        QVERIFY(account.onboardingState().has_value());
        const QJsonObject raw = account.onboardingRaw();
        QCOMPARE(raw.value(QStringLiteral("step")).toString(), QStringLiteral("intro"));
        QCOMPARE(raw.value(QStringLiteral("completed_steps")).toArray().size(), 1);
    }

    void transportFailureStaysUnknown()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::onboardingCurrentReady);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);

        account.refreshOnboardingCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(onboardingBody(), 500);
        // 5xx is retried once: the second reply is the one the handler sees.
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(onboardingBody(), 500);

        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000);
        QCOMPARE(ready.count(), 0);
        QVERIFY(!account.hasOnboardingState());
        QVERIFY(!account.onboardingState().has_value());
        QVERIFY(account.onboardingRaw().isEmpty());
    }

    void emptyBodyStaysUnknown()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::onboardingCurrentReady);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);

        account.refreshOnboardingCurrent();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(QByteArray{}, 200);

        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000);
        QCOMPARE(ready.count(), 0);
        QVERIFY(!account.hasOnboardingState());
        QVERIFY(account.onboardingRaw().isEmpty());
    }
};

QTEST_MAIN(TestSunoAccountManagerOnboarding)
#include "test_SunoAccountManagerOnboarding.moc"
