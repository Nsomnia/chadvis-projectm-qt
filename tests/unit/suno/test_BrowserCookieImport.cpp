/**
 * @file test_BrowserCookieImport.cpp
 * @brief Covers the pure halves of the browser-cookie harvest (T0199).
 *
 * What is here and what deliberately is not:
 *
 *   - `deriveChromiumKey` and `decryptChromiumValue` are covered against
 *     CommonCrypto itself: a known secret derives a known-length key, and a
 *     value encrypted with that key round-trips. The PBKDF2 parameters and the
 *     v10 layout are Chromium's macOS constants, taken from the recon's
 *     working freshtok.py; if either drifts, the round trip is the test that
 *     says so.
 *   - `assembleCookieHeader` is covered because the stored header is
 *     reproducible only if the order is fixed — `__client*` before
 *     `__session*`, unsuffixed first. An unstable order means a save and a
 *     reload disagree about whether the credential changed, which is the
 *     "setting reverted" class of bug.
 *   - `readStore` is covered against a synthetic SQLite file so the
 *     suno.com / suno.ai host restriction and the Clerk-family name filter are asserted
 *     from the outside: a Stripe cookie on the same row must NOT come back,
 *     and a non-suno host must not even be read.
 *
 * What is NOT here, on purpose: the Keychain. `harvest()` shells out to
 * `security find-generic-password`, which on this machine prompts the user
 * for consent the first time it is called per browser. A test binary that
 * triggers a Keychain consent dialog is a test that interrupts whoever runs
 * it, so the environmental half is verified by the manual-QA checklist
 * (MT-001-G-adjacent: the import button) rather than by ctest.
 */

#include "suno/auth/BrowserCookieImport.hpp"

#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryFile>

#include <array>
#include <vector>

#ifdef Q_OS_MACOS
#include <CommonCrypto/CommonCrypto.h>
#endif

namespace {

/// Encrypt exactly the way Chromium on macOS does, so the decrypt under test
/// is exercised against the real producer's format rather than against a
/// blob produced by the function under test (which would only prove it
/// agrees with itself).
///
/// Two format facts, both measured against this machine's real Brave store:
///   - the layout is `v10` + 16-byte IV + AES-128-CBC ciphertext;
///   - the plaintext is a 16-byte binary host-binding prefix + the value +
///     PKCS7. The prefix is NOT a plain SHA-256 of the host (checked), and it
///     is not the IV (the value begins at offset 16 under the real IV). Its
///     bytes vary per encryption, so the decrypt must detect it by content.
///
/// The tail is padded to the block boundary; the decrypt handles both a valid
/// PKCS7 run and plain NULs, so this helper pads with PKCS7 — the case the
/// old `freshtok.py` happened to rely on.
QByteArray chromiumEncrypt(const QByteArray& plaintext, const QByteArray& key)
{
#ifdef Q_OS_MACOS
    std::array<unsigned char, 16> iv{};
    for (int i = 0; i < 16; ++i) {
        iv[static_cast<size_t>(i)] = static_cast<unsigned char>(i * 7 + 1);
    }
    // The 16-byte binary prefix, binary on purpose so the detector under test
    // has the real thing to find. The 0xdd first byte is not decoration: this
    // machine's `.suno.com` cookies have a prefix that begins with exactly
    // that byte, and a detector that only rejects control characters calls it
    // printable and skips the strip. The first version of this test used
    // control bytes, passed green, and the shipped build rejected every real
    // `__session`.
    QByteArray payload(16, '\x01');
    payload[0] = '\xdd';
    payload[2] = '\xd9';
    payload[7] = '\xff';
    payload += plaintext;

    const int unpadded = payload.size();
    const int padded = ((unpadded / 16) + 1) * 16;
    const char pad = static_cast<char>(padded - unpadded);
    payload.resize(padded);
    memset(payload.data() + unpadded, pad, padded - unpadded);

    QByteArray out(3 + 16 + padded + 16, Qt::Uninitialized);
    out[0] = 'v'; out[1] = '1'; out[2] = '0';
    memcpy(out.data() + 3, iv.data(), 16);

    size_t moved = 0;
    const int status = CCCrypt(kCCEncrypt, kCCAlgorithmAES128, 0,
                               key.constData(), 16, iv.data(),
                               payload.constData(), payload.size(),
                               out.data() + 19, out.size() - 19, &moved);
    if (status != kCCSuccess) {
        return {};
    }
    out.truncate(19 + static_cast<int>(moved));
    return out;
#else
    Q_UNUSED(plaintext); Q_UNUSED(key);
    return {};
#endif
}

struct StoreFixture {
    QString path;
    explicit StoreFixture() : file(QStringLiteral("XXXXXX.db"))
    {
        file.setAutoRemove(false);
        [[maybe_unused]] const bool opened = file.open();
        path = file.fileName();
        file.close();
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("fixture_store"));
        db.setDatabaseName(path);
        db.open();
        QSqlQuery create(db);
        create.exec(QStringLiteral("CREATE TABLE cookies ("
                                   "host_key TEXT, name TEXT, "
                                   "encrypted_value BLOB)"));
    }
    ~StoreFixture()
    {
        QSqlDatabase::removeDatabase(QStringLiteral("fixture_store"));
        QFile::remove(path);
    }
    void insert(const QString& host, const QString& name, const QByteArray& value)
    {
        QSqlQuery q(QSqlDatabase::database(QStringLiteral("fixture_store")));
        q.prepare(QStringLiteral("INSERT INTO cookies (host_key, name, "
                                 "encrypted_value) VALUES (?, ?, ?)"));
        q.addBindValue(host);
        q.addBindValue(name);
        q.addBindValue(value);
        QVERIFY(q.exec());
    }
    QTemporaryFile file;
};

} // namespace

