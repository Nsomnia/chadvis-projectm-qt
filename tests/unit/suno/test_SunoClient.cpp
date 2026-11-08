// test_SunoClient.cpp -- the status classification and retry decisions in
// SunoClient (T0215, T0216).
//
// Two facts are pinned here, both of which were silently wrong before:
//
//  * a 403 is an auth failure, and an auth failure is *refreshable*: the retry
//    path tested `status == 401`, so a 403 -- what a WAF or a rotated session
//    answers -- fell straight into the teardown path and signed the user out
//    (T0215);
//  * a 429/430/5xx is classified before any body is parsed. A rate-limit reply
//    carries a JSON body, and the route handlers read the body rather than the
//    status, so "you are going too fast" arrived as an answer (T0216).
//
// The one thing these tests cannot do is drive a *successful* bearer refresh:
// ClerkAuthClient owns its QNetworkAccessManager and SunoClient rebuilds it in
// resetClerkClient(), so there is no reply seam to fake. That is why the auth
// retry is pinned as a predicate (the decision the branch actually makes)
// rather than end to end, and why `hasCredentials() == false` is used to keep
// every behavioural case off the network.

#include <QtTest>

#include "suno/DownloadQueue.hpp"
#include "suno/SunoClient.hpp"
#include "FakeNetworkReply.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {

/// What the body reader sees for a 429 that carries valid JSON.
const char* const kJsonErrorBody = R"({"detail":"rate limited","clips":[]})";


/// A JWT that decodes and carries a future `exp`, so setToken accepts it.
QString fakeBearer(const QString& marker) {
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)},
                                    {QStringLiteral("test"), marker}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend() {
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

} // namespace

class TestSunoClient : public QObject {
    Q_OBJECT

private slots:
    void aForbiddenReplyIsRetryableExactlyLikeUnauthorized();
    void aForbiddenReplyWithoutARefreshableCredentialTearsDown();
    void retryAfterIsParsedAndClamped();
    void aRateLimitedReplyRetriesAfterTheRetryAfterSeconds();
    void aRateLimitedReplyRetriesAfterAnHttpDate();
    void aRateLimitedJsonBodyIsNeverReadAsTheAnswer();
    void aServerErrorOnGetIsRetriedOnce();
    void aServerErrorOnPostIsNotRetried();
    void aSearchTermIsSentAsFiltersSearchText();
    void anEmptySearchTermOmitsSearchText();
};

void TestSunoClient::aForbiddenReplyIsRetryableExactlyLikeUnauthorized() {
    // The exact condition handleReplyFinished evaluates. The 403 rows are the
    // regression: `status == 401` answered false to both of them (T0215).
    QVERIFY(mayRetryAuthFailure(401, /*retryOnUnauthorized=*/true, /*alreadyRetried=*/false,
                                /*hasCredentials=*/true));
    QVERIFY(mayRetryAuthFailure(403, true, false, true));

    // The three gates that must survive the widened status set.
    QVERIFY(!mayRetryAuthFailure(403, /*retryOnUnauthorized=*/false, false, true));
    QVERIFY(!mayRetryAuthFailure(403, true, /*alreadyRetried=*/true, true));
    QVERIFY(!mayRetryAuthFailure(403, true, false, /*hasCredentials=*/false));

    // A rate limit is not a credential verdict and must not refresh a bearer.
    QVERIFY(!mayRetryAuthFailure(429, true, false, true));
    QVERIFY(!mayRetryAuthFailure(430, true, false, true));
    QVERIFY(!mayRetryAuthFailure(500, true, false, true));
}

void TestSunoClient::aForbiddenReplyWithoutARefreshableCredentialTearsDown() {
    // A token-only client has nothing to mint a fresh bearer from, so a 403 has
    // to reach the teardown path rather than being retried forever. This is the
    // other half of the widened status set: a 403 is refreshable *when* there
    // is a refreshable credential.
    std::vector<FakeReply*> replies;
    auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                              const QByteArray&) {
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());
    QVERIFY(client.isAuthenticated());

    std::vector<std::string> errors;
    client.errorOccurred.connect([&errors](std::string message) {
        errors.push_back(std::move(message));
    });
    QSignalSpy reauth(&client, &SunoClient::needsReauth);

    client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                       [](QNetworkReply*) {});
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    replies.front()->respond({}, 403);

    QTRY_COMPARE_WITH_TIMEOUT(reauth.count(), 1, 2000);
    QCOMPARE(client.authState(), auth::AuthState::NeedsReauth);
    QCOMPARE(client.authFailureKind(), auth::AuthFailureKind::RejectedCredential);
    // Exactly one request: nothing was re-sent without a credential.
    QCOMPARE(replies.size(), std::size_t(1));
    QVERIFY(!errors.empty());
}

