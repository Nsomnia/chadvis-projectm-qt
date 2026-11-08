#include "suno/auth/AuthHeaders.hpp"
#include "suno/auth/ClerkAuthClient.hpp"
#include "suno/auth/JwtUtils.hpp"
#include <QtTest>

#include <QSignalSpy>

#include <array>
#include <utility>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QUrl>

using namespace vc::suno::auth;

namespace vc::suno::auth {

/// Non-public test seam: exercises the same synchronous body handler used by
/// GET and touch without adding parser methods to the production API.
class ClerkAuthClientTestAccess
{
public:
    static void processEnvelope(ClerkAuthClient& client, const QByteArray& body)
    {
        client.handleEnvelopeBody(body, ClerkAuthClient::CallContext{});
    }

    static void processTokensBody(ClerkAuthClient& client, const QByteArray& body,
                                  const QString& sessionId)
    {
        client.handleTokensBody(
            body, ClerkAuthClient::CallContext{Credentials{}, sessionId,
                                                ClerkAuthClient::CallContext::Kind::Tokens});
    }

    static QNetworkRequest makeRequest(const QUrl& url, const Credentials& credentials,
                                       bool isPost)
    {
        return ClerkAuthClient::makeRequest(url, credentials, isPost);
    }

    static QString lastObservedSessionId(const ClerkAuthClient& client)
    {
        return client.lastObservedSessionId_;
    }

    static AuthFailureKind classifyHttpFailure(int status)
    {
        return ClerkAuthClient::classifyHttpFailure(status);
    }

    static QString httpFailureReason(int status)
    {
        return ClerkAuthClient::httpFailureReason(status);
    }

    static QString failureReason(int status, const QByteArray& body,
                                 const QByteArray& retryAfterHeader)
    {
        return ClerkAuthClient::failureReason(status, body, retryAfterHeader);
    }
};

} // namespace vc::suno::auth