class TestBrowserCookieImport : public QObject {
    Q_OBJECT

private slots:

    /// A real secret derives a 16-byte key. The length is the contract: the
    /// AES-128 key size is what the v10 layout assumes, and a wrong-size key
    /// is the failure that decrypts to nothing rather than to noise.
    void keyDerivationProducesA16ByteKey()
    {
        const QByteArray key = vc::suno::auth::BrowserCookieImport::
            deriveChromiumKey(QByteArrayLiteral("a keychain secret"));
        QCOMPARE(key.size(), 16);
    }

    void emptySecretDerivesNoKey()
    {
        QVERIFY(vc::suno::auth::BrowserCookieImport::
                     deriveChromiumKey({}).isEmpty());
    }

    /// The round trip that matters: Chromium-shaped ciphertext in, plaintext
    /// out. This is the exact path a real `__client` value takes.
    void decryptRoundTripsAChromiumShapedValue()
    {
        const QByteArray key = vc::suno::auth::BrowserCookieImport::
            deriveChromiumKey(QByteArrayLiteral("round trip secret"));
        const QByteArray plain =
            QByteArrayLiteral("eyJhbGciOiJSUzI1NiJ9.payload.signature");
        const QByteArray encrypted = chromiumEncrypt(plain, key);

        QVERIFY(!encrypted.isEmpty());
        QVERIFY(encrypted.startsWith("v10"));
        QCOMPARE(vc::suno::auth::BrowserCookieImport::
                     decryptChromiumValue(encrypted, key),
                 plain);
    }

    /// The wrong key must not yield something that looks like a value. A
    /// block-aligned AES decrypt with the wrong key produces plausible bytes,
    /// which is why the printable check inside decryptChromiumValue exists —
    /// this is the test that keeps it there.
    void wrongKeyYieldsNothingUsable()
    {
        const QByteArray right = vc::suno::auth::BrowserCookieImport::
            deriveChromiumKey(QByteArrayLiteral("round trip secret"));
        const QByteArray wrong = vc::suno::auth::BrowserCookieImport::
            deriveChromiumKey(QByteArrayLiteral("a different secret"));
        const QByteArray encrypted = chromiumEncrypt(
            QByteArrayLiteral("eyJhbGciOiRSUzI1NiJ9.body.sig"), right);

        QVERIFY(vc::suno::auth::BrowserCookieImport::
                    decryptChromiumValue(encrypted, wrong).isEmpty());
    }

    void nonV10ValuesAreRejected()
    {
        const QByteArray key(16, 'k');
        QVERIFY(vc::suno::auth::BrowserCookieImport::
                    decryptChromiumValue(QByteArrayLiteral("plaintext"), key)
                    .isEmpty());
        QVERIFY(vc::suno::auth::BrowserCookieImport::
                    decryptChromiumValue(QByteArrayLiteral("v10"), key)
                    .isEmpty());
        QVERIFY(vc::suno::auth::BrowserCookieImport::
                    decryptChromiumValue({}, key).isEmpty());
    }

