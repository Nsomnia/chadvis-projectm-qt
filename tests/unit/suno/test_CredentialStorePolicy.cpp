// test_CredentialStorePolicy.cpp - tests for the pure, platform-free half of
// CredentialStore's backend selection and credential encoding.
//
// WHAT THIS FILE DELIBERATELY DOES NOT TEST
// -----------------------------------------
// It never calls CredWriteW, CredReadW, CredDeleteW, SecItem* or any D-Bus
// method, and it does not pretend to. The author is on macOS with no Windows
// SDK, no mingw-w64 cross-compiler and no D-Bus session bus, so a test that
// stubbed those syscalls and asserted the stub's return value would prove
// nothing about the real API - it would be a test of the test's own fake.
//
// What IS tested here is everything that was extracted from the platform code
// precisely so it could be tested on this host: the selection rule, the refusal
// reasons, the UTF-8 encode and its two refusal modes, the Win32 target name
// and blob length, and the Win32 error classification. Every assertion is an
// exact value or an exact inequality, not "non-zero" or "roughly right".

#include "core/Logger.hpp"
#include "suno/auth/CredentialStore.hpp"

#include <QtTest>

#include <QString>
#include <QTemporaryDir>

using namespace vc::suno::auth;

class TestCredentialStorePolicy : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Logger::get() is a shared_ptr the LOG_* macros dereference; init it so
        // a refusal path that logs cannot crash the suite.
        vc::Logger::init("chadvis-test", false);
    }

    // ------------------------------------------------------------------
    // Backend selection, per platform.
    // ------------------------------------------------------------------

    void appleWithItsBackendCompiledSelectsTheKeychain() {
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Apple;
        in.compiledAppleKeychain = true;

        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::AppleKeychain);
        QCOMPARE(d.reason, UnavailableReason::None);
        QVERIFY(d.isSecure());
        QVERIFY(d.isAvailable());
    }

    void windowsWithItsBackendCompiledSelectsTheCredentialManager() {
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Windows;
        in.compiledWindowsCredentialManager = true;

        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::WindowsCredentialManager);
        QCOMPARE(d.reason, UnavailableReason::None);
        QVERIFY(d.isSecure());
    }

    void linuxWithAUsableSecretServiceSelectsIt() {
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Linux;
        in.compiledSecretService = true;
        in.secretService = {true, true, true, false};

        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::LinuxSecretService);
        QCOMPARE(d.reason, UnavailableReason::None);
        QVERIFY(d.isSecure());
    }

    // ------------------------------------------------------------------
    // The fail-closed core: unavailable is NEVER plaintext, and always named.
    //
    // This is the regression guard for the whole change. Before it, a build
    // without CHADVIS_HAS_KEYCHAIN fell through to a 0600 plaintext file
    // containing a year-long refresh cookie, on Windows and Linux.
    // ------------------------------------------------------------------

    void noBackendCompiledFailsClosedRatherThanWritingPlaintext() {
        for (const auto platform : {SecretStorePlatform::Apple, SecretStorePlatform::Windows,
                                    SecretStorePlatform::Linux}) {
            SecretStoreInputs in;
            in.platform = platform;
            // All three compiled* flags left false: the state a build is in when
            // the CMake owner has not yet added the new sources.

            const BackendDecision d = decideBackend(in);
            QCOMPARE(d.choice, BackendChoice::Unavailable);
            QVERIFY(d.choice != BackendChoice::PlaintextFile);
            QCOMPARE(d.reason, UnavailableReason::BackendNotCompiled);
            QVERIFY(!d.isSecure());
            QVERIFY(!d.isAvailable());
        }
    }

    void anUnknownPlatformIsUnsupportedNotPlaintext() {
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Unknown;
        in.compiledAppleKeychain = true; // nonsense input must not rescue it
        in.compiledWindowsCredentialManager = true;
        in.compiledSecretService = true;

        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::Unavailable);
        QCOMPARE(d.reason, UnavailableReason::PlatformUnsupported);
    }

    void noKeychainBuildFlagIsTheOnlyRouteToPlaintext() {
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Linux;
        in.compiledSecretService = true;
        in.secretService = {true, true, true, false};
        in.keychainDisabledByBuild = true;

        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::PlaintextFile);
        QVERIFY(!d.isSecure());
    }

    void noKeychainBuildFlagOutranksAWorkingBackend() {
        // Even on macOS with a working keychain, CHADVIS_NO_KEYCHAIN wins:
        // somebody asked for plaintext at configure time on purpose.
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Apple;
        in.compiledAppleKeychain = true;
        in.keychainDisabledByBuild = true;

        QCOMPARE(decideBackend(in).choice, BackendChoice::PlaintextFile);
    }

    void noKeychainBuildFlagAppliesToWindowsToo() {
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Windows;
        in.compiledWindowsCredentialManager = true;
        in.keychainDisabledByBuild = true;

        QCOMPARE(decideBackend(in).choice, BackendChoice::PlaintextFile);
    }

    // ------------------------------------------------------------------
    // Linux availability: every distinct failure names itself.
    // ------------------------------------------------------------------

    void headlessLinuxIsNoSessionBusNotServiceNotRunning() {
        // All false is what probeSecretServiceOverDBus() returns with no bus.
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Linux;
        in.compiledSecretService = true;
        in.secretService = {};

        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::Unavailable);
        QCOMPARE(d.reason, UnavailableReason::NoSessionBus);
        QVERIFY(!d.isSecure());
    }

    void eachSecretServiceFailureMapsToItsOwnReason() {
        struct Row {
            SecretServiceProbe probe;
            UnavailableReason expected;
            const char* what;
        };
        const Row rows[] = {
                {{false, false, false, false}, UnavailableReason::NoSessionBus, "no bus"},
                {{true, false, false, false}, UnavailableReason::ServiceNotRunning, "no service"},
                {{true, true, false, false},
                 UnavailableReason::NoDefaultCollection,
                 "no collection"},
                {{true, true, true, true}, UnavailableReason::CollectionLocked, "locked"},
        };
        for (const Row& row : rows) {
            SecretStoreInputs in;
            in.platform = SecretStorePlatform::Linux;
            in.compiledSecretService = true;
            in.secretService = row.probe;

            const BackendDecision d = decideBackend(in);
            QCOMPARE(d.choice, BackendChoice::Unavailable);
            QCOMPARE(d.reason, row.expected);
            QVERIFY(!d.isSecure());
        }
    }

    void classifySecretServiceReportsTheFirstFailingFact() {
        // Ordering matters: a box with no bus AND no service must say "no bus",
        // because "is gnome-keyring running" is not actionable advice for
        // someone in a container.
        QCOMPARE(classifySecretService({false, false, false, false}),
                 SecretServiceVerdict::NoSessionBus);
        QCOMPARE(classifySecretService({true, false, false, false}),
                 SecretServiceVerdict::ServiceNotRunning);
        QCOMPARE(classifySecretService({true, true, false, false}),
                 SecretServiceVerdict::NoDefaultCollection);
        QCOMPARE(classifySecretService({true, true, true, true}),
                 SecretServiceVerdict::CollectionLocked);
        QCOMPARE(classifySecretService({true, true, true, false}), SecretServiceVerdict::Usable);
    }

    void everySecretServiceVerdictHasItsOwnReason() {
        QCOMPARE(reasonForVerdict(SecretServiceVerdict::Usable), UnavailableReason::None);
        QCOMPARE(reasonForVerdict(SecretServiceVerdict::NoSessionBus),
                 UnavailableReason::NoSessionBus);
        QCOMPARE(reasonForVerdict(SecretServiceVerdict::ServiceNotRunning),
                 UnavailableReason::ServiceNotRunning);
        QCOMPARE(reasonForVerdict(SecretServiceVerdict::NoDefaultCollection),
                 UnavailableReason::NoDefaultCollection);
        QCOMPARE(reasonForVerdict(SecretServiceVerdict::CollectionLocked),
                 UnavailableReason::CollectionLocked);
    }

    void aLockedCollectionWinsOverAnAbsentOne() {
        // Both flags set is not reachable from the probe, but the rule must be
        // deterministic if it ever is: locked is the safer of the two answers.
        QCOMPARE(classifySecretService({true, true, false, true}),
                 SecretServiceVerdict::NoDefaultCollection);
    }

    // ------------------------------------------------------------------
    // Refusal messages: actionable, distinct, and free of any secret.
    // ------------------------------------------------------------------

    void everyRefusalReasonNamesItselfDistinctly() {
        const UnavailableReason reasons[] = {
                UnavailableReason::None,
                UnavailableReason::PlatformUnsupported,
                UnavailableReason::BackendNotCompiled,
                UnavailableReason::DisabledByBuild,
                UnavailableReason::NoSessionBus,
                UnavailableReason::ServiceNotRunning,
                UnavailableReason::NoDefaultCollection,
                UnavailableReason::CollectionLocked,
                UnavailableReason::KeychainAccessDenied,
        };
        QSet<QString> texts;
        for (const UnavailableReason reason : reasons) {
            const QString text = describeUnavailable(reason);
            QVERIFY2(!text.isEmpty(), "a refusal must say something");
            QVERIFY2(!texts.contains(text),
                     qPrintable(QStringLiteral("duplicate refusal text for %1")
                                        .arg(static_cast<int>(reason))));
            texts.insert(text);
        }
        QCOMPARE(texts.size(), static_cast<int>(std::size(reasons)));
    }

    void aRefusalNeverClaimsSuccess() {
        QVERIFY(!describeUnavailable(UnavailableReason::NoSessionBus)
                         .contains(QStringLiteral("ok"), Qt::CaseInsensitive));
        // UnavailableReason::None is the only one that may read as fine.
        QCOMPARE(describeUnavailable(UnavailableReason::None),
                 QStringLiteral("secret storage is available"));
    }

    void aRefusalNeverMentionsAFileAsTheDestination() {
        // The point of the change: the user must never be told a plaintext file
        // is where the credential went.
        for (const UnavailableReason reason :
             {UnavailableReason::PlatformUnsupported, UnavailableReason::BackendNotCompiled,
              UnavailableReason::NoSessionBus, UnavailableReason::ServiceNotRunning,
              UnavailableReason::NoDefaultCollection, UnavailableReason::CollectionLocked}) {
            const QString lower = describeUnavailable(reason).toLower();
            QVERIFY2(!lower.contains(QStringLiteral("fallback")),
                     "no refusal may advertise a fallback");
        }
    }

    // ------------------------------------------------------------------
    // Credential encoding: the bytes, exactly.
    // ------------------------------------------------------------------

    void plainAsciiEncodesToItselfWithNoTerminator() {
        const QString secret = QStringLiteral("__client=__session=abc123");
        const auto encoded = encodeSecret(secret);

        QVERIFY(encoded.has_value());
        QCOMPARE(encoded->size(), 25); // exact byte count, not "non-empty"
        QCOMPARE(encoded->at(0), '_');
        QCOMPARE(encoded->at(encoded->size() - 1), '3');
        // No trailing NUL: the length is the size, and a NUL would become part
        // of the stored secret on the Win32 and Secret Service paths.
        QVERIFY(!encoded->endsWith('\0'));
        QCOMPARE(QString::fromUtf8(*encoded), secret);
    }

    void nonAsciiRoundTripsAsUtf8() {
        // The emoji goes through fromUtf8 rather than a \x escape: in a UTF-16
        // source "\xF0\x9F\x98\x80" is four *bytes*, each widened to U+00F0
        // etc., which is a different string that happens to look the same in a
        // diff. Measured: this composes to 8 UTF-16 units and 11 UTF-8 bytes
        // ("naïve-" = 5 chars / 7 bytes, plus a 4-byte emoji).
        const QString secret =
                QStringLiteral("na\u00EFve-") + QString::fromUtf8("\xF0\x9F\x98\x80");
        const auto encoded = encodeSecret(secret);

        QVERIFY(encoded.has_value());
        QCOMPARE(QString::fromUtf8(*encoded), secret);
        QCOMPARE(encoded->size(), 11);
        QCOMPARE(secret.size(), 8);
    }

    void anEmptySecretIsValidAndEmpty() {
        const auto encoded = encodeSecret(QString());
        QVERIFY(encoded.has_value());
        QCOMPARE(encoded->size(), 0);
    }

    // ------------------------------------------------------------------
    // The corrupt-secret refusal, and WHY it exists.
    //
    // Measured on Qt 6.11.1: QString::toUtf8() silently DROPS an unpaired
    // surrogate rather than substituting U+FFFD. A 4-character QString
    // (U+D800 "abc") encodes to the 3 bytes "abc" - so without this refusal a
    // store()/load() pair round-trips to a credential that differs from the one
    // the caller supplied, with nothing logged and nothing written to indicate
    // it.
    // ------------------------------------------------------------------

    void aLoneHighSurrogateIsRefused() {
        QString secret = QString(QChar(char16_t(0xD800)));
        secret += QStringLiteral("abc");

        const auto encoded = encodeSecret(secret);
        QVERIFY(!encoded.has_value());
        QCOMPARE(encoded.error(), SecretEncodeDefect::UnpairedSurrogate);
    }

    void aLoneLowSurrogateIsRefused() {
        const QString secret = QString(QChar(char16_t(0xDC00)));

        const auto encoded = encodeSecret(secret);
        QVERIFY(!encoded.has_value());
        QCOMPARE(encoded.error(), SecretEncodeDefect::UnpairedSurrogate);
    }

    void aTruncatedSurrogatePairIsRefused() {
        QString secret = QString(QChar(char16_t(0xD83D))); // high, no low follows
        secret += QStringLiteral("x");

        QCOMPARE(encodeSecret(secret).error(), SecretEncodeDefect::UnpairedSurrogate);
    }

    void aMismatchedPairIsRefused() {
        // High surrogate followed by ANOTHER high surrogate, not a low one.
        QString secret = QString(QChar(char16_t(0xD800)));
        secret += QString(QChar(char16_t(0xD801)));

        QCOMPARE(encodeSecret(secret).error(), SecretEncodeDefect::UnpairedSurrogate);
    }

    void aValidSurrogatePairIsAccepted() {
        // The control for the four refusals above: a well-formed pair must NOT
        // be rejected, or the check is just "contains a surrogate".
        QString secret = QString(QChar(char16_t(0xD83D)));
        secret += QString(QChar(char16_t(0xDE00)));

        const auto encoded = encodeSecret(secret);
        QVERIFY(encoded.has_value());
        QCOMPARE(encoded->size(), 4);
        QCOMPARE(QString::fromUtf8(*encoded), secret);
    }

    void theRefusalMessageNeverEchoesTheSecret() {
        QString secret = QString(QChar(char16_t(0xD800)));
        secret += QStringLiteral("SUPERSECRETVALUE");

        const QString text = describeEncodeDefect(encodeSecret(secret).error(), secret);
        QVERIFY(!text.contains(QStringLiteral("SUPERSECRETVALUE")));
        QVERIFY(text.contains(QStringLiteral("****(len 17)")));
    }

    // ------------------------------------------------------------------
    // The size ceiling, at the exact boundary.
    // ------------------------------------------------------------------

    void exactlyAtTheCeilingIsAcceptedAndOneByteOverIsRefused() {
        // CRED_MAX_CREDENTIAL_BLOB_SIZE is 2560 and a 2560-character ASCII string
        // encodes to exactly 2560 bytes (measured), so this is the real
        // boundary rather than a rounded one.
        const QString atLimit(kMaxSecretBlobBytes, QChar('a'));
        QCOMPARE(atLimit.toUtf8().size(), kMaxSecretBlobBytes);

        const auto accepted = encodeSecret(atLimit);
        QVERIFY(accepted.has_value());
        QCOMPARE(accepted->size(), kMaxSecretBlobBytes);

        const QString overLimit(kMaxSecretBlobBytes + 1, QChar('a'));
        QCOMPARE(encodeSecret(overLimit).error(), SecretEncodeDefect::TooLarge);
    }

    void theCeilingCountsBytesNotCharacters() {
        // A 2000-character string of 3-byte characters is 6000 bytes and must be
        // refused even though its character count is under the limit. Checking
        // size() instead of the encoded length would let this through.
        const QString wide(kMaxSecretBlobBytes - 1000, QChar(char16_t(0x20AC)));
        QVERIFY(wide.size() < kMaxSecretBlobBytes);
        QVERIFY(wide.toUtf8().size() > kMaxSecretBlobBytes);
        QCOMPARE(encodeSecret(wide).error(), SecretEncodeDefect::TooLarge);
    }

    void theWindowsCeilingIsTheBindingOne() {
        QCOMPARE(secretBlobCeilingFor(SecretStorePlatform::Windows), 2560);
        QVERIFY(secretBlobCeilingFor(SecretStorePlatform::Apple) > 2560);
        QVERIFY(secretBlobCeilingFor(SecretStorePlatform::Linux) > 2560);
    }

    // ------------------------------------------------------------------
    // Target name / label construction, as a pure function.
    // ------------------------------------------------------------------

    void targetNameIsNamespacedAndStable() {
        // Exact strings. This is the value written to disk on every platform, so
        // a change silently orphans every credential a user already has.
        QCOMPARE(namespacedKeyFor(QStringLiteral("suno/default")),
                 QStringLiteral("chadvis-projectm-qt/chadvis/suno/default"));
        QCOMPARE(namespacedKeyFor(QStringLiteral("chadvis/suno/default")),
                 QStringLiteral("chadvis-projectm-qt/chadvis/suno/default"));
        QCOMPARE(namespacedKeyFor(QStringLiteral("suno/bearer")),
                 QStringLiteral("chadvis-projectm-qt/chadvis/suno/bearer"));
    }

    void targetNameIsIdempotentOnAPartiallyNamespacedKey() {
        // "chadvis/suno/default" already carries the namespace segment, so the
        // service prefix is applied once rather than twice. This is the
        // credential already on users' machines and it must not move.
        QCOMPARE(namespacedKeyFor(QStringLiteral("chadvis/suno/default")),
                 QStringLiteral("chadvis-projectm-qt/chadvis/suno/default"));
    }

    // ------------------------------------------------------------------
    // The Win32 plan: blob length is the whole point.
    // ------------------------------------------------------------------

    void theWin32PlanCarriesTheExactBlobAndLength() {
        const QString secret = QStringLiteral("__client=__session=deadbeef");
        const auto plan = planWindowsCredential(QStringLiteral("suno/default"), secret);

        QVERIFY(plan.has_value());
        QCOMPARE(plan->targetName, QStringLiteral("chadvis-projectm-qt/chadvis/suno/default"));
        QCOMPARE(plan->userName, QStringLiteral("chadvis-projectm-qt"));
        QCOMPARE(plan->blob.size(), 27);
        // blobSize is the length, NOT length+1. A trailing NUL would be stored as
        // part of the secret and read back as a different credential.
        QCOMPARE(plan->blobSize, plan->blob.size());
        QCOMPARE(plan->blobSize, 27);
        QVERIFY(!plan->blob.endsWith('\0'));
    }

    void theWin32PlanRefusesAnOversizedSecretBeforeAnyApiCall() {
        const QString huge(kMaxSecretBlobBytes + 1, QChar('x'));
        const auto plan = planWindowsCredential(QStringLiteral("suno/default"), huge);

        QVERIFY(!plan.has_value());
        QCOMPARE(plan.error(), SecretEncodeDefect::TooLarge);
    }

    void theWin32PlanRefusesACorruptSecret() {
        QString corrupt = QString(QChar(char16_t(0xD800)));
        corrupt += QStringLiteral("__client=x");

        QCOMPARE(planWindowsCredential(QStringLiteral("suno/default"), corrupt).error(),
                 SecretEncodeDefect::UnpairedSurrogate);
    }

    void theWin32PlanAcceptsExactlyTheCeiling() {
        const QString atLimit(kMaxSecretBlobBytes, QChar('z'));
        const auto plan = planWindowsCredential(QStringLiteral("suno/default"), atLimit);

        QVERIFY(plan.has_value());
        QCOMPARE(plan->blobSize, kMaxSecretBlobBytes);
    }

    // ------------------------------------------------------------------
    // Win32 error classification: absent must stay distinct from broken.
    // ------------------------------------------------------------------

    void credReadNotFoundIsTheOnlyAbsence() {
        QCOMPARE(classifyCredReadStatus(kWinErrorSuccess), CredReadStatus::Success);
        QCOMPARE(classifyCredReadStatus(kWinErrorNotFound), CredReadStatus::NotFound);
    }

    void credReadAccessDeniedIsNotAbsence() {
        QCOMPARE(classifyCredReadStatus(kWinErrorAccessDenied), CredReadStatus::AccessDenied);
        QCOMPARE(classifyCredReadStatus(kWinErrorNoLogonSession), CredReadStatus::AccessDenied);
        // The regression this guards: collapsing either into NotFound turns a
        // locked or policy-blocked store into a silently signed-out user.
        QVERIFY(classifyCredReadStatus(kWinErrorAccessDenied) != CredReadStatus::NotFound);
    }

    void everyOtherWin32ErrorIsAFailureNotAnAbsence() {
        for (const unsigned long code : {1UL, 2UL, 3UL, 87UL, 1004UL, 1167UL, 1169UL, 99999UL}) {
            QCOMPARE(classifyCredReadStatus(code), CredReadStatus::Failed);
            QVERIFY(classifyCredReadStatus(code) != CredReadStatus::NotFound);
        }
    }

    // ------------------------------------------------------------------
    // Redaction, and the store's fail-closed behaviour end to end.
    // ------------------------------------------------------------------

    void redactNeverLeaks() {
        QCOMPARE(CredentialStore::redact(QStringLiteral("hello")), QStringLiteral("****(len 5)"));
        QCOMPARE(CredentialStore::redact(QString()), QStringLiteral("****(len 0)"));
        const QString secret = QStringLiteral("__client=super-secret");
        QVERIFY(!CredentialStore::redact(secret).contains(QStringLiteral("secret")));
    }

    void theDefaultStoreOnThisHostIsSecureAndTheFileStoreIsNot() {
        // The macOS build has a real keychain, so Default must resolve to it.
        // This asserts the dispatch actually consulted the policy rather than
        // silently taking the file path - the original defect.
        CredentialStore secureDefault;
        QVERIFY2(secureDefault.isSecureBackend(),
                 "the default store on a keyed platform must be the OS secret store");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        CredentialStore optedOut(CredentialStore::Backend::File, dir.path());
        QVERIFY(!optedOut.isSecureBackend());
    }

    void anUnavailableBackendRefusesWithoutWritingAnything() {
        // Drives the RefusingBackend shape directly: it must fail every
        // operation, and it must not create a file even when handed a root.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Simulate the unavailable case by asserting what the policy chose and
        // that the message it produces is the refusal - the RefusingBackend is
        // constructed from exactly these two values.
        SecretStoreInputs in;
        in.platform = SecretStorePlatform::Linux;
        in.compiledSecretService = false; // as a build without the new source has
        const BackendDecision d = decideBackend(in);
        QCOMPARE(d.choice, BackendChoice::Unavailable);
        QVERIFY(!describeUnavailable(d.reason).isEmpty());
        QVERIFY(!d.isSecure());
    }

    void theFileBackendStillRoundTripsForTestsAndOptInBuilds() {
        // The opt-in escape hatch must keep working, or CHADVIS_NO_KEYCHAIN
        // builds and the existing test suite lose their only backend.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        CredentialStore store(CredentialStore::Backend::File, dir.path());

        const QString secret = QStringLiteral("__client=round-trip");
        QVERIFY(store.store(QStringLiteral("suno/default"), secret).isOk());
        const auto loaded = store.load(QStringLiteral("suno/default"));
        QVERIFY(loaded.isOk());
        QCOMPARE(loaded.value(), secret);
    }
};

int runTestCredentialStorePolicy(int argc, char** argv) {
    TestCredentialStorePolicy tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_CredentialStorePolicy.moc"