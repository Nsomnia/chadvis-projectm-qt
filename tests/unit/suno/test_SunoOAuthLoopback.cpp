#include <QtTest>

#include "suno/auth/SunoOAuthLoopback.hpp"

#include <QJsonObject>
#include <QUrlQuery>

using namespace vc::suno::auth;

/// The OAuth surface is mostly protocol text, so most of it can be pinned
/// without a socket or a network. The three behaviours that must never drift:
/// the PKCE pair (an RFC 7636 known-answer), the authorize URL's parameter set
/// (the one the MCP flow actually sends), and the callback parser's refusal to
/// accept anything but a GET/HEAD request line for /callback.
class TestSunoOAuthLoopback : public QObject
{
    Q_OBJECT

private slots:
    void pkceMatchesRfc7636Vector()
    {
        // RFC 7636 Appendix B, verbatim. If this drifts, every real server
        // rejects our challenge and the symptom is a generic 400.
        const QString verifier =
                QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
        QCOMPARE(SunoOAuthLoopback::pkceChallenge(verifier),
                 QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void verifierUsesUnreservedCharsetAndLength()
    {
        const QString verifier = SunoOAuthLoopback::makeCodeVerifier();
        QVERIFY(verifier.size() >= 43);
        QVERIFY(verifier.size() <= 128);
        static const QString allowed =
                QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
        for (const QChar ch : verifier) {
            QVERIFY2(allowed.contains(ch), qPrintable(QStringLiteral("bad char: %1").arg(ch)));
        }
        // Freshness: two runs must not share a verifier.
        QVERIFY(SunoOAuthLoopback::makeCodeVerifier() != verifier);
    }

    void authorizeUrlCarriesEveryObservedParameter()
    {
        SunoOAuthLoopback::ClientConfig config;
        const QUrl url = SunoOAuthLoopback::buildAuthorizeUrl(
                QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/v2/external/oauth/authorize/")),
                config, QStringLiteral("client-123"),
                QStringLiteral("http://127.0.0.1:41234/callback"),
                QStringLiteral("state-abc"), QStringLiteral("challenge-xyz"));

        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("response_type"), QUrl::FullyDecoded),
                 QStringLiteral("code"));
        QCOMPARE(query.queryItemValue(QStringLiteral("client_id"), QUrl::FullyDecoded),
                 QStringLiteral("client-123"));
        QCOMPARE(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded),
                 QStringLiteral("http://127.0.0.1:41234/callback"));
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge"), QUrl::FullyDecoded),
                 QStringLiteral("challenge-xyz"));
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method"), QUrl::FullyDecoded),
                 QStringLiteral("S256"));
        QCOMPARE(query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded),
                 QStringLiteral("state-abc"));
        QCOMPARE(query.queryItemValue(QStringLiteral("resource"), QUrl::FullyDecoded),
                 QStringLiteral("https://api.suno.com/mcp"));
        // The scope is the eight-scope MCP set from the protected-resource metadata.
        QVERIFY(query.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded)
                        .contains(QStringLiteral("library:write")));
        // The endpoint path survives: nothing rewrote the authorize route.
        QCOMPARE(url.path(),
                 QStringLiteral("/api/v2/external/oauth/authorize/"));
    }

    void parseRequestTargetAcceptsGetAndHead()
    {
        const auto get = SunoOAuthLoopback::parseRequestTarget(
                QByteArrayLiteral("GET /callback?code=abc&state=def HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
        QVERIFY(get.has_value());
        QCOMPARE(*get, QByteArrayLiteral("/callback?code=abc&state=def"));

        const auto head = SunoOAuthLoopback::parseRequestTarget(
                QByteArrayLiteral("HEAD /callback?code=abc HTTP/1.1\r\n\r\n"));
        QVERIFY(head.has_value());
        // HEAD has no query here; the target itself is still the path.
        QCOMPARE(*head, QByteArrayLiteral("/callback?code=abc"));
    }

    void parseRequestTargetRejectsEverythingElse()
    {
        // A POST to the loopback is not a browser callback.
        QVERIFY(!SunoOAuthLoopback::parseRequestTarget(
                QByteArrayLiteral("POST /callback HTTP/1.1\r\n\r\n")));
        // Garbage, an empty buffer, and an absolute-form target are all refused.
        QVERIFY(!SunoOAuthLoopback::parseRequestTarget(QByteArrayLiteral("garbage")));
        QVERIFY(!SunoOAuthLoopback::parseRequestTarget(QByteArray{}));
        QVERIFY(!SunoOAuthLoopback::parseRequestTarget(
                QByteArrayLiteral("GET https://evil.example/callback HTTP/1.1\r\n\r\n")));
    }

    void queryValueDecodesAndDistinguishesAbsentFromEmpty()
    {
        const QByteArray query = QByteArrayLiteral("code=a%2Fb%2Bc&state=x&flag&empty=");
        QCOMPARE(SunoOAuthLoopback::queryValue(query, "code").value(),
                 QStringLiteral("a/b+c"));
        QCOMPARE(SunoOAuthLoopback::queryValue(query, "state").value(), QStringLiteral("x"));
        // A bare key is present with an empty value, not absent.
        QVERIFY(SunoOAuthLoopback::queryValue(query, "flag").has_value());
        QVERIFY(SunoOAuthLoopback::queryValue(query, "flag")->isEmpty());
        QVERIFY(SunoOAuthLoopback::queryValue(query, "empty").has_value());
        QVERIFY(!SunoOAuthLoopback::queryValue(query, "missing").has_value());
    }

    void tokenSetEmptinessIsTheAccessToken()
    {
        SunoOAuthLoopback::TokenSet tokens;
        QVERIFY(tokens.isEmpty());
        tokens.refreshToken = QStringLiteral("r");
        QVERIFY(tokens.isEmpty()); // refresh alone is not a usable bearer
        tokens.accessToken = QStringLiteral("a");
        QVERIFY(!tokens.isEmpty());
    }
};

QTEST_MAIN(TestSunoOAuthLoopback)
#include "test_SunoOAuthLoopback.moc"