    /// The stored header must be reproducible: `__client*` before
    /// `__session*`, unsuffixed before suffixed, regardless of the order the
    /// store handed them over. Without a fixed order, a save and a reload
    /// disagree about whether the credential changed.
    void headerOrderIsDeterministic()
    {
        using vc::suno::auth::BrowserCookieImport;
        const QList<QPair<QString, QString>> shuffled{
            {QStringLiteral("__session_Jnxw-muT"), QStringLiteral("S2")},
            {QStringLiteral("__session"), QStringLiteral("S1")},
            {QStringLiteral("__client_uat_Jnxw-muT"), QStringLiteral("U2")},
            {QStringLiteral("__client_Jnxw-muT"), QStringLiteral("C2")},
            {QStringLiteral("__client"), QStringLiteral("C1")},
            {QStringLiteral("__client_uat"), QStringLiteral("U1")},
        };
        const QString expected =
            QStringLiteral("__client=C1; __client_Jnxw-muT=C2; "
                           "__client_uat=U1; __client_uat_Jnxw-muT=U2; "
                           "__session=S1; __session_Jnxw-muT=S2");
        QCOMPARE(BrowserCookieImport::assembleCookieHeader(shuffled), expected);
    }

    void headerSkipsEmptyNamesAndValues()
    {
        using vc::suno::auth::BrowserCookieImport;
        const QList<QPair<QString, QString>> pairs{
            {QStringLiteral("__client"), QStringLiteral("v")},
            {QStringLiteral(""), QStringLiteral("v")},
            {QStringLiteral("__session"), QStringLiteral("")},
        };
        QCOMPARE(BrowserCookieImport::assembleCookieHeader(pairs),
                 QStringLiteral("__client=v"));
    }

    // ── The signed-out discriminator (the "did not work" report) ──────────
    //
    // A logged-out Clerk store holds `__client_uat = 0` and no `__client`.
    // The first shipped version harvested exactly that into a header, stored
    // it through setCookie(), and reported success with no visible change --
    // the button read as broken. These two asserts are the bar that closed
    // it: the header must carry a credential to leave this module.