namespace {

QString b64url(const QJsonObject& object)
{
    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(encoded);
}

/// Sanitized, unsigned fixture token. No captured or real credential is used.
/// `sid` reproduces the standard Clerk session claim; empty omits it, which is
/// the tolerance branch T0340 keeps.
QString fakeJwt(const QString& marker, qint64 expiryEpochSecs, const QString& sid = {})
{
    const QString header = b64url({{"alg", "NONE"}, {"typ", "JWT"}});
    QJsonObject payload{{"exp", expiryEpochSecs}, {"test_marker", marker}};
    if (!sid.isEmpty())
    {
        payload["sid"] = sid;
    }
    const QString payloadB64 = b64url(payload);
    return header + "." + payloadB64 + ".ZmFrZQ";
}

QByteArray jsonBody(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

/// Sanitized Clerk failure envelope: `{"errors":[{"code":...,"long_message":...}]}`.
QByteArray clerkErrorBody(const QString& code, const QString& longMessage = {})
{
    QJsonObject error{{"code", code}, {"message", QStringLiteral("raw short message")}};
    if (!longMessage.isEmpty())
    {
        error["long_message"] = longMessage;
    }
    return jsonBody({{"errors", QJsonArray{error}}});
}

QJsonObject token(const QString& jwt)
{
    return QJsonObject{{"jwt", jwt}};
}

QJsonObject session(const QString& id, const QString& jwt)
{
    QJsonObject object{{"last_active_token", token(jwt)}};
    if (!id.isEmpty())
    {
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
    CredentialExpiry expiry;
    AuthFailureKind failureKind = AuthFailureKind::None;
};

ParseOutcome parseBody(const QByteArray& body)
{
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
    outcome.sessionId = ClerkAuthClientTestAccess::lastObservedSessionId(client);
    outcome.expiry = client.lastCredentialExpiry();
    outcome.failureKind = client.failureKind();
    return outcome;
}

/// The tokens-body counterpart of parseBody: feeds a `{ "jwt": ... }` body
/// through the tokens parser and reports what it emitted.
ParseOutcome parseTokensBody(const QByteArray& body, const QString& sessionId)
{
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

    ClerkAuthClientTestAccess::processTokensBody(client, body, sessionId);
    outcome.sessionId = ClerkAuthClientTestAccess::lastObservedSessionId(client);
    outcome.failureKind = client.failureKind();
    return outcome;
}

void verifySuccess(const ParseOutcome& outcome, const QString& marker, qint64 expiry)
{
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

/// Epoch-milliseconds for a date `days` (and optional `extraSecs`) after
/// `base`, as the captures carry it: numbers for `cookie_expires_at`,
/// strings for session `abandon_at` / `expire_at` (TOUCH-TOKENS-SHAPES.md).
qint64 epochMs(const QDateTime& base, qint64 days, qint64 extraSecs = 0)
{
    return (base.addDays(days).addSecs(extraSecs).toSecsSinceEpoch()) * 1000;
}

QString advisoryTextFor(const QDateTime& dateTime)
{
    return QStringLiteral("Re-import your Suno session before %1.")
            .arg(QLocale(QLocale::English).toString(dateTime, QStringLiteral("d MMMM yyyy")));
}

/// Session object carrying token plus epoch-ms lifetime values, the touch
/// capture shape. Malformed values are passed through as-is so the parser
/// itself decides what is trustworthy.
QJsonObject sessionWithLifetime(const QString& id, const QString& jwt,
                                  const QJsonValue& abandonAt, const QJsonValue& expireAt)
{
    QJsonObject object{{"last_active_token", token(jwt)}};
    if (!id.isEmpty())
    {
        object["id"] = id;
    }
    object["abandon_at"] = abandonAt;
    object["expire_at"] = expireAt;
    return object;
}

} // namespace

class TestClerkAuthClient : public QObject
{
    Q_OBJECT

private slots:
    /// The bridge no longer owns this vocabulary; these are the strings it
    /// passes to QML, so a rename or an empty message is a user-visible bug.
    void authFailureVocabularyIsComplete()
    {
        const std::array<std::pair<AuthFailureKind, const char*>, 5> kinds{{
                {AuthFailureKind::None, "none"},
                {AuthFailureKind::NoActiveSession, "noSession"},
                {AuthFailureKind::RejectedCredential, "rejected"},
                {AuthFailureKind::ProtocolMismatch, "protocol"},
                {AuthFailureKind::MalformedResponse, "malformed"},
        }};
        for (const auto& [kind, id] : kinds) {
            QCOMPARE(QString::fromLatin1(authFailureKindId(kind)), QString::fromLatin1(id));
            const QString message = QString::fromLatin1(authFailureMessage(kind));
            if (kind == AuthFailureKind::None) {
                QVERIFY(message.isEmpty());
            } else {
                QVERIFY(!message.isEmpty());
            }
        }
    }

    void getStyleEnvelopeParses()
    {
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

    void touchStyleResponseTokenParsesRegression()
    {
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
        verifySuccess(outcome, QStringLiteral("touch-client"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_touch"));
    }

    void touchStyleClientTokenParses()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("touch-client-only"), expiry);
        const QByteArray body = jsonBody({
            {"client", QJsonObject{
                           {"last_active_session_id", "sess_client"},
                           {"sessions", QJsonArray{session("sess_client", jwt)}},
                       }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("touch-client-only"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_client"));
    }

    void authRequestNormalizesCookieAndUsesCapturedHeaders()
    {
        const QUrl url(QStringLiteral("https://auth.suno.com/v1/client"));
        const Credentials credentials{
            QStringLiteral("  cOoKiE:  __client=OPAQUE==; __session=Keep-Case  "),
        };
        const QNetworkRequest get =
            ClerkAuthClientTestAccess::makeRequest(url, credentials, false);
        const QNetworkRequest post =
            ClerkAuthClientTestAccess::makeRequest(url, credentials, true);

        const QByteArray expectedCookie("__client=OPAQUE==; __session=Keep-Case");
        QCOMPARE(get.rawHeader("Cookie"), expectedCookie);
        QCOMPARE(post.rawHeader("Cookie"), expectedCookie);
        QVERIFY(!get.rawHeader("Cookie").contains("Cookie:"));
        QCOMPARE(get.rawHeader("Accept"), QByteArray("*/*"));
        QCOMPARE(post.rawHeader("Accept"), QByteArray("*/*"));
        QVERIFY(get.rawHeader("Content-Type").isEmpty());
        QCOMPARE(post.rawHeader("Content-Type"),
                 QByteArray("application/x-www-form-urlencoded"));
        QCOMPARE(get.rawHeader("User-Agent"), QByteArray(kBrowserUserAgent));
        QVERIFY(get.attribute(QNetworkRequest::RedirectPolicyAttribute)
                    .value<QNetworkRequest::RedirectPolicy>() ==
                QNetworkRequest::ManualRedirectPolicy);
    }

    void preferredSessionRequiresExactSelector()
    {
        ClerkClientInfo info;
        info.sessions = {{QStringLiteral("first"), {}},
                         {QStringLiteral("selected"), {}}};
        QVERIFY(!info.preferredSession());

        info.lastActiveSessionId = QStringLiteral("selected");
        QVERIFY(info.preferredSession());
        QCOMPARE(info.preferredSession()->sessionId, QStringLiteral("selected"));

        info.lastActiveSessionId = QStringLiteral("missing");
        QVERIFY(!info.preferredSession());
    }

    void emptyCredentialFailsBeforeNetworkRequest()
    {
        ClerkAuthClient client;
        QSignalSpy failures(&client, &ClerkAuthClient::authFailed);
        client.fetchBearer(Credentials{});

        QCOMPARE(failures.size(), 1);
        QCOMPARE(client.failureKind(), AuthFailureKind::NoActiveSession);
    }

    void tokenLocationsUseDocumentedPrecedence()
    {
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
                                            session("sess_client", fakeJwt(QStringLiteral("client-session"), expiry)),
                                        }},
                       }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("response-session"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_primary"));
    }

    void responseSessionRequiresMatchingId()
    {
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
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::NoActiveSession);
    }

    /// A selector is still required whenever the envelope is ambiguous: two or
    /// more sessions with no selector, a selector that matches no session, or a
    /// direct token with no session at all (T0342 keeps the multi-session
    /// refusal; only the single-session case gained a fallback).
    void sessionSelectorRequired_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("two-sessions-without-selector")
            << QByteArray(R"({"response":{"sessions":[{"id":"a","last_active_token":{"jwt":"token-a"}},{"id":"b","last_active_token":{"jwt":"token-b"}}]}})");
        QTest::newRow("three-sessions-without-selector")
            << QByteArray(R"({"response":{"sessions":[{"id":"a","last_active_token":{"jwt":"token-a"}},{"id":"b","last_active_token":{"jwt":"token-b"}},{"id":"c","last_active_token":{"jwt":"token-c"}}]}})");
        QTest::newRow("split-halves-name-two-sessions")
            << QByteArray(R"({"response":{"sessions":[{"id":"a","last_active_token":{"jwt":"token-a"}}]},"client":{"sessions":[{"id":"b","last_active_token":{"jwt":"token-b"}}]}})");
        QTest::newRow("unmatched-selector")
            << QByteArray(R"({"response":{"last_active_session_id":"b","sessions":[{"id":"a","last_active_token":{"jwt":"token-a"}}]}})");
        QTest::newRow("direct-token-is-not-envelope")
            << QByteArray(R"({"response":{"last_active_session_id":"a","last_active_token":{"jwt":"token-a"}}})");
    }

    void sessionSelectorRequired()
    {
        QFETCH(QByteArray, body);
        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::NoActiveSession);
    }

    /// T0342: one session and no `last_active_session_id` is not a refusal.
    /// The web client reads `last_active_session_id ?? sessions?.[0]?.id`, so
    /// the sole session is the answer whether it arrives in the `response`
    /// half or the `client` half.
    void singleSessionWithoutSelectorFallsBack()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("sole-session"), expiry);
        const QJsonArray sessions{session(QStringLiteral("sess_only"), jwt)};

        const std::array<QByteArray, 2> bodies{
            jsonBody({{"response", QJsonObject{{"sessions", sessions}}}}),
            jsonBody({{"client", QJsonObject{{"sessions", sessions}}}}),
        };
        for (const QByteArray& body : bodies)
        {
            const ParseOutcome outcome = parseBody(body);
            verifySuccess(outcome, QStringLiteral("sole-session"), expiry);
            QCOMPARE(outcome.sessionId, QStringLiteral("sess_only"));
        }
    }

    /// The fallback must not swallow an id-less session: `sessions[0]?.id` is
    /// undefined there, so there is no session to report.
    void idlessSessionWithoutSelectorStillRefuses()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QByteArray body = jsonBody({
            {"response", QJsonObject{
                             {"sessions", QJsonArray{
                                              session(QString(), fakeJwt(QStringLiteral("idless"), expiry)),
                                          }},
                         }},
        });

        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureKind, AuthFailureKind::NoActiveSession);
    }

    void noActiveSession_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty-response") << QByteArray(R"({"response":{}})");
        QTest::newRow("empty-response-sessions")
            << QByteArray(R"({"response":{"sessions":[]}})");
        QTest::newRow("all-session-arrays-empty")
            << QByteArray(
                   R"({"response":{"sessions":[]},"client":{"sessions":[]}})");
    }

    void noActiveSession()
    {
        QFETCH(QByteArray, body);
        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::NoActiveSession);
        QCOMPARE(outcome.error,
                 QStringLiteral("no active session; stored credential is incomplete or signed out"));
        QVERIFY(!outcome.error.contains(QStringLiteral("expired"), Qt::CaseInsensitive));
    }

    void malformedResponse_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("malformed-json") << QByteArray(R"({"response":)");
        QTest::newRow("array") << QByteArray(R"([{"response":{}}])");
        QTest::newRow("scalar") << QByteArray("42");
        QTest::newRow("null") << QByteArray("null");
        QTest::newRow("no-envelope") << QByteArray(R"({"unexpected":true})");
    }

    void malformedResponse()
    {
        QFETCH(QByteArray, body);
        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::MalformedResponse);
        QCOMPARE(outcome.error, QStringLiteral("unexpected Clerk response shape"));
    }

    void expiredAlternateTokenReportsExpiry()
    {
        const QString jwt = fakeJwt(QStringLiteral("expired"),
                                    QDateTime::currentSecsSinceEpoch() - 60);
        const QByteArray body = jsonBody({
            {"response", QJsonObject{
                             {"last_active_session_id", "sess_expired"},
                             {"sessions", QJsonArray{session("sess_expired", jwt)}},
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

    void malformedTokenReportsUndecodable()
    {
        const QString jwt = QStringLiteral("not-a-jwt-CANARY");
        const QByteArray body = jsonBody({
            {"client", QJsonObject{
                           {"last_active_session_id", "sess_bad"},
                           {"sessions", QJsonArray{session("sess_bad", jwt)}},
                       }},
        });

        const ParseOutcome outcome = parseBody(body);
        QCOMPARE(outcome.failureKind, AuthFailureKind::ProtocolMismatch);
        QCOMPARE(outcome.error,
                 QStringLiteral("Clerk returned a bearer token that could not be decoded"));
        QVERIFY(!outcome.error.contains(jwt));
    }

    void httpRejectionIsClassifiedWithoutResponseData()
    {
        for (const int status : {401, 403})
        {
            QCOMPARE(ClerkAuthClientTestAccess::classifyHttpFailure(status),
                     AuthFailureKind::RejectedCredential);
            QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(status),
                     QStringLiteral("stored credential was rejected by Clerk"));
        }
        QCOMPARE(ClerkAuthClientTestAccess::classifyHttpFailure(500),
                 AuthFailureKind::ProtocolMismatch);
        // A rate limit and an origin outage get a sentence that names the wait,
        // not "unexpected HTTP status" -- which read as a client bug (T0216).
        QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(429),
                 QStringLiteral("Clerk is rate limiting sign-in requests (HTTP 429); try again "
                                "shortly"));
        QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(430),
                 QStringLiteral("Clerk is rate limiting sign-in requests (HTTP 430); try again "
                                "shortly"));
        QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(500),
                 QStringLiteral("Clerk is temporarily unavailable (HTTP 500); try again shortly"));
        QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(503),
                 QStringLiteral("Clerk is temporarily unavailable (HTTP 503); try again shortly"));
        QCOMPARE(ClerkAuthClientTestAccess::httpFailureReason(400),
                 QStringLiteral("Clerk request failed with an unexpected HTTP status"));
    }

    /// T0341: a bounded `errors[0].code` map becomes a fixed sentence. The raw
    /// `long_message` must never appear in it, because Clerk writes that field
    /// for a human reading the response and it can quote the caller's own
    /// identifier back at us.
    void clerkErrorCodeMapsToFixedSentence_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("sentence");

        QTest::newRow("session") << QStringLiteral("session_not_found")
                                 << QStringLiteral("Clerk no longer recognises the stored "
                                                   "sign-in session; sign in again");
        QTest::newRow("client") << QStringLiteral("client_not_found")
                                << QStringLiteral("Clerk no longer recognises this client; "
                                                  "restart the app and sign in again");
        QTest::newRow("resource") << QStringLiteral("resource_not_found")
                                  << QStringLiteral("Clerk could not find the stored sign-in "
                                                    "resource; sign in again");
        QTest::newRow("identifier") << QStringLiteral("form_identifier_not_found")
                                    << QStringLiteral("Clerk does not recognise the stored "
                                                      "account identifier; sign in again");
        QTest::newRow("password") << QStringLiteral("form_password_incorrect")
                                  << QStringLiteral("stored credential was rejected by Clerk; "
                                                    "sign in again");
        QTest::newRow("verification") << QStringLiteral("verification_failed")
                                      << QStringLiteral("Clerk could not verify the stored "
                                                        "credential; sign in again");
        QTest::newRow("captcha-required") << QStringLiteral("requires_captcha")
                                          << QStringLiteral("Clerk requires an interactive "
                                                            "sign-in before it will accept this "
                                                            "account");
        QTest::newRow("captcha-missing") << QStringLiteral("captcha_missing_token")
                                         << QStringLiteral("Clerk requires an interactive "
                                                           "sign-in before it will accept this "
                                                           "account");
    }

    void clerkErrorCodeMapsToFixedSentence()
    {
        QFETCH(QString, code);
        QFETCH(QString, sentence);

        const QString longMessageCanary = QStringLiteral("user_CANARY_IDENTIFIER is not allowed");
        const QByteArray body = clerkErrorBody(code, longMessageCanary);
        const QString reason = ClerkAuthClientTestAccess::failureReason(404, body, {});

        QCOMPARE(reason, sentence);
        QVERIFY(!reason.contains(QStringLiteral("user_CANARY_IDENTIFIER")));
        QVERIFY(!reason.contains(QStringLiteral("raw short message")));
    }

    /// A code outside the table is not a reason to invent a sentence: the
    /// status-based sentence stands, and the body stays unpublished.
    void unknownClerkErrorCodeFallsBackToStatusSentence()
    {
        const QByteArray body = clerkErrorBody(QStringLiteral("some_future_code"),
                                               QStringLiteral("internal detail"));
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(400, body, {}),
                 QStringLiteral("Clerk request failed with an unexpected HTTP status"));
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(500, body, {}),
                 QStringLiteral("Clerk is temporarily unavailable (HTTP 500); try again shortly"));
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(401, body, {}),
                 QStringLiteral("stored credential was rejected by Clerk"));
    }

    /// Absent, empty, malformed, or codeless envelopes must not turn a failure
    /// into an empty message.
    void missingClerkErrorCodeFallsBackToStatusSentence()
    {
        const QString expected =
                QStringLiteral("Clerk request failed with an unexpected HTTP status");

        QCOMPARE(ClerkAuthClientTestAccess::failureReason(400, {}, {}), expected);
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(400, QByteArray("not json"), {}),
                 expected);
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(
                         400, jsonBody({{"unexpected", true}}), {}),
                 expected);
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(
                         400, jsonBody({{"errors", QJsonArray{}}}), {}),
                 expected);
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(
                         400, jsonBody({{"errors", QJsonArray{QJsonObject{}}}}), {}),
                 expected);
    }

    /// T0341: a 429 says how long the wait is when the origin supplies
    /// `Retry-After`, and keeps the existing sentence when it does not.
    void rateLimitRetryAfterIsSurfaced_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<QString>("sentence");

        const QString shortly =
                QStringLiteral("Clerk is rate limiting sign-in requests (HTTP 429); try again "
                               "shortly");

        QTest::newRow("seconds") << QByteArray("42")
                                 << QStringLiteral("Clerk is rate limiting sign-in requests "
                                                   "(HTTP 429); try again in 42 seconds");
        QTest::newRow("singular") << QByteArray("1")
                                  << QStringLiteral("Clerk is rate limiting sign-in requests "
                                                    "(HTTP 429); try again in 1 second");
        QTest::newRow("padded") << QByteArray(" 7 ")
                                << QStringLiteral("Clerk is rate limiting sign-in requests "
                                                  "(HTTP 429); try again in 7 seconds");
        // A zero or an elapsed HTTP-date means "now"; the floor keeps the
        // sentence from reading "in 0 seconds".
        QTest::newRow("zero") << QByteArray("0")
                              << QStringLiteral("Clerk is rate limiting sign-in requests "
                                                "(HTTP 429); try again in 1 second");
        QTest::newRow("past-date") << QByteArray("Sun, 06 Nov 1994 08:49:37 GMT")
                                   << QStringLiteral("Clerk is rate limiting sign-in requests "
                                                     "(HTTP 429); try again in 1 second");
        QTest::newRow("capped") << QByteArray("999999")
                                << QStringLiteral("Clerk is rate limiting sign-in requests "
                                                  "(HTTP 429); try again in 86400 seconds");
        QTest::newRow("missing") << QByteArray() << shortly;
        QTest::newRow("garbage") << QByteArray("soon") << shortly;
        QTest::newRow("negative") << QByteArray("-5") << shortly;
        QTest::newRow("fractional") << QByteArray("2.5") << shortly;
        QTest::newRow("overflow") << QByteArray("9223372036854775808") << shortly;
    }

    void rateLimitRetryAfterIsSurfaced()
    {
        QFETCH(QByteArray, header);
        QFETCH(QString, sentence);

        QCOMPARE(ClerkAuthClientTestAccess::failureReason(429, {}, header), sentence);
    }

    /// `Retry-After` is the rate limiter's instruction, not a general status
    /// hint: a 503 with the same header keeps its own sentence, and a mapped
    /// code still wins over the generic status text.
    void retryAfterDoesNotLeakIntoOtherStatuses()
    {
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(503, {}, QByteArray("30")),
                 QStringLiteral("Clerk is temporarily unavailable (HTTP 503); try again shortly"));
        QCOMPARE(ClerkAuthClientTestAccess::failureReason(
                         400, clerkErrorBody(QStringLiteral("session_not_found")),
                         QByteArray("30")),
                 QStringLiteral("Clerk no longer recognises the stored sign-in session; sign "
                                "in again"));
    }

    void errorStringsNeverContainSecrets()
    {
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

    /// The tokens route's `{ "jwt": ... }` body parses to a bearer, and the
    /// observed session is the one the call asked about — the body names no
    /// session itself, and the caller (SunoClient's touch fallback) needs
    /// one to name.
    void tokensBodyParses()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("tokens-session"), expiry);
        const QByteArray body = jsonBody({{"jwt", jwt}});

        const ParseOutcome outcome = parseTokensBody(body, QStringLiteral("sess_tokens"));
        verifySuccess(outcome, QStringLiteral("tokens-session"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_tokens"));
    }

    /// T0340: the minted bearer must belong to the session the request named.
    /// A "sid" claim that says otherwise is a protocol mismatch -- not a
    /// credential verdict, and the error must not quote either identifier.
    void tokensBodyWithMismatchedSidClaimFails()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("tokens-wrong-session"), expiry,
                                    QStringLiteral("sess_other"));
        const QByteArray body = jsonBody({{"jwt", jwt}});

        const ParseOutcome outcome = parseTokensBody(body, QStringLiteral("sess_tokens"));
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::ProtocolMismatch);
        QCOMPARE(outcome.error,
                 QStringLiteral("Clerk minted a bearer token for a session other than the one "
                                "requested"));
        QVERIFY(!outcome.error.contains(jwt));
        QVERIFY(!outcome.error.contains(QStringLiteral("sess_other")));
        QVERIFY(!outcome.error.contains(QStringLiteral("sess_tokens")));
        QVERIFY(outcome.sessionId.isEmpty());
    }

    /// A "sid" claim that agrees with the request is accepted, and the claim is
    /// what gets recorded as the observed session.
    void tokensBodyWithMatchingSidClaimSucceeds()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("tokens-bound"), expiry,
                                    QStringLiteral("sess_tokens"));
        const QByteArray body = jsonBody({{"jwt", jwt}});

        const ParseOutcome outcome = parseTokensBody(body, QStringLiteral("sess_tokens"));
        verifySuccess(outcome, QStringLiteral("tokens-bound"), expiry);
        QCOMPARE(outcome.sessionId, QStringLiteral("sess_tokens"));
    }

    /// T0340's other half: an absent "sid" stays tolerated, but it must not
    /// wipe a session an earlier envelope already named.
    void tokensBodyWithoutSidClaimKeepsObservedSession()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        ClerkAuthClient client;
        QSignalSpy bearers(&client, &ClerkAuthClient::bearerReady);
        QSignalSpy failures(&client, &ClerkAuthClient::authFailed);

        const QByteArray envelope = jsonBody({
            {"response", QJsonObject{
                             {"last_active_session_id", "sess_envelope"},
                             {"sessions", QJsonArray{
                                              session(QStringLiteral("sess_envelope"),
                                                      fakeJwt(QStringLiteral("envelope"), expiry)),
                                          }},
                         }},
        });
        ClerkAuthClientTestAccess::processEnvelope(client, envelope);
        QCOMPARE(ClerkAuthClientTestAccess::lastObservedSessionId(client),
                 QStringLiteral("sess_envelope"));

        const QByteArray body =
            jsonBody(token(fakeJwt(QStringLiteral("tokens-no-sid"), expiry)));
        ClerkAuthClientTestAccess::processTokensBody(client, body,
                                                     QStringLiteral("sess_tokens"));

        QCOMPARE(failures.count(), 0);
        QCOMPARE(bearers.count(), 2);
        QCOMPARE(client.failureKind(), AuthFailureKind::None);
        QCOMPARE(ClerkAuthClientTestAccess::lastObservedSessionId(client),
                 QStringLiteral("sess_envelope"));
    }

    /// A tokens body without a usable `jwt` is a shape error, not a bearer:
    /// missing, non-string, and empty all fail the same way, and none of
    /// them may be mistaken for an envelope (which would fail differently).
    void tokensBodyWithoutJwtIsMalformed()
    {
        for (const QByteArray& body :
             {jsonBody({}), jsonBody({{"jwt", 123}}), jsonBody({{"jwt", QString()}}),
              jsonBody({{"response", QJsonObject{}}})}) {
            const ParseOutcome outcome =
                parseTokensBody(body, QStringLiteral("sess_tokens"));
            QCOMPARE(outcome.bearerCount, 0);
            QCOMPARE(outcome.failureCount, 1);
            QCOMPARE(outcome.failureKind, AuthFailureKind::MalformedResponse);
            QCOMPARE(outcome.error, QStringLiteral("unexpected Clerk response shape"));
        }
    }

    /// A tokens body is parsed by member name: future additive members must
    /// not turn a working mint into a shape error.
    void tokensBodyIgnoresExtraMembers()
    {
        const qint64 expiry = QDateTime::currentSecsSinceEpoch() + 3600;
        const QString jwt = fakeJwt(QStringLiteral("tokens-extra"), expiry);
        const QByteArray body =
            jsonBody({{"jwt", jwt}, {"object", "session_token"}, {"future", true}});

        const ParseOutcome outcome = parseTokensBody(body, QStringLiteral("sess_tokens"));
        verifySuccess(outcome, QStringLiteral("tokens-extra"), expiry);
    }

    /// An expired tokens JWT is rejected with the same expiry failure as an
    /// expired envelope token, and the JWT value never appears in the error.
    void tokensBodyWithExpiredJwtReportsExpiry()
    {
        const QString jwt = fakeJwt(QStringLiteral("tokens-expired"),
                                    QDateTime::currentSecsSinceEpoch() - 60);
        const QByteArray body = jsonBody({{"jwt", jwt}});

        const ParseOutcome outcome = parseTokensBody(body, QStringLiteral("sess_tokens"));
        QCOMPARE(outcome.bearerCount, 0);
        QCOMPARE(outcome.failureCount, 1);
        QCOMPARE(outcome.failureKind, AuthFailureKind::ProtocolMismatch);
        QVERIFY(!outcome.error.contains(jwt));
    }

    /// The tokens entry point validates before any network request, the way
    /// touch() does: no session id is a protocol error, no credential is
    /// a missing-session error, and neither emits a bearer.
    void tokensRequestRequiresSessionIdAndCredential()
    {
        ClerkAuthClient client;
        QSignalSpy failures(&client, &ClerkAuthClient::authFailed);
        QSignalSpy bearers(&client, &ClerkAuthClient::bearerReady);

        const Credentials creds{QStringLiteral("__client=x")};
        client.mintBearerViaTokens(creds, QStringLiteral(""));
        QCOMPARE(failures.count(), 1);
        QCOMPARE(client.failureKind(), AuthFailureKind::ProtocolMismatch);

        client.mintBearerViaTokens(Credentials{}, QStringLiteral("sess_1"));
        QCOMPARE(failures.count(), 2);
        QCOMPARE(client.failureKind(), AuthFailureKind::NoActiveSession);

        QCOMPARE(bearers.count(), 0);
    }

    /// T0344: with all three envelope dates present the advisory names the
    /// earliest. Session dates use the touch capture's string form;
    /// `cookie_expires_at` is the numeric form at envelope root.
    void credentialExpiryEarliestDateWins()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 jwtExpiry = now.addSecs(3600).toSecsSinceEpoch();
        const QString jwt = fakeJwt(QStringLiteral("expiry-all"), jwtExpiry);

        const QDateTime cookieDate = now.addDays(60);
        const QDateTime abandonDate = now.addDays(20);
        const QDateTime expireDate = now.addDays(10);
        const QByteArray body = jsonBody({
            {"response", QJsonObject{
                             {"last_active_session_id", "sess_expiry"},
                             {"sessions",
                              QJsonArray{sessionWithLifetime(
                                  QStringLiteral("sess_expiry"), jwt,
                                  QJsonValue(QString::number(epochMs(now, 20))),
                                  QJsonValue(QString::number(epochMs(now, 10))))}},
                         }},
            {"cookie_expires_at", epochMs(now, 60)},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("expiry-all"), jwtExpiry);
        QVERIFY(outcome.expiry.cookieExpiresAt.has_value());
        QVERIFY(outcome.expiry.sessionAbandonAt.has_value());
        QVERIFY(outcome.expiry.sessionExpiresAt.has_value());
        QCOMPARE(outcome.expiry.cookieExpiresAt->toSecsSinceEpoch(),
                 cookieDate.toSecsSinceEpoch());
        QCOMPARE(outcome.expiry.sessionAbandonAt->toSecsSinceEpoch(),
                 abandonDate.toSecsSinceEpoch());
        QCOMPARE(outcome.expiry.sessionExpiresAt->toSecsSinceEpoch(),
                 expireDate.toSecsSinceEpoch());

        const auto advisory = reimportAdvisory(outcome.expiry, now);
        QVERIFY(advisory.has_value());
        QCOMPARE(*advisory, advisoryTextFor(expireDate));
    }

    /// T0344: only the cookie date is present and near, so it is the date the
    /// advisory names.
    void credentialExpiryCookieOnly()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 jwtExpiry = now.addSecs(3600).toSecsSinceEpoch();
        const QString jwt = fakeJwt(QStringLiteral("expiry-cookie"), jwtExpiry);
        const QDateTime cookieDate = now.addDays(5);
        const QByteArray body = jsonBody({
            {"response", QJsonObject{
                             {"last_active_session_id", "sess_cookie"},
                             {"sessions",
                              QJsonArray{session(QStringLiteral("sess_cookie"), jwt)}},
                         }},
            {"cookie_expires_at", epochMs(now, 5)},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("expiry-cookie"), jwtExpiry);
        QVERIFY(outcome.expiry.cookieExpiresAt.has_value());
        QVERIFY(!outcome.expiry.sessionAbandonAt.has_value());
        QVERIFY(!outcome.expiry.sessionExpiresAt.has_value());
        const auto advisory = reimportAdvisory(outcome.expiry, now);
        QVERIFY(advisory.has_value());
        QCOMPARE(*advisory, advisoryTextFor(cookieDate));
    }

    /// T0344: no dates anywhere in the envelope -- the credential is still
    /// accepted and there is nothing to advise about.
    void credentialExpiryAbsent()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 jwtExpiry = now.addSecs(3600).toSecsSinceEpoch();
        const QString jwt = fakeJwt(QStringLiteral("expiry-none"), jwtExpiry);
        const QByteArray body = jsonBody({
            {"response", QJsonObject{
                             {"last_active_session_id", "sess_nodate"},
                             {"sessions",
                              QJsonArray{session(QStringLiteral("sess_nodate"), jwt)}},
                         }},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("expiry-none"), jwtExpiry);
        QVERIFY(!outcome.expiry.cookieExpiresAt.has_value());
        QVERIFY(!outcome.expiry.sessionAbandonAt.has_value());
        QVERIFY(!outcome.expiry.sessionExpiresAt.has_value());
        QVERIFY(!reimportAdvisory(outcome.expiry, now).has_value());
    }

    /// T0344: malformed dates are ignored, never fabricated and never a
    /// reason to reject an otherwise valid credential.
    void credentialExpiryMalformedIsIgnored()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 jwtExpiry = now.addSecs(3600).toSecsSinceEpoch();
        const QString jwt = fakeJwt(QStringLiteral("expiry-malformed"), jwtExpiry);
        const QByteArray body = jsonBody({
            {"response", QJsonObject{
                             {"last_active_session_id", "sess_bad_dates"},
                             {"sessions",
                              QJsonArray{sessionWithLifetime(
                                  QStringLiteral("sess_bad_dates"), jwt,
                                  QJsonValue(QStringLiteral("not-a-date")),
                                  QJsonValue(QStringLiteral("123.5")))}},
                         }},
            {"cookie_expires_at", QJsonValue(QStringLiteral("soon"))},
        });

        const ParseOutcome outcome = parseBody(body);
        verifySuccess(outcome, QStringLiteral("expiry-malformed"), jwtExpiry);
        QVERIFY(!outcome.expiry.cookieExpiresAt.has_value());
        QVERIFY(!outcome.expiry.sessionAbandonAt.has_value());
        QVERIFY(!outcome.expiry.sessionExpiresAt.has_value());
        QVERIFY(!reimportAdvisory(outcome.expiry, now).has_value());
    }

    /// T0344: the 30-day window boundary on the pure helper with an injected
    /// `now`: exactly 30 days out warns, one second beyond it does not.
    void reimportAdvisoryThresholdBoundary()
    {
        const QDateTime now = QDateTime(QDate(2026, 3, 3), QTime(12, 0, 0), QTimeZone::utc());

        CredentialExpiry atBoundary;
        atBoundary.sessionExpiresAt = now.addDays(30);
        const auto atBoundaryAdvisory = reimportAdvisory(atBoundary, now);
        QVERIFY(atBoundaryAdvisory.has_value());
        QCOMPARE(*atBoundaryAdvisory, advisoryTextFor(now.addDays(30)));

        CredentialExpiry beyond;
        beyond.sessionExpiresAt = now.addDays(30).addSecs(1);
        QVERIFY(!reimportAdvisory(beyond, now).has_value());

        CredentialExpiry pastDue;
        pastDue.cookieExpiresAt = now.addDays(-1);
        const auto pastDueAdvisory = reimportAdvisory(pastDue, now);
        QVERIFY(pastDueAdvisory.has_value());
        QCOMPARE(*pastDueAdvisory, advisoryTextFor(now.addDays(-1)));

        CredentialExpiry unknown;
        QVERIFY(!reimportAdvisory(unknown, now).has_value());
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestClerkAuthClient test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ClerkAuthClient.moc"
