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

struct ClassificationCase {
    QString name;
    QString value;
    StoredCredentialShape expectedShape;
    AuthFailureKind expectedFailureKind;
    QStringList secretCanaries;
};

std::array<ClassificationCase, 5> classificationCases() {
    const QString jwt = sanitizedFakeJwt();
    const QString cookie =
            QStringLiteral("Cookie: __client=COOKIE_VALUE_CANARY; "
                           "__client_uat=COOKIE_UAT_CANARY");
    const QString sessionOnly =
            QStringLiteral("__session=") + jwt;

    return {{
            {"jwt bearer", jwt, StoredCredentialShape::BearerToken,
             AuthFailureKind::None,
             {"JWT_PAYLOAD_CANARY", "JWT_SIGNATURE_CANARY"}},
            {"clerk cookie", cookie, StoredCredentialShape::ClerkCookieHeader,
             AuthFailureKind::None,
             {"COOKIE_VALUE_CANARY", "COOKIE_UAT_CANARY"}},
            {"session cookie without clerk client", sessionOnly,
             StoredCredentialShape::Unsupported, AuthFailureKind::NoActiveSession,
             {"JWT_PAYLOAD_CANARY", "JWT_SIGNATURE_CANARY"}},
            {"garbage", QStringLiteral("GARBAGE_CREDENTIAL_CANARY"),
             StoredCredentialShape::Unsupported, AuthFailureKind::NoActiveSession,
             {"GARBAGE_CREDENTIAL_CANARY"}},
            {"empty", QString(), StoredCredentialShape::Empty,
             AuthFailureKind::None, {}},
    }};
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
};

int runTestStoredCredentialClassification(int argc, char** argv) {
    TestStoredCredentialClassification test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_StoredCredentialClassification.moc"