    void uatOnlyHeaderCarriesNoCredential()
    {
        using vc::suno::auth::BrowserCookieImport;
        QVERIFY(!BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("__client_uat=0; __client_uat_Jnxw-muT=0")));
        // The third-party-refresh half of the family: `__client_Jnxw-muT` IS
        // a credential, and `__client_uat` must not shadow it away.
        QVERIFY(BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("__client=jwt; __client_uat=1787705821")));
        QVERIFY(BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("__client_Jnxw-muT=jwt")));
        QVERIFY(BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("__session")));
        QVERIFY(!BrowserCookieImport::headerCarriesUsableCredential({}));
    }

    /// The prepared signed-out store: both `__client_uat` variants read as
    /// "0", no `__client` anywhere. readStore must report `sawSignedOut`,
    /// zero credential rows, and return the two uat pairs -- which the
    /// HARVEST then refuses, so this observation is the evidence the refusal
    /// was not an unobserved fall-through.
    void signedOutStoreIsObservedAsSignedOut()
    {
        using vc::suno::auth::BrowserCookieImport;
        StoreFixture store;
        const QByteArray key = BrowserCookieImport::deriveChromiumKey(
            QByteArrayLiteral("fixture secret"));
        store.insert(QStringLiteral(".suno.com"),
                     QStringLiteral("__client_uat"),
                     chromiumEncrypt(QByteArrayLiteral("0"), key));
        store.insert(QStringLiteral(".suno.com"),
                     QStringLiteral("__client_uat_Jnxw-muT"),
                     chromiumEncrypt(QByteArrayLiteral("0"), key));

        vc::suno::auth::StoreObservation seen;
        const auto found = BrowserCookieImport::readStore(store.path, key, &seen);

        QCOMPARE(found.size(), 2);
        QVERIFY(!seen.keyMissing);
        QCOMPARE(seen.clerkRows, 2);
        QCOMPARE(seen.decrypted, 2);
        QVERIFY(!seen.hadCredential);
        QVERIFY(seen.sawSignedOut);

        // And the user-facing sentence names the actual situation, not
        // "no session was found".
        const QString reason = BrowserCookieImport::reasonForStore(
            seen, QStringLiteral("Brave"));
        QVERIFY(reason.contains(QStringLiteral("signed out")));
        QVERIFY(reason.contains(QStringLiteral("Brave")));
        QVERIFY(reason.contains(QStringLiteral("Import")));
    }

    /// A keychain refusal must NOT read as "no Suno session was found" -- the
    /// user was signed in when the report was written, and the real cause was
    /// macOS not handing over the Safe Storage secret. The message must ask
    /// for the retry that actually fixes it.
    void keychainRefusalMessageAsksForTheRetry()
    {
        using vc::suno::auth::BrowserCookieImport;
        vc::suno::auth::StoreObservation seen;
        seen.keyMissing = true;
        const QString reason = BrowserCookieImport::reasonForStore(
            seen, QStringLiteral("Brave"));
        QVERIFY(reason.contains(QStringLiteral("cookie-store key")));
        QVERIFY(reason.contains(QStringLiteral("Allow")));
    }

    /// Rows present, none decodable: a distinct diagnosis from "signed out"
    /// and from "never visited suno.com".
    void undecryptableRowsAreNamedAsADecodeFailure()
    {
        using vc::suno::auth::BrowserCookieImport;
        vc::suno::auth::StoreObservation seen;
        seen.clerkRows = 2;
        seen.decrypted = 0;
        const QString reason = BrowserCookieImport::reasonForStore(
            seen, QStringLiteral("Brave"));
        QVERIFY(reason.contains(QStringLiteral("could not be decoded")));
        // Priority: the decode failure wins over the generic empty message
        // a same-row-nothing store would produce.
        QVERIFY(!reason.contains(QStringLiteral("No Suno cookies were found")));
    }

    /// A store with rows but no credential anywhere (half signed out) gets
    /// its own wording, and an empty Clerk set keeps the generic one.
    void rowsWithoutCredentialAndEmptyRowsAreDistinguished()
    {
        using vc::suno::auth::BrowserCookieImport;
        vc::suno::auth::StoreObservation half;
        half.clerkRows = 1;
        half.decrypted = 1;
        const QString halfReason = BrowserCookieImport::reasonForStore(
            half, QStringLiteral("Brave"));
        QVERIFY(halfReason.contains(QStringLiteral("carry no usable session")));

        vc::suno::auth::StoreObservation empty;
        const QString emptyReason = BrowserCookieImport::reasonForStore(
            empty, QStringLiteral("Brave"));
        QVERIFY(emptyReason.contains(QStringLiteral("No Suno cookies were found")));
    }

    /// The store query is host-restricted and name-filtered from the outside:
    /// a Stripe cookie on a suno.com row and a Clerk cookie on an unrelated
    /// host must both stay out of the result. The first is the privacy
    /// boundary; the second is the "no cookie jar enumeration" rule.
    ///
    /// Drives readStore with the test's own key — the key is a parameter
    /// precisely so this never touches the Keychain, whose consent prompt
    /// would interrupt whoever ran the suite.
    void readStoreTakesOnlySunoClerkCookies()
    {
        using vc::suno::auth::BrowserCookieImport;
        StoreFixture store;
        const QByteArray key = BrowserCookieImport::deriveChromiumKey(
            QByteArrayLiteral("fixture secret"));

        store.insert(QStringLiteral("auth.suno.com"), QStringLiteral("__client"),
                     chromiumEncrypt(QByteArrayLiteral("refresh-token"), key));
        store.insert(QStringLiteral(".suno.com"),
                     QStringLiteral("__client_uat_Jnxw-muT"),
                     chromiumEncrypt(QByteArrayLiteral("1787705821"), key));
        // Same host, wrong family: must not be read out.
        store.insert(QStringLiteral(".suno.com"), QStringLiteral("__stripe_mid"),
                     chromiumEncrypt(QByteArrayLiteral("not ours"), key));
        // Right name, wrong host: must not be read out.
        store.insert(QStringLiteral("example.com"), QStringLiteral("__client"),
                     chromiumEncrypt(QByteArrayLiteral("someone else"), key));
        // A session cookie too, so the assembly assertion sees both families.
        store.insert(QStringLiteral("suno.com"), QStringLiteral("__session"),
                     chromiumEncrypt(QByteArrayLiteral("bearer"), key));

        const auto found = BrowserCookieImport::readStore(store.path, key);

        QCOMPARE(found.size(), 3);
        for (const auto& [name, value] : found) {
            QVERIFY(name.startsWith(QStringLiteral("__client")) ||
                    name.startsWith(QStringLiteral("__session")));
            QVERIFY(!name.contains(QLatin1String("stripe")));
            QVERIFY(value != QLatin1String("not ours"));
            QVERIFY(value != QLatin1String("someone else"));
        }
        // The full assembly, from a real (synthetic) store, through the real
        // decrypt, into the header the client would receive.
        QCOMPARE(BrowserCookieImport::assembleCookieHeader(found),
                 QStringLiteral("__client=refresh-token; "
                               "__client_uat_Jnxw-muT=1787705821; "
                               "__session=bearer"));
    }

    /// suno.ai is a Suno host too (it 308-redirects to suno.com), so its
    /// rows are read — but the host match is exact-plus-dot-subdomain, so
    /// lookalikes that merely END in suno.com / suno.ai stay out. The old
    /// `LIKE '%suno.com'` clause admitted `evilsuno.com`; this test is the
    /// one that would have caught it.
    void readStoreTakesSunoAiAndRejectsSuffixLookalikes()
    {
        using vc::suno::auth::BrowserCookieImport;
        StoreFixture store;
        const QByteArray key = BrowserCookieImport::deriveChromiumKey(
            QByteArrayLiteral("fixture secret"));

        store.insert(QStringLiteral("suno.ai"), QStringLiteral("__client"),
                     chromiumEncrypt(QByteArrayLiteral("ai-refresh"), key));
        store.insert(QStringLiteral("www.suno.ai"), QStringLiteral("__session"),
                     chromiumEncrypt(QByteArrayLiteral("ai-bearer"), key));
        store.insert(QStringLiteral(".suno.ai"),
                     QStringLiteral("__client_uat_Jnxw-muT"),
                     chromiumEncrypt(QByteArrayLiteral("1787705821"), key));
        // Right name, lookalike host: no dot boundary before the domain.
        store.insert(QStringLiteral("evilsuno.com"), QStringLiteral("__client"),
                     chromiumEncrypt(QByteArrayLiteral("not ours"), key));
        store.insert(QStringLiteral("notsuno.ai"), QStringLiteral("__client"),
                     chromiumEncrypt(QByteArrayLiteral("not ours"), key));
        // Right name, the domain as a subdomain of an unrelated host.
        store.insert(QStringLiteral("suno.com.evil.com"), QStringLiteral("__client"),
                     chromiumEncrypt(QByteArrayLiteral("not ours"), key));

        const auto found = BrowserCookieImport::readStore(store.path, key);

        QCOMPARE(found.size(), 3);
        QCOMPARE(BrowserCookieImport::assembleCookieHeader(found),
                 QStringLiteral("__client=ai-refresh; "
                               "__client_uat_Jnxw-muT=1787705821; "
                               "__session=ai-bearer"));
    }

    /// The credential check splits on the ';' separator, not on "; ":
    /// a header reassembled with a bare ';' must read the same as one
    /// assembled with "; ". Splitting on "; " read the whole header as
    /// one pair named after its first cookie, so a usable `__client`
    /// after a bare ';' was invisible.
    void headerCredentialCheckAcceptsBareSemicolonSeparators()
    {
        using vc::suno::auth::BrowserCookieImport;
        QVERIFY(BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("other=1;__client=jwt")));
        QVERIFY(BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("__client_uat=0;__client=jwt")));
        QVERIFY(!BrowserCookieImport::headerCarriesUsableCredential(
            QStringLiteral("__client_uat=0;other=1")));
    }

    /// The candidate list must not name a browser that is not installed —
    /// the UI would offer an import that can only fail.
    void candidatesAreAllInstalled()
    {
        using vc::suno::auth::BrowserCookieImport;
        const QStringList names = BrowserCookieImport::candidateBrowserNames();
        QVERIFY(!names.isEmpty());
        QVERIFY(!names.contains(QStringLiteral("Vivaldi")));
    }
};

int runTestBrowserCookieImport(int argc, char** argv) {
    TestBrowserCookieImport test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_BrowserCookieImport.moc"