#include <QtTest>

#include "suno/auth/ClerkAuthClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <array>

using namespace vc::suno::auth;

namespace {

QString base64Url(const QJsonObject& object) {
    return QString::fromLatin1(
            QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                    QByteArray::Base64UrlEncoding |
                    QByteArray::OmitTrailingEquals));
}

QString sanitizedFakeJwt() {
    const QString header = base64Url({{"alg", "NONE"}, {"typ", "JWT"}});
    const QString payload = base64Url(
            {{"exp", static_cast<qint64>(4102444800)},
             {"test_canary", "JWT_PAYLOAD_CANARY"}});
    return header + "." + payload + ".JWT_SIGNATURE_CANARY";
}

/// The instance-key form of Clerk's own suffix, as it appears in the 2026-08-25
/// capture. It is a suffix Clerk chose for this instance, not a constant of the
/// protocol, so nothing under test may require it.
const QString kInstanceSuffix = QStringLiteral("Jnxw-muT");

struct ClassificationCase {
    QString name;
    QString value;
    StoredCredentialShape expectedShape;
    AuthFailureKind expectedFailureKind;
    QStringList secretCanaries;
};

std::array<ClassificationCase, 9> classificationCases() {
    const QString jwt = sanitizedFakeJwt();
    const QString bareCookiePair =
            QStringLiteral("Cookie: __client=COOKIE_VALUE_CANARY; "
                           "__client_uat=COOKIE_UAT_CANARY");
    const QString suffixedCookiePair =
            QStringLiteral("__client_%1=COOKIE_VALUE_CANARY; "
                           "__client_uat_%1=COOKIE_UAT_CANARY")
                    .arg(kInstanceSuffix);
    const QString sessionOnly = QStringLiteral("__session=") + jwt;
    const QString suffixedSessionOnly =
            QStringLiteral("__session_%1=%2").arg(kInstanceSuffix, jwt);

    return {{
            {"jwt bearer", jwt, StoredCredentialShape::BearerToken,
             AuthFailureKind::None,
             {"JWT_PAYLOAD_CANARY", "JWT_SIGNATURE_CANARY"}},
            {"clerk cookie", bareCookiePair, StoredCredentialShape::ClerkCookieHeader,
             AuthFailureKind::None,
             {"COOKIE_VALUE_CANARY", "COOKIE_UAT_CANARY"}},
            // Regression guard: Clerk suffixes the name it writes, so a jar that
            // carries only the instance-keyed pair is a valid credential and must
            // not be refused for failing an exact-name comparison.
            {"instance-suffixed clerk cookie pair", suffixedCookiePair,
             StoredCredentialShape::ClerkCookieHeader, AuthFailureKind::None,
             {"COOKIE_VALUE_CANARY", "COOKIE_UAT_CANARY"}},
            // __session is the access-token cookie: a jar holding only it is the
            // one a user gets from DevTools when they click the wrong row.
            {"session cookie only", sessionOnly,
             StoredCredentialShape::ClerkCookieHeader, AuthFailureKind::None,
             {"JWT_PAYLOAD_CANARY", "JWT_SIGNATURE_CANARY"}},
            {"instance-suffixed session cookie only", suffixedSessionOnly,
             StoredCredentialShape::ClerkCookieHeader, AuthFailureKind::None,
             {"JWT_PAYLOAD_CANARY", "JWT_SIGNATURE_CANARY"}},
            {"empty-suffix clerk cookie", QStringLiteral("__client=COOKIE_VALUE_CANARY"),
             StoredCredentialShape::ClerkCookieHeader, AuthFailureKind::None,
             {"COOKIE_VALUE_CANARY"}},
            {"empty-suffix clerk uat cookie",
             QStringLiteral("__client_uat=COOKIE_UAT_CANARY"),
             StoredCredentialShape::ClerkCookieHeader, AuthFailureKind::None,
             {"COOKIE_UAT_CANARY"}},
            {"non-clerk analytics jar",
             QStringLiteral("_ga=GA_CANARY; _gid=GID_CANARY"),
             StoredCredentialShape::Unsupported, AuthFailureKind::NoActiveSession,
             {"GA_CANARY", "GID_CANARY"}},
            {"garbage", QStringLiteral("GARBAGE_CREDENTIAL_CANARY"),
             StoredCredentialShape::Unsupported, AuthFailureKind::NoActiveSession,
             {"GARBAGE_CREDENTIAL_CANARY"}},
    }};
}

/// A jar of `count` non-Clerk cookies, used to drive the reporting cap.
QString analyticsJar(int count) {
    QString jar;
    for (int i = 1; i <= count; ++i) {
        jar += QStringLiteral("_name%1=VALUE%1_CANARY; ").arg(i);
    }
    return jar;
}

} // namespace

class TestStoredCredentialClassification : public QObject {
    Q_OBJECT

private slots:
    void classifiesCredentialShapes() {
        for (const auto& testCase : classificationCases()) {
            const auto classification = classifyStoredCredential(testCase.value);
            QVERIFY2(classification.shape == testCase.expectedShape,
                     qPrintable(QStringLiteral("%1: unexpected shape")
                                        .arg(testCase.name)));
            QVERIFY2(classification.failureKind == testCase.expectedFailureKind,
                     qPrintable(QStringLiteral("%1: unexpected failure kind")
                                        .arg(testCase.name)));
        }
    }

    void classifiesEmptyValue() {
        // Kept out of the shape table so every entry there is a non-empty value.
        const auto classification = classifyStoredCredential(QString());
        QCOMPARE(classification.shape, StoredCredentialShape::Empty);
        QCOMPARE(classification.failureKind, AuthFailureKind::None);
        QVERIFY(!classification.safeDiagnostic.isEmpty());
    }

    void diagnosticsContainNoCredentialMaterial() {
        for (const auto& testCase : classificationCases()) {
            const auto classification = classifyStoredCredential(testCase.value);
            QVERIFY2(!classification.safeDiagnostic.isEmpty(),
                     qPrintable(QStringLiteral("%1: empty safe diagnostic")
                                        .arg(testCase.name)));
            for (const QString& canary : testCase.secretCanaries) {
                QVERIFY2(!classification.safeDiagnostic.contains(canary),
                         qPrintable(QStringLiteral("%1: credential material leaked into diagnostic")
                                            .arg(testCase.name)));
            }
        }
    }

    /// A cookie *name* is not a secret, so the refusal for a non-Clerk jar has to
    /// name the names it saw. "You pasted the wrong cookie" and "you pasted no
    /// cookie header at all" are different user mistakes with different fixes.
    void wrongCookieDiagnosticNamesTheNamesItSaw() {
        const auto classification = classifyStoredCredential(
                QStringLiteral("_ga=GA_CANARY; _gid=GID_CANARY"));
        QCOMPARE(classification.shape, StoredCredentialShape::Unsupported);
        QCOMPARE(classification.failureKind, AuthFailureKind::NoActiveSession);
        QVERIFY2(classification.safeDiagnostic.contains(QStringLiteral("saw cookie name(s): "
                                                                 "_ga, _gid")),
                 qPrintable(classification.safeDiagnostic));
        // The expected set is restated so the message cannot drift back to
        // naming only __client / __client_uat.
        QVERIFY2(classification.safeDiagnostic.contains(QStringLiteral("__session")),
                 qPrintable(classification.safeDiagnostic));
        // Values are secrets and must never be quoted, only names.
        QVERIFY2(!classification.safeDiagnostic.contains(QStringLiteral("GA_CANARY")),
                 qPrintable(classification.safeDiagnostic));
        QVERIFY2(!classification.safeDiagnostic.contains(QStringLiteral("GID_CANARY")),
                 qPrintable(classification.safeDiagnostic));
    }

    void wrongCookieDiagnosticIsDistinctFromNoCookieAtAll() {
        const auto wrongCookie = classifyStoredCredential(
                QStringLiteral("_ga=GA_CANARY"));
        const auto noCookie = classifyStoredCredential(
                QStringLiteral("GARBAGE_CREDENTIAL_CANARY"));

        QVERIFY2(wrongCookie.safeDiagnostic.contains(
                         QStringLiteral("saw cookie name(s)")),
                 qPrintable(wrongCookie.safeDiagnostic));
        QVERIFY2(!wrongCookie.safeDiagnostic.contains(
                          QStringLiteral("unrecognized non-cookie value")),
                 qPrintable(wrongCookie.safeDiagnostic));

        QVERIFY2(noCookie.safeDiagnostic.contains(
                         QStringLiteral("unrecognized non-cookie value")),
                 qPrintable(noCookie.safeDiagnostic));
        QVERIFY2(!noCookie.safeDiagnostic.contains(
                          QStringLiteral("saw cookie name(s)")),
                 qPrintable(noCookie.safeDiagnostic));

        // Both refusals still name the whole expected set, so a user who pasted
        // nothing at all is told what to go and get.
        for (const auto& classification : {wrongCookie, noCookie}) {
            for (const QString& family : {QStringLiteral("__client"),
                                          QStringLiteral("__client_uat"),
                                          QStringLiteral("__session")}) {
                QVERIFY2(classification.safeDiagnostic.contains(family),
                         qPrintable(classification.safeDiagnostic));
            }
        }
    }

