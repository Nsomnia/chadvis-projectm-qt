#include <QtTest>

#include "suno/SunoAudioUploadService.hpp"
#include "suno/SunoClient.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cstring>
#include <utility>
#include <vector>

using namespace vc::suno;

class FakeReply final : public QNetworkReply
{
public:
    explicit FakeReply(const QNetworkRequest& request, const std::string& method)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(method == "POST" ? QNetworkAccessManager::PostOperation
                                      : QNetworkAccessManager::GetOperation);
        setOpenMode(ReadOnly | Unbuffered);
    }

    void abort() override
    {
        if (finished_)
        {
            return;
        }
        aborted_ = true;
        finished_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return payload_.size() - offset_ + QNetworkReply::bytesAvailable();
    }

    qint64 readData(char* data, qint64 maxSize) override
    {
        if (offset_ >= payload_.size())
        {
            return 0;
        }
        const qint64 count = qMin(maxSize, static_cast<qint64>(payload_.size()) - offset_);
        std::memcpy(data, payload_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

    void succeed(QByteArray body, int status)
    {
        if (finished_)
        {
            return;
        }
        payload_ = std::move(body);
        offset_ = 0;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        finished_ = true;
        emit finished();
    }

    [[nodiscard]] bool wasAborted() const
    { return aborted_; }

private:
    QByteArray payload_;
    qint64 offset_ = 0;
    bool finished_ = false;
    bool aborted_ = false;
};

static QString fakeBearer(const QString& marker)
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(
            QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)},
                                    {QStringLiteral("test"), marker}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

static CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