void TestSunoClient::retryAfterIsParsedAndClamped() {
    // Injected "now" so the HTTP-date form is deterministic. 784111777 is
    // Sun, 06 Nov 1994 08:49:37 GMT, the reference timestamp from the parser's
    // own comment.
    constexpr std::int64_t now = 784111777;
    QCOMPARE(transientRetryDelayMs("5", now), std::int64_t(5000));
    QCOMPARE(transientRetryDelayMs("0", now), kMinRetryAfterMs);
    QCOMPARE(transientRetryDelayMs("3600", now), kMaxRetryAfterMs);
    QCOMPARE(transientRetryDelayMs("Sun, 06 Nov 1994 08:49:37 GMT", now), kMinRetryAfterMs);
    QCOMPARE(transientRetryDelayMs("Sun, 06 Nov 1994 08:49:42 GMT", now), std::int64_t(5000));
    // A date already in the past means "retry now", floored rather than negative.
    QCOMPARE(transientRetryDelayMs("Sun, 06 Nov 1994 08:49:32 GMT", now), kMinRetryAfterMs);
    // Unparseable and malformed hints fall to the fixed floor, never to zero.
    QCOMPARE(transientRetryDelayMs("-5", now), std::int64_t(2000));
    QCOMPARE(transientRetryDelayMs("soon", now), std::int64_t(2000));
    QCOMPARE(transientRetryDelayMs(QByteArray(), now), std::int64_t(2000));
}

void TestSunoClient::aRateLimitedReplyRetriesAfterTheRetryAfterSeconds() {
    std::vector<FakeReply*> replies;
    auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                              const QByteArray&) {
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

    int callbackRuns = 0;
    client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                       [&callbackRuns](QNetworkReply*) { ++callbackRuns; });
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    replies.front()->respond(kJsonErrorBody, 429, "1");

    // The retry is the assertion: one backoff, then the same request is issued
    // again instead of the 429 body reaching the callback as a result.
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 2, 6000);
    QCOMPARE(callbackRuns, 0);

    replies.back()->respond(kJsonErrorBody, 429, "1");
    QTRY_COMPARE_WITH_TIMEOUT(callbackRuns, 1, 4000);
    // And only one retry: the third attempt is not issued.
    QTest::qWait(1500);
    QCOMPARE(replies.size(), std::size_t(2));
}

void TestSunoClient::aRateLimitedReplyRetriesAfterAnHttpDate() {
    std::vector<FakeReply*> replies;
    auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                              const QByteArray&) {
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

    int callbackRuns = 0;
    client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                       [&callbackRuns](QNetworkReply*) { ++callbackRuns; });
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    // A date in the past is "retry now", which the clamp raises to the 1 s
    // floor -- so the retry happens and the test does not wait on a clock.
    replies.front()->respond(kJsonErrorBody, 429, "Sun, 06 Nov 1994 08:49:37 GMT");

    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 2, 6000);
    QCOMPARE(callbackRuns, 0);
    replies.back()->respond(kJsonErrorBody, 429);
    QTRY_COMPARE_WITH_TIMEOUT(callbackRuns, 1, 4000);
}

void TestSunoClient::aRateLimitedJsonBodyIsNeverReadAsTheAnswer() {
    // The defect T0216 names: the body is valid JSON, so every route handler
    // parsed it and reported an empty result rather than a rate limit.
    std::vector<FakeReply*> replies;
    auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                              const QByteArray&) {
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

    int fedPages = 0;
    client.libraryFetched.connect([&fedPages](const auto&) { ++fedPages; });
    std::vector<std::string> errors;
    client.errorOccurred.connect([&errors](std::string message) {
        errors.push_back(std::move(message));
    });

    client.fetchLibraryPage(std::nullopt, 1);
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    // First 429: retried silently.
    replies.front()->respond(kJsonErrorBody, 429, "1");
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 2, 6000);
    // Second 429: the retry is spent, so the reply is delivered to the route --
    // and the route must refuse the body rather than emit an empty page.
    replies.back()->respond(kJsonErrorBody, 429, "1");
    QTRY_VERIFY_WITH_TIMEOUT(!errors.empty(), 4000);

    QCOMPARE(fedPages, 0);
    QCOMPARE(errors.size(), std::size_t(1));
    QVERIFY(errors.front().find("rate limiting") != std::string::npos);
    QVERIFY(errors.front().find("429") != std::string::npos);
}