    /// A name arrives from a user paste, so it is untrusted text: a name outside
    /// the conventional cookie-name character set, and the overflow past the
    /// reporting cap, are counted instead of reproduced.
    void withholdsUntrustedCookieNameText() {
        const auto untrusted = classifyStoredCredential(
                QStringLiteral("_ga=GA_CANARY; not a cookie name=WEIRD_NAME_CANARY"));
        QVERIFY2(untrusted.safeDiagnostic.contains(
                         QStringLiteral("saw cookie name(s): _ga")),
                 qPrintable(untrusted.safeDiagnostic));
        QVERIFY2(!untrusted.safeDiagnostic.contains(
                          QStringLiteral("not a cookie name")),
                 qPrintable(untrusted.safeDiagnostic));
        QVERIFY2(untrusted.safeDiagnostic.contains(
                         QStringLiteral("1 further name(s) withheld")),
                 qPrintable(untrusted.safeDiagnostic));

        const auto overflow = classifyStoredCredential(analyticsJar(10));
        QCOMPARE(overflow.shape, StoredCredentialShape::Unsupported);
        QVERIFY2(overflow.safeDiagnostic.contains(QStringLiteral("_name1, _name2, _name3, "
                                                                "_name4, _name5, _name6, "
                                                                "_name7, _name8")),
                 qPrintable(overflow.safeDiagnostic));
        QVERIFY2(!overflow.safeDiagnostic.contains(QStringLiteral("_name9")),
                 qPrintable(overflow.safeDiagnostic));
        QVERIFY2(overflow.safeDiagnostic.contains(QStringLiteral("2 further name(s) withheld")),
                 qPrintable(overflow.safeDiagnostic));
        for (int i = 1; i <= 10; ++i) {
            QVERIFY2(!overflow.safeDiagnostic.contains(
                             QStringLiteral("VALUE%1_CANARY").arg(i)),
                     qPrintable(overflow.safeDiagnostic));
        }
    }

    /// A name that merely *starts* with a Clerk family is that family -- that is
    /// the whole point of instance suffixes -- and a name that merely contains
    /// one is not.
    void matchesOnlyAtTheStartOfTheName() {
        for (const QString& value :
             {QStringLiteral("__client_uat_=COOKIE_UAT_CANARY"),
              QStringLiteral("__client_=COOKIE_VALUE_CANARY"),
              QStringLiteral("__session_=JWT_PAYLOAD_CANARY")}) {
            const auto classification = classifyStoredCredential(value);
            QVERIFY2(classification.shape == StoredCredentialShape::ClerkCookieHeader,
                     qPrintable(QStringLiteral("%1: unexpected shape for %2")
                                        .arg(classification.safeDiagnostic, value)));
            QCOMPARE(classification.failureKind, AuthFailureKind::None);
        }

        for (const QString& value :
             {QStringLiteral("x__client=COOKIE_VALUE_CANARY"),
              QStringLiteral("not__session=JWT_PAYLOAD_CANARY")}) {
            const auto classification = classifyStoredCredential(value);
            QVERIFY2(classification.shape == StoredCredentialShape::Unsupported,
                     qPrintable(QStringLiteral("%1: unexpected shape for %2")
                                        .arg(classification.safeDiagnostic, value)));
        }
    }

    void acceptsSuffixedFormsRegardlessOfWhichOneIsPresent() {
        // A jar may carry any subset; one matching cookie is enough, and the
        // __client prefix must not swallow the __client_uat decision.
        const QString uat = QStringLiteral("__client_uat_%1=0").arg(kInstanceSuffix);
        const QString refresh =
                QStringLiteral("__client_%1=COOKIE_VALUE_CANARY").arg(kInstanceSuffix);
        const QString mixed = QStringLiteral("_ga=GA_CANARY; __session_%1=COOKIE_VALUE_CANARY")
                                      .arg(kInstanceSuffix);
        for (const QString& value : {uat, refresh, mixed}) {
            const auto classification = classifyStoredCredential(value);
            QVERIFY2(classification.shape == StoredCredentialShape::ClerkCookieHeader,
                     qPrintable(QStringLiteral("%1: unexpected shape for %2")
                                        .arg(classification.safeDiagnostic, value)));
            QCOMPARE(classification.failureKind, AuthFailureKind::None);
        }
    }
};

int runTestStoredCredentialClassification(int argc, char** argv) {
    TestStoredCredentialClassification test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_StoredCredentialClassification.moc"
