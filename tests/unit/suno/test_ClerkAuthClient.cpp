#include <QtTest>
#include "suno/auth/ClerkAuthClient.hpp"
#include "suno/auth/JwtUtils.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

using namespace vc::suno::auth;

namespace vc::suno::auth {

/// Non-public test seam: exercises the same synchronous body handler used by
/// GET and touch without adding parser methods to the production API.
class ClerkAuthClientTestAccess {
public:
    static void processEnvelope(ClerkAuthClient& client, const QByteArray& body) {
        client.handleEnvelopeBody(body, ClerkAuthClient::CallContext{});
    }

    static QString lastSessionId(const ClerkAuthClient& client) {
        return client.lastKnownSessionId_;
    }

    static AuthFailureKind classifyHttpFailure(int status) {
        return ClerkAuthClient::classifyHttpFailure(status);
    }

    static QString httpFailureReason(int status) {
        return ClerkAuthClient::httpFailureReason(status);
    }
};

} // namespace vc::suno::auth

namespace {

QString b64url(const QJsonObject& object) {
    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact)
                                       .toBase64(QByteArray::Base64UrlEncoding |
                                                 QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(encoded);
}

/// Sanitized, unsigned fixture token. No captured or real credential is used.
QString fakeJwt(const QString& marker, qint64 expiryEpochSecs) {
    const QString header = b64url({{"alg", "NONE"}, {"typ", "JWT"}});
    const QString payload = b64url({{"exp", expiryEpochSecs}, {"test_marker", marker}});
    return header + "." + payload + ".ZmFrZQ";
}

QByteArray jsonBody(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject token(const QString& jwt) {
    return QJsonObject{{"jwt", jwt}};
}

QJsonObject session(const QString& id, const QString& jwt) {
    QJsonObject object{{"last_active_token", token(jwt)}};
    if (!id.isEmpty()) {
        object["id"] = id;
    }
    return object;
}

struct ParseOutcome {
    int bearerCount = 0;
    int failureCount = 0;
    BearerToken bearer;
    QString error;
    QString sessionId;
    AuthFailureKind failureKind = AuthFailureKind::None;
};

ParseOutcome parseBody(const QByteArray& body) {
    ClerkAuthClient client;
    ParseOutcome outcome;
    QObject::connect(&client, &ClerkAuthClient::bearerReady, &client,
                     [&outcome](const BearerToken& bearer) {
                         ++outcome.bearerCount;
                         outcome.bearer = bearer;
                     });
    QObject::connect(&client, &ClerkAuthClient::authFailed, &client,
                     [&outcome](const QString& reason) {
                         ++outcome.failureCount;
                         outcome.error = reason;
                     });

    ClerkAuthClientTestAccess::processEnvelope(client, body);
    outcome.sessionId = ClerkAuthClientTestAccess::lastSessionId(client);
    outcome.failureKind = client.failureKind();
    return outcome;
}

void verifySuccess(const ParseOutcome& outcome, const QString& marker, qint64 expiry) {
    QCOMPARE(outcome.bearerCount, 1);
    QCOMPARE(outcome.failureCount, 0);
    QCOMPARE(outcome.failureKind, AuthFailureKind::None);
    QVERIFY(outcome.error.isEmpty());

    const auto claims = JwtUtils::claims(outcome.bearer.jwt);
    QVERIFY(claims.has_value());
    QCOMPARE(claims->value("test_marker").toString(), marker);
    QCOMPARE(JwtUtils::expiryEpochSecs(*claims), expiry);
    QCOMPARE(outcome.bearer.expiresAt.toTimeZone(QTimeZone::UTC).toSecsSinceEpoch(), expiry);
}

} // namespace

class TestClerkAuthClient : public QObject {
    Q_OBJECT

private slots:
    void getStyleEnvelopeParses() {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("get-session"), expiry);
        const QByteArray body = jsonBody({
                {"response", QJsonObject{
                        {"id", "client_get"},
                        {"last_active_session_id", "sess_get"},
                        {"sessions", QJsonArray{session("sess_get", jwt)}},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("get-session"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_get"));
    }

    void touchStyleResponseTokenParsesRegression() {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString responseJwt = fakeJwt(QStringLiteral("touch-response"), expiry);
        const QString clientJwt = fakeJwt(QStringLiteral("touch-client"), expiry);
        const QByteArray body = jsonBody({
                {"response", QJsonObject{
                        {"last_active_session_id", "sess_touch"},
                        {"last_active_token", token(responseJwt)},
                }},
                {"client", QJsonObject{
                        {"sessions", QJsonArray{session("sess_touch", clientJwt)}},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("touch-response"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_touch"));
    }

    void touchStyleClientTokenParses() {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("touch-client-only"), expiry);
        const QByteArray body = jsonBody({
                {"client", QJsonObject{
                        {"sessions", QJsonArray{session("sess_client", jwt)}},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("touch-client-only"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_client"));
    }

    void tokenLocationsUseDocumentedPrecedence() {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QByteArray body = jsonBody({
                {"response", QJsonObject{
                        {"last_active_session_id", "sess_primary"},
                        {"last_active_token",
                         token(fakeJwt(QStringLiteral("response-direct"), expiry))},
                        {"sessions", QJsonArray{
                                session("sess_primary",
                                        fakeJwt(QStringLiteral("response-session"), expiry)),
                        }},
                }},
                {"client", QJsonObject{
                        {"sessions", QJsonArray{
                                session("sess_client",
                                        fakeJwt(QStringLiteral("client-session"), expiry)),
                        }},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("response-session"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_primary"));
    }

    void responseSessionSelectorSuppliesMissingId() {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QByteArray body = jsonBody({
                {"response", QJsonObject{
                        {"last_active_session_id", "sess_from_selector"},
                        {"sessions", QJsonArray{
                                session(QString(), fakeJwt(QStringLiteral("selected"), expiry)),
                        }},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("selected"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_from_selector"));
    }

    void noActiveSession_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty-response") << QByteArray(R"({"response":{}})");
        QTest::newRow("empty-response-sessions")
                << QByteArray(R"({"response":{"sessions":[]}})");
        QTest::newRow("all-session-arrays-empty")
                << QByteArray(
                        R"({"response":{"sessions":[]},"client":{"sessions":[]}})");
    }

    void noActiveSession() {
        QFETCH(QByteArray, body);
        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::NoActiveSession);
        QCOMPARE(outcome.error,
                 QStringLiteral("no active session; stored credential is incomplete or signed out"));
        QVERIFY(!outcome.error.contains(QStringLiteral("expired"), Qt::CaseInsensitive));
    }

    void malformedResponse_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("malformed-json") << QByteArray(R"({"response":)");
        QTest::newRow("array") << QByteArray(R"([{"response":{}}])");
        QTest::newRow("scalar") << QByteArray("42");
        QTest::newRow("null") << QByteArray("null");
        QTest::newRow("no-envelope") << QByteArray(R"({"unexpected":true})");
    }

    void malformedResponse() {
        QFETCH(QByteArray, body);
        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::MalformedResponse);
        QCOMPARE(outcome.error, QStringLiteral("unexpected Clerk response shape"));
    }

    void expiredAlternateTokenReportsExpiry() {
        const QString jwt = fakeJwt(QStringLiteral("expired"),
                                    QDateTime::currentSecsSinceEpoch() - 60);
        const QByteArray body = jsonBody({
                {"response", QJsonObject{
                        {"last_active_session_id", "sess_expired"},
                        {"last_active_token", token(jwt)},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::ProtocolMismatch);
        QCOMPARE(outcome.error, QStringLiteral("Clerk returned an expired bearer token"));
        QVERIFY(!outcome.error.contains(QStringLiteral("no active session")));
        QVERIFY(!outcome.error.contains(jwt));
    }

    void malformedTokenReportsUndecodable() {
        const QString jwt = QStringLiteral("not-a-jwt-CANARY");
        const QByteArray body = jsonBody({
                {"client", QJsonObject{
                        {"sessions", QJsonArray{session("sess_bad", jwt)}},
                }},
        });

        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.failureKind, AuthFailureKind::ProtocolMismatch);
        QCOMPARE(outcome.error,
                 QStringLiteral("Clerk returned a bearer token that could not be decoded"));
        QVERIFY(!outcome.error.contains(jwt));
    }

    void httpRejectionIsClassifiedWithoutResponseData() {
        for (const int status : {401, 403}) {
            QCOMPARE(ClerkAuthClientTestAccess::classifyHttpFailure(status),
                     AuthFailureKind::RejectedCredential);
            QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(status),
                     QStringLiteral("stored credential was rejected by Clerk"));
        }
        QCOMPARE(ClerkAuthClientTestAccess::classifyHttpFailure(500),
                 AuthFailureKind::ProtocolMismatch);
        QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(500),
                 QStringLiteral("Clerk request failed with an unexpected HTTP status"));
    }

    void errorStringsNeverContainSecrets() {
        const QString cookieCanary = QStringLiteral("__client=COOKIE_CANARY");
        const QString queryCanary = QStringLiteral("state=QUERY_CANARY");
        const QByteArray body = jsonBody({
                {"unexpected", true},
                {"cookie", cookieCanary},
                {"query", queryCanary},
        });

        const ParseOutcome outcome = parseBody(body);
        QVERIFY(!outcome.error.contains(cookieCanary));
        QVERIFY(!outcome.error.contains(queryCanary));
        QVERIFY(!ClerkAuthClientTestAccess::httpFailureReason(401).contains(cookieCanary));
        QVERIFY(!ClerkAuthClientTestAccess::httpFailureReason(401).contains(queryCanary));
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestClerkAuthClient test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ClerkAuthClient.moc"