static QJsonObject initializerPayload()
{
    return {
        {QStringLiteral("id"), QStringLiteral("upload-123")},
        {QStringLiteral("url"),
         QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload?signature=opaque")},
        {QStringLiteral("is_file_uploaded"), false},
        {QStringLiteral("fields"), QJsonObject{
                                       {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
                                       {QStringLiteral("Content-Type"), QStringLiteral("audio/mp4")},
                                       {QStringLiteral("key"), QStringLiteral("key-123")},
                                       {QStringLiteral("policy"), QStringLiteral("policy-123")},
                                       {QStringLiteral("signature"), QStringLiteral("signature-123")},
                                   }},
    };
}

class TestSunoRequestEpoch : public QObject
{
    Q_OBJECT

private slots:
    void signOutClearsQueuedAndInflightWork()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request,
                                  const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        int callbackCount = 0;
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount](QNetworkReply*) { ++callbackCount; });
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount](QNetworkReply*) { ++callbackCount; });

        client.clearLocalCredentials();
        QTest::qWait(1200);
        QCOMPARE(replies.size(), std::size_t(0));
        QCOMPARE(callbackCount, 0);

        client.setToken(fakeBearer(QStringLiteral("two")).toStdString());
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount](QNetworkReply*) { ++callbackCount; });
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 2500);
        QPointer<FakeReply> inflight(replies.front());
        client.clearLocalCredentials();
        QVERIFY(inflight->wasAborted());
        QTRY_VERIFY_WITH_TIMEOUT(callbackCount == 0, 1000);
    }

    void credentialReplacementAbortsInflightWork()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request,
                                  const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        int callbackCount = 0;
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount](QNetworkReply*) { ++callbackCount; });
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 2500);
        QPointer<FakeReply> inflight(replies.front());
        client.setToken(fakeBearer(QStringLiteral("two")).toStdString());
        QCOMPARE(restored.count(), 3);
        QVERIFY(inflight->wasAborted());
        QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 0, 1000);
        QCOMPARE(client.token(), fakeBearer(QStringLiteral("two")));
    }

    void invalidatedRestoreWakesReadinessWaiters()
    {
        QSemaphore entered(0);
        QSemaphore release(0);
        auto backend = [&entered, &release](CredentialStoreWorker::Request request) {
            if (request.operation == CredentialStoreWorker::Operation::Restore)
            {
                entered.release();
                release.acquire();
            }
            return CredentialStoreWorker::Outcome{};
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, backend);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QVERIFY(entered.tryAcquire(1, 2000));
        client.setToken(fakeBearer(QStringLiteral("replacement")).toStdString());
        QCOMPARE(restored.count(), 0);
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(restored.count() >= 1, 2000);
    }

    void emptyReloadClearsActiveCredential()
    {
        int restoreCount = 0;
        auto backend = [&restoreCount](CredentialStoreWorker::Request request) {
            CredentialStoreWorker::Outcome outcome;
            if (request.operation == CredentialStoreWorker::Operation::Restore &&
                ++restoreCount == 1)
            {
                outcome.bearer = fakeBearer(QStringLiteral("initial"));
            }
            return outcome;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, backend);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_VERIFY_WITH_TIMEOUT(client.isAuthenticated(), 2000);
        client.setToken(fakeBearer(QStringLiteral("local")).toStdString());
        const int beforeReload = restored.count();
        client.reloadStoredCredentials();
        QTRY_VERIFY_WITH_TIMEOUT(restored.count() > beforeReload, 2000);
        QCOMPARE(client.authState(), vc::suno::auth::AuthState::Disconnected);
        QVERIFY(client.token().isEmpty());
    }

    void restoreInvalidationRebasesAuthWaiters()
    {
        QSemaphore entered(0);
        QSemaphore release(0);
        auto backend = [&entered, &release](CredentialStoreWorker::Request request) {
            CredentialStoreWorker::Outcome outcome;
            if (request.operation == CredentialStoreWorker::Operation::Restore)
            {
                entered.release();
                release.acquire();
                outcome.bearer = fakeBearer(QStringLiteral("restored"));
            }
            return outcome;
        };
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request,
                                  const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, backend, factory);
        QVERIFY(entered.tryAcquire(1, 2000));

        int callbackCount = 0;
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount](QNetworkReply* reply) {
                ++callbackCount;
                reply->deleteLater();
            });
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
        replies.front()->succeed(QByteArrayLiteral("{}"), 200);
        QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 1, 1000);
    }

    void currentEpochCallbackAndTerminalAuthFailure()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request,
                                  const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        int callbackCount = 0;
        QByteArray callbackBody;
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount, &callbackBody](QNetworkReply* reply) {
                ++callbackCount;
                callbackBody = reply->readAll();
                reply->deleteLater();
            });
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 2500);
        replies.front()->succeed(QByteArrayLiteral("{}"), 200);
        QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 1, 1000);
        QCOMPARE(callbackBody, QByteArrayLiteral("{}"));

        client.enqueueAuthenticatedRequest(
            QStringLiteral("/session/"), "GET", {},
            [&callbackCount](QNetworkReply*) { ++callbackCount; });
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 2, 2500);
        QSignalSpy needsReauth(&client, &SunoClient::needsReauth);
        replies.back()->succeed(QByteArrayLiteral("unauthorized"), 401);
        QTRY_COMPARE_WITH_TIMEOUT(needsReauth.count(), 1, 1000);
        QCOMPARE(callbackCount, 1);
        QCOMPARE(client.authState(), vc::suno::auth::AuthState::NeedsReauth);
    }

    void mutationRequestDoesNotRetryAfter401()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request,
                                  const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("mutation")).toStdString());

        int callbackCount = 0;
        client.enqueueAuthenticatedRequest(
            QStringLiteral("/notification/v2/read/"), "POST", {},
            [&callbackCount](QNetworkReply*) { ++callbackCount; },
            false);
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 2500);

        QSignalSpy needsReauth(&client, &SunoClient::needsReauth);
        replies.front()->succeed(QByteArrayLiteral("unauthorized"), 401);
        QTRY_COMPARE_WITH_TIMEOUT(needsReauth.count(), 1, 1000);
        QCOMPARE(replies.size(), std::size_t(1));
        QCOMPARE(callbackCount, 0);
    }

    void uploadCancelsOnCredentialReplacement()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("take.m4a"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("audio")), qint64(5));
        file.close();

        std::vector<FakeReply*> studioReplies;
        auto studioFactory = [&studioReplies](const QNetworkRequest& request,
                                              const std::string& method,
                                              const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            studioReplies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), studioFactory);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        std::vector<FakeReply*> directReplies;
        auto directFactory = [&directReplies](const QNetworkRequest& request,
                                              QHttpMultiPart*) {
            auto* reply = new FakeReply(request, "POST");
            directReplies.push_back(reply);
            return reply;
        };
        SunoAudioUploadService service(&client, nullptr, nullptr, directFactory);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);
        service.start(path);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 1, 2500);
        studioReplies.front()->succeed(
            QJsonDocument(initializerPayload()).toJson(QJsonDocument::Compact), 200);
        QTRY_VERIFY_WITH_TIMEOUT(directReplies.size() == 1, 1000);
        QPointer<FakeReply> direct(directReplies.front());

        client.setToken(fakeBearer(QStringLiteral("two")).toStdString());
        QVERIFY(direct->wasAborted());
        QTRY_VERIFY_WITH_TIMEOUT(!service.isUploading(), 1000);
        QCOMPARE(failed.count(), 1);
    }

    void uploadCancelsOnAuthLoss()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("take.m4a"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("audio")), qint64(5));
        file.close();

        std::vector<FakeReply*> studioReplies;
        auto studioFactory = [&studioReplies](const QNetworkRequest& request,
                                              const std::string& method,
                                              const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            studioReplies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), studioFactory);
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        std::vector<FakeReply*> directReplies;
        auto directFactory = [&directReplies](const QNetworkRequest& request,
                                              QHttpMultiPart*) {
            auto* reply = new FakeReply(request, "POST");
            directReplies.push_back(reply);
            return reply;
        };
        SunoAudioUploadService service(&client, nullptr, nullptr, directFactory);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);
        service.start(path);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 1, 2500);
        studioReplies.front()->succeed(
            QJsonDocument(initializerPayload()).toJson(QJsonDocument::Compact), 200);
        QTRY_VERIFY_WITH_TIMEOUT(directReplies.size() == 1, 1000);
        QVERIFY(service.isUploading());

        QPointer<FakeReply> direct(directReplies.front());
        client.clearLocalCredentials();
        QVERIFY(direct->wasAborted());
        QTRY_VERIFY_WITH_TIMEOUT(!service.isUploading(), 1000);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(service.error(), QStringLiteral("Not authenticated"));
    }
};

#include "test_SunoRequestEpoch.moc"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestSunoRequestEpoch test;
    return QTest::qExec(&test, argc, argv);
}