void TestSunoClient::aServerErrorOnGetIsRetriedOnce() {
    std::vector<FakeReply*> replies;
    auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                              const QByteArray&) {
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

    int callbackRuns = 0;
    client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "GET", {},
                                       [&callbackRuns](QNetworkReply*) { ++callbackRuns; });
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    replies.front()->respond({}, 503);

    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 2, 6000);
    replies.back()->respond({}, 503);
    QTRY_COMPARE_WITH_TIMEOUT(callbackRuns, 1, 4000);
    QTest::qWait(1500);
    QCOMPARE(replies.size(), std::size_t(2));
}

void TestSunoClient::aServerErrorOnPostIsNotRetried() {
    // A mutation passes retryOnUnauthorized=false, and that single flag is the
    // idempotency boundary: a 5xx must not double a non-idempotent POST.
    std::vector<FakeReply*> replies;
    auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                              const QByteArray&) {
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

    int callbackRuns = 0;
    std::vector<std::string> errors;
    client.errorOccurred.connect([&errors](std::string message) {
        errors.push_back(std::move(message));
    });
    client.enqueueAuthenticatedRequest(QStringLiteral("/session/"), "POST", QByteArray("{}"),
                                       [&callbackRuns](QNetworkReply*) { ++callbackRuns; },
                                       /*retryOnUnauthorized=*/false);
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    replies.front()->respond({}, 503);

    QTRY_COMPARE_WITH_TIMEOUT(callbackRuns, 1, 3000);
    QTest::qWait(1500);
    QCOMPARE(replies.size(), std::size_t(1));
    QCOMPARE(errors.size(), std::size_t(1));
    QVERIFY(errors.front().find("temporarily unavailable") != std::string::npos);
}

void TestSunoClient::aSearchTermIsSentAsFiltersSearchText() {
    std::vector<FakeReply*> replies;
    std::vector<QByteArray> requestBodies;
    auto factory = [&replies, &requestBodies](const QNetworkRequest& request,
                                              const std::string& method,
                                              const QByteArray& body) {
        requestBodies.push_back(body);
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());
    QVERIFY(client.isAuthenticated());

    int fedPages = 0;
    client.libraryFetched.connect([&fedPages](const auto&) { ++fedPages; });

    client.fetchLibraryPage(std::nullopt, 100, QStringLiteral("midnight"));
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    const QJsonObject request = QJsonDocument::fromJson(requestBodies[0]).object();
    QCOMPARE(request.value(QStringLiteral("limit")).toInt(), 100);
    QVERIFY(request.value(QStringLiteral("cursor")).isNull());
    const QJsonObject filters = request.value(QStringLiteral("filters")).toObject();
    QCOMPARE(filters.value(QStringLiteral("searchText")).toString(),
             QStringLiteral("midnight"));
    // The captured filter controls still ride along.
    QCOMPARE(filters.value(QStringLiteral("disliked")).toString(), QStringLiteral("False"));
    QCOMPARE(filters.value(QStringLiteral("trashed")).toString(), QStringLiteral("False"));

    const QJsonObject envelope{{QStringLiteral("clips"), QJsonArray{}},
                               {QStringLiteral("has_more"), false}};
    replies.front()->succeed(
        QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    QTRY_COMPARE_WITH_TIMEOUT(fedPages, 1, 3000);
}

void TestSunoClient::anEmptySearchTermOmitsSearchText() {
    std::vector<FakeReply*> replies;
    std::vector<QByteArray> requestBodies;
    auto factory = [&replies, &requestBodies](const QNetworkRequest& request,
                                              const std::string& method,
                                              const QByteArray& body) {
        requestBodies.push_back(body);
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
    SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
    QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
    client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

    const QJsonObject envelope{{QStringLiteral("clips"), QJsonArray{}},
                               {QStringLiteral("has_more"), false}};
    const QByteArray envelopeBytes =
        QJsonDocument(envelope).toJson(QJsonDocument::Compact);

    client.fetchLibraryPage(std::nullopt, 100);
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 1, 3000);
    QVERIFY(!QJsonDocument::fromJson(requestBodies[0])
                 .object()
                 .value(QStringLiteral("filters"))
                 .toObject()
                 .contains(QStringLiteral("searchText")));
    replies[0]->succeed(envelopeBytes);

    // A whitespace-only term is also unfiltered, not a literal search.
    client.fetchLibraryPage(std::nullopt, 100, QStringLiteral("   "));
    QTRY_VERIFY_WITH_TIMEOUT(replies.size() == 2, 5000);
    QVERIFY(!QJsonDocument::fromJson(requestBodies[1])
                 .object()
                 .value(QStringLiteral("filters"))
                 .toObject()
                 .contains(QStringLiteral("searchText")));
    replies[1]->succeed(envelopeBytes);
}

#include "test_SunoClient.moc"

int runTestSunoClient(int argc, char** argv) {
    TestSunoClient tc;
    return QTest::qExec(&tc, argc, argv);
}
