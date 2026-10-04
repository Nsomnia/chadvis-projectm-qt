#include <QtTest>
#include "core/Logger.hpp"
#include "suno/auth/AuthHeaders.hpp"
#include "suno/auth/AuthTypes.hpp"
#include "suno/auth/CredentialStore.hpp"
#include "suno/auth/JwtUtils.hpp"
#include "qml_bridge/QmlSingletonBridge.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrl>

#include <limits>
#include <vector>

using namespace vc::suno::auth;

namespace {

/// base64url (unpadded) of a compact JSON object - mirrors what Clerk emits.
QString b64url(const QJsonObject& obj) {
    const QByteArray encoded = QJsonDocument(obj).toJson(QJsonDocument::Compact)
                                       .toBase64(QByteArray::Base64UrlEncoding |
                                                 QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(encoded);
}

/// Build a syntactically valid, unsigned JWT for decoder tests.
QString makeJwt(const QJsonObject& payload) {
    return b64url({{"alg", "HS256"}, {"typ", "JWT"}}) + "." + b64url(payload) + ".sig";
}

constexpr qint64 kFutureExp = 4102444800; // 2100-01-01, safely unexpired

} // namespace

class DummySingleton : public QObject,
                       public qml_bridge::QmlSingletonBridge<
                               DummySingleton,
                               qml_bridge::SingletonPolicy::CachedQmlParented> {
    friend class qml_bridge::QmlSingletonBridge<
            DummySingleton, qml_bridge::SingletonPolicy::CachedQmlParented>;

    explicit DummySingleton(QObject* parent) : QObject(parent) {
        setInstance(this);
    }
};

class TestAuthModule : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // The file backend logs a plaintext-fallback warning on first use;
        // make sure a logger exists so that call is well-defined.
        vc::Logger::init("test_auth_module", false);
        tempRoot_ = std::make_unique<QTemporaryDir>();
        QVERIFY(tempRoot_->isValid());
    }

    void cachedSingletonClearsOnDestruction() {
        auto* singleton = DummySingleton::create(nullptr, nullptr);
        QVERIFY(singleton);
        QCOMPARE(DummySingleton::instance(), singleton);
        delete singleton;
        QVERIFY(!DummySingleton::instance());
    }

    // ------------------------------------------------------------------
    // JwtUtils
    // ------------------------------------------------------------------

    void jwtValidRoundtrip() {
        const QJsonObject payload{
                {"exp", static_cast<qint64>(kFutureExp)},
                {"sid", "sess_abc"},
                {"suno.com/claims/user_id", "usr_42"},
        };
        auto claims = JwtUtils::claims(makeJwt(payload));
        QVERIFY(claims.has_value());

        QCOMPARE(JwtUtils::expiryEpochSecs(*claims), kFutureExp);
        QCOMPARE(claims->value("sid").toString(), QStringLiteral("sess_abc"));
        // Exact slash-claim key...
        QCOMPARE(JwtUtils::claimString(*claims, "suno.com/claims/user_id"),
                 QStringLiteral("usr_42"));
        // ...and tolerant lookup without the vendor prefix.
        QCOMPARE(JwtUtils::claimString(*claims, "user_id"), QStringLiteral("usr_42"));

        QVERIFY(!JwtUtils::isExpired(*claims));
        QVERIFY(!JwtUtils::isExpired(*claims, 300));

        BearerToken token;
        token.jwt = makeJwt(payload);
        token.expiresAt = QDateTime::fromSecsSinceEpoch(kFutureExp, QTimeZone::UTC);
        QVERIFY(!token.isExpiringSoon());
    }

    void jwtMalformedInputs() {
        // Not enough / empty segments.
        QVERIFY(!JwtUtils::claims(QStringLiteral("")).has_value());
        QVERIFY(!JwtUtils::claims(QStringLiteral("onlyone")).has_value());
        QVERIFY(!JwtUtils::claims(QStringLiteral("two.segments")).has_value());
        QVERIFY(!JwtUtils::claims(QStringLiteral(".payload.sig")).has_value());
        // Invalid base64url in the payload segment.
        QVERIFY(!JwtUtils::claims(QStringLiteral("hdr.!!!!not-base64!!!!.sig")).has_value());
        // Valid base64 but not a JSON object (array).
        QVERIFY(!JwtUtils::claims(QStringLiteral("hdr.WzEsMl0.sig")).has_value()); // "[1,2]"
        // Valid JSON object but garbage after it.
        QVERIFY(!JwtUtils::claims(QStringLiteral("hdr.e30gZ2FyYmFnZQ.sig")).has_value());

        // None of the above may crash; exp helpers stay defensive too.
        QCOMPARE(JwtUtils::expiryEpochSecs(QJsonObject{}), 0);
        QVERIFY(!JwtUtils::isExpired(QJsonObject{}));
    }

    void jwtUnpaddedBase64Url() {
        // Roundtrip through unpadded base64url (the common JWT wire format).
        auto claims = JwtUtils::claims(
                makeJwt({{"exp", static_cast<qint64>(kFutureExp)}, {"k", "v"}}));
        QVERIFY(claims.has_value());
        QCOMPARE(claims->value("k").toString(), QStringLiteral("v"));
    }

    void jwtExpirySemantics() {
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        auto expiredClaims = JwtUtils::claims(makeJwt({{"exp", now - 60}}));
        QVERIFY(expiredClaims.has_value());
        QVERIFY(JwtUtils::isExpired(*expiredClaims));

        // Within the grace window counts as expired (proactive refresh).
        auto almostExpired = JwtUtils::claims(makeJwt({{"exp", now + 60}}));
        QVERIFY(almostExpired.has_value());
        QVERIFY(JwtUtils::isExpired(*almostExpired));
        QVERIFY(!JwtUtils::isExpired(*almostExpired, 0));

        // Missing exp never reports expired.
        QVERIFY(!JwtUtils::isExpired(QJsonObject{{"foo", 1}}));
    }

    // ------------------------------------------------------------------
    // JwtUtils: the "exp" fail-closed gate
    //
    // expiryEpochSecs() deliberately folds "absent", "not a number" and "epoch
    // zero" into one 0 sentinel, and isExpired() used to read that 0 as
    // `if (exp <= 0) return false;` -- never expires. So a JWT with no `exp` at
    // all was installed as a live credential and no refresh timer was ever
    // armed. expiryDefect() now names each shape separately and
    // hasUsableLifetime() is the accept/reject gate.
    //
    // Everything below asserts the exact ENUM VALUE, never a message string, so
    // a wording change cannot break it, and every value was confirmed against
    // the real JwtUtils.cpp rather than reasoned about.
    // ------------------------------------------------------------------

    void missingExpiryIsADefectNotAnInfiniteLifetime() {
        const QJsonObject empty;
        QCOMPARE(JwtUtils::expiryDefect(empty), JwtUtils::ExpiryDefect::Missing);
        QVERIFY(!JwtUtils::hasUsableLifetime(empty));

        // The lossy sentinel is unchanged, and still 0 -- which is exactly why
        // it must never be the thing that decides acceptance.
        QCOMPARE(JwtUtils::expiryEpochSecs(empty), qint64{0});

        // Other claims present, `exp` still absent: "missing", not "present".
        const QJsonObject otherClaims{
                {"sub", QStringLiteral("user_2abc")},
                {"plan", QStringLiteral("pro")},
                {"suno/handle", QStringLiteral("chad")},
        };
        QCOMPARE(JwtUtils::expiryDefect(otherClaims), JwtUtils::ExpiryDefect::Missing);
        QVERIFY(!JwtUtils::hasUsableLifetime(otherClaims));

        // Through the decoder, because that is how a real token arrives: the
        // payload parses cleanly and simply carries no `exp`.
        const auto decoded =
                JwtUtils::claims(makeJwt({{"sub", QStringLiteral("user_2abc")}}));
        QVERIFY(decoded.has_value());
        QCOMPARE(JwtUtils::expiryDefect(*decoded), JwtUtils::ExpiryDefect::Missing);
        QVERIFY(!JwtUtils::hasUsableLifetime(*decoded));
    }

    void expiryDefectOverWireJson_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<int>("expected");
        QTest::addColumn<qint64>("epochSecs");

        using D = JwtUtils::ExpiryDefect;
        const auto add = [](const char* name, QByteArray json, D expected, qint64 epoch) {
            QTest::newRow(name) << std::move(json) << static_cast<int>(expected) << epoch;
        };

        // ── absent ──────────────────────────────────────────────────────
        add("no-claims", "{}", D::Missing, 0);
        add("other-claims-only", R"({"sub":"user_2abc","plan":"pro"})", D::Missing, 0);

        // ── present but not a JSON number ───────────────────────────────
        // isDouble() is the JSON-number test, so nothing is coerced: a quoted
        // decimal is the shape a hand-edited config or a mis-typed issuer
        // produces, and accepting it would mean trusting a string.
        add("quoted-decimal", R"({"exp":"1712345678"})", D::NotIntegral, 0);
        add("quoted-non-decimal", R"({"exp":"soon"})", D::NotIntegral, 0);
        add("true", R"({"exp":true})", D::NotIntegral, 0);
        add("false", R"({"exp":false})", D::NotIntegral, 0);
        add("null", R"({"exp":null})", D::NotIntegral, 0);
        add("array", R"({"exp":[1712345678]})", D::NotIntegral, 0);
        add("object", R"({"exp":{"seconds":1712345678}})", D::NotIntegral, 0);

        // ── fractional ──────────────────────────────────────────────────
        // Qt's parser stores 1712345678 AND 1712345678.0 both as Double, so
        // isDouble() alone cannot separate them; the whole-number check is what
        // does, and these two rows below (same number, one written with a
        // decimal point) are the proof that it does.
        add("fractional-1.5", R"({"exp":1.5})", D::NotIntegral, 0);
        add("fractional--1.5", R"({"exp":-1.5})", D::NotIntegral, 0);
        add("fractional-0.5", R"({"exp":0.5})", D::NotIntegral, 0);

        // ── integral but not an epoch-seconds instant ───────────────────
        add("zero", R"({"exp":0})", D::NotPositive, 0);
        add("negative-one", R"({"exp":-1})", D::NotPositive, 0);
        add("negative-far", R"({"exp":-1712345678})", D::NotPositive, 0);
        add("zero-written-as-double", R"({"exp":0.0})", D::NotPositive, 0);

        // ── out of range for qint64: bounded before the narrowing cast ──
        // 2^63 is the first double above qint64::max(), and -2^63 is exactly
        // qint64::min(), which is why the high bound is `>=` and the low one is
        // `<`. The last row is the low bound landing on the value it names, so
        // it must be reported as NotPositive rather than as out of range.
        add("1e300", R"({"exp":1e300})", D::NotIntegral, 0);
        add("-1e300", R"({"exp":-1e300})", D::NotIntegral, 0);
        add("2^63", R"({"exp":9223372036854775808})", D::NotIntegral, 0);
        add("-2^63-is-qint64-min", R"({"exp":-9223372036854775808})", D::NotPositive, 0);

        // ── the two whole-number shapes a real issuer emits ─────────────
        // 1712345678 is 2024-04-09: elapsed, and still a perfectly good epoch.
        // Written with and without a trailing ".0" the classification is
        // identical, which is the non-redundancy claim about isDouble() made
        // executable.
        add("whole-int-elapsed", R"({"exp":1712345678})", D::Elapsed, 1712345678);
        add("whole-double-elapsed", R"({"exp":1712345678.0})", D::Elapsed, 1712345678);
        // 4102444800 is 2100-01-01: far enough that the default 300 s grace
        // cannot reach it, so this row is stable rather than clock-sensitive.
        add("whole-int-future", R"({"exp":4102444800})", D::None, 4102444800);
        add("whole-double-future", R"({"exp":4102444800.0})", D::None, 4102444800);
    }

    void expiryDefectOverWireJson() {
        QFETCH(QByteArray, json);
        QFETCH(int, expected);
        QFETCH(qint64, epochSecs);

        QJsonParseError parseError{};
        const auto doc = QJsonDocument::fromJson(json, &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
        const QJsonObject claims = doc.object();

        // Exact enum value, not "it is not None" -- a NotIntegral misfiled as
        // Missing (or the reverse) must fail here.
        QCOMPARE(static_cast<int>(JwtUtils::expiryDefect(claims)), expected);
        QCOMPARE(static_cast<int>(JwtUtils::expiryDefect(claims, /*graceSecs=*/0)), expected);

        // The lossy accessor keeps its documented contract for every shape.
        QCOMPARE(JwtUtils::expiryEpochSecs(claims), epochSecs);

        // The gate itself, and the identity it is documented to have:
        // hasUsableLifetime() == "expiryDefect with zero grace is None".
        const bool usable = JwtUtils::expiryDefect(claims, 0) == JwtUtils::ExpiryDefect::None;
        QCOMPARE(JwtUtils::hasUsableLifetime(claims), usable);
        QCOMPARE(JwtUtils::hasUsableLifetime(claims),
                 expected == static_cast<int>(JwtUtils::ExpiryDefect::None));
    }

    // NaN and the infinities are not representable in JSON, so they can only be
    // built programmatically -- but a QJsonValue can hold one, and the payload
    // is remote data, so the narrowing cast must be bounded for them too.
    // Casting NaN or +/-inf to qint64 is undefined behaviour, which is the
    // failure this guards: reaching the assertions at all is the assertion.
    void nonFiniteExpiryIsRejectedWithoutUnreachableBehaviour() {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        const auto expect = [](const char* label, QJsonValue value,
                               JwtUtils::ExpiryDefect expected) {
            const QJsonObject claims{{QStringLiteral("exp"), value}};
            QVERIFY2(JwtUtils::expiryDefect(claims) == expected,
                     qPrintable(QStringLiteral("%1 was classified %2")
                                        .arg(QString::fromLatin1(label))
                                        .arg(static_cast<int>(JwtUtils::expiryDefect(claims)))));
            QVERIFY(!JwtUtils::hasUsableLifetime(claims));
            QCOMPARE(JwtUtils::expiryEpochSecs(claims), qint64{0});
        };

        expect("quiet_NaN", QJsonValue(nan), JwtUtils::ExpiryDefect::NotIntegral);
        expect("-NaN", QJsonValue(-nan), JwtUtils::ExpiryDefect::NotIntegral);
        expect("+infinity", QJsonValue(inf), JwtUtils::ExpiryDefect::NotIntegral);
        expect("-infinity", QJsonValue(-inf), JwtUtils::ExpiryDefect::NotIntegral);
        // The finite-but-unrepresentable case, programmatically, so it does not
        // depend on how a JSON exponent is spelled.
        expect("1e300", QJsonValue(1e300), JwtUtils::ExpiryDefect::NotIntegral);
        expect("-1e300", QJsonValue(-1e300), JwtUtils::ExpiryDefect::NotIntegral);
        // 2^63 exactly: in range of a double, one step above qint64::max().
        expect("2^63", QJsonValue(9223372036854775808.0),
               JwtUtils::ExpiryDefect::NotIntegral);
        // qint64::min() exactly: inside the range check, so this is NotPositive.
        expect("qint64-min", QJsonValue(-9223372036854775808.0),
               JwtUtils::ExpiryDefect::NotPositive);
    }

    void elapsedAndFutureAreDistinctFromEveryDefect() {
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        // Inside the grace window, with zero slack: Elapsed holds because
        // now >= now, whatever the clock does inside the call.
        const QJsonObject wellPast{{QStringLiteral("exp"), now - 60}};
        QCOMPARE(JwtUtils::expiryDefect(wellPast), JwtUtils::ExpiryDefect::Elapsed);
        QVERIFY(JwtUtils::isExpired(wellPast));
        QVERIFY(!JwtUtils::hasUsableLifetime(wellPast));

        // Already elapsed at zero grace too, so the grace window is not what
        // produced the verdict above.
        QCOMPARE(JwtUtils::expiryDefect(wellPast, /*graceSecs=*/0),
                 JwtUtils::ExpiryDefect::Elapsed);

        // Far future: outside the 300 s default grace, so this needs a
        // 3300-second clock jump to change its mind.
        const QJsonObject farFuture{{QStringLiteral("exp"), now + 3600}};
        QCOMPARE(JwtUtils::expiryDefect(farFuture), JwtUtils::ExpiryDefect::None);
        QCOMPARE(JwtUtils::expiryDefect(farFuture, /*graceSecs=*/0),
                 JwtUtils::ExpiryDefect::None);
        QVERIFY(!JwtUtils::isExpired(farFuture));
        QVERIFY(JwtUtils::hasUsableLifetime(farFuture));
        QCOMPARE(JwtUtils::expiryEpochSecs(farFuture), now + 3600);

        // kFutureExp (2100-01-01) is what the pre-existing tests use; it must
        // not be affected by any of this.
        QCOMPARE(JwtUtils::expiryDefect(QJsonObject{{QStringLiteral("exp"), kFutureExp}}),
                 JwtUtils::ExpiryDefect::None);

        // Same verdict through the decoder, for the elapsed case.
        const auto decoded =
                JwtUtils::claims(makeJwt({{"exp", static_cast<qint64>(now - 60)}}));
        QVERIFY(decoded.has_value());
        QCOMPARE(JwtUtils::expiryDefect(*decoded), JwtUtils::ExpiryDefect::Elapsed);
        QVERIFY(!JwtUtils::hasUsableLifetime(*decoded));
    }

    // The grace window is a documented shift of one predicate -- Elapsed when
    // `now + graceSecs >= exp` -- so the switch sits exactly at
    // `graceSecs == exp - now`. `now` is a wall clock that cannot be sampled
    // atomically with the call, so this locates the switch by bisection and
    // pins it to the bracket the clock allowed across the search. That is an
    // exact statement rather than an approximation: with the clock frozen the
    // bracket collapses to the single value `exp - now`, and the two endpoints
    // are one second apart.
    void graceBoundaryIsExactlyTheDocumentedSwitch() {
        const qint64 exp = kFutureExp;
        const QJsonObject claims{{QStringLiteral("exp"), exp}};
        const qint64 searchStart = QDateTime::currentSecsSinceEpoch();

        // Both endpoints follow from the predicate alone, with no assumption
        // about how long the search takes:
        //   hi = exp - searchStart + 60  =>  now + hi >= exp + 60  (now >= searchStart)
        //   lo = exp - searchStart - 60  =>  Elapsed would need now >= searchStart + 60
        qint64 lo = exp - searchStart - 60;
        qint64 hi = exp - searchStart + 60;
        QCOMPARE(JwtUtils::expiryDefect(claims, lo), JwtUtils::ExpiryDefect::None);
        QCOMPARE(JwtUtils::expiryDefect(claims, hi), JwtUtils::ExpiryDefect::Elapsed);

        // Monotonicity is part of the contract, not an assumption: a larger
        // grace can only ever move the boundary further out, so every probe is
        // either None or Elapsed and never a defect.
        while (hi - lo > 1) {
            const qint64 mid = lo + (hi - lo) / 2;
            const JwtUtils::ExpiryDefect defect = JwtUtils::expiryDefect(claims, mid);
            QVERIFY2(defect == JwtUtils::ExpiryDefect::None ||
                             defect == JwtUtils::ExpiryDefect::Elapsed,
                     qPrintable(QStringLiteral("grace %1 classified as %2")
                                        .arg(mid)
                                        .arg(static_cast<int>(defect))));
            if (defect == JwtUtils::ExpiryDefect::Elapsed) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        const qint64 searchEnd = QDateTime::currentSecsSinceEpoch();

        // `hi` is the smallest grace that reports Elapsed, i.e. the switch, and
        // the switch is `exp - now`. Bounding `now` by the search window is what
        // turns that into a checkable interval.
        QCOMPARE(hi - lo, qint64{1});
        QVERIFY2(hi >= exp - searchEnd,
                 qPrintable(QStringLiteral("switch %1 is below exp - searchEnd %2")
                            .arg(hi)
                            .arg(exp - searchEnd)));
        QVERIFY2(hi <= exp - searchStart,
                 qPrintable(QStringLiteral("switch %1 is above exp - searchStart %2")
                            .arg(hi)
                            .arg(exp - searchStart)));

        // And the classification really does flip between the two neighbours,
        // with isExpired() agreeing at each one.
        QCOMPARE(JwtUtils::expiryDefect(claims, hi), JwtUtils::ExpiryDefect::Elapsed);
        QCOMPARE(JwtUtils::expiryDefect(claims, lo), JwtUtils::ExpiryDefect::None);
        QVERIFY(JwtUtils::isExpired(claims, hi));
        QVERIFY(!JwtUtils::isExpired(claims, lo));

        // With the default grace and a 2100 expiry the switch is far negative,
        // which is the same formula evaluated outside the interesting range.
        QCOMPARE(JwtUtils::expiryDefect(claims, JwtUtils::kDefaultExpiryGraceSecs),
                 JwtUtils::ExpiryDefect::None);
        QCOMPARE(JwtUtils::kDefaultExpiryGraceSecs, qint64{300});
    }

    // The documented relationship, asserted as an identity over a table that
    // spans every defect class plus the two usable ones. The expectation per row
    // is derived from the ROW, not read back out of expiryDefect(): checking the
    // identity against itself would pass even if both sides were changed to
    // answer some third question.
    void usableLifetimeIsExactlyTheZeroGraceClassification() {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const auto fromJson = [](const QByteArray& text) {
            return QJsonDocument::fromJson(text).object();
        };

        // {claims, may this be accepted as a live credential?}
        const std::vector<std::pair<QJsonObject, bool>> cases{
                {QJsonObject{}, false},                             // no exp at all
                {fromJson(R"({"sub":"user_2abc"})"), false},        // no exp at all
                {fromJson(R"({"exp":"1712345678"})"), false},       // quoted decimal
                {fromJson(R"({"exp":true})"), false},               // bool
                {fromJson(R"({"exp":null})"), false},               // null
                {fromJson(R"({"exp":[1]})"), false},                 // array
                {fromJson(R"({"exp":1.5})"), false},                // fractional
                {fromJson(R"({"exp":1e300})"), false},              // out of range
                {fromJson(R"({"exp":0})"), false},                  // epoch zero
                {fromJson(R"({"exp":-1})"), false},                 // negative
                {QJsonObject{{QStringLiteral("exp"),
                              QJsonValue(std::numeric_limits<double>::quiet_NaN())}},
                 false},
                {QJsonObject{{QStringLiteral("exp"),
                              QJsonValue(std::numeric_limits<double>::infinity())}},
                 false},
                {fromJson(R"({"exp":1712345678})"), false},         // 2024, elapsed
                {QJsonObject{{QStringLiteral("exp"), now - 60}}, false},
                {fromJson(R"({"exp":4102444800})"), true},          // 2100
                {QJsonObject{{QStringLiteral("exp"), now + 3600}}, true},
        };

        std::size_t usable = 0;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const QJsonObject& claims = cases[i].first;
            const bool expected = cases[i].second;
            QVERIFY2(JwtUtils::hasUsableLifetime(claims) == expected,
                     qPrintable(QStringLiteral("case %1: hasUsableLifetime reported %2, "
                                               "the row says %3")
                                    .arg(static_cast<qulonglong>(i))
                                    .arg(JwtUtils::hasUsableLifetime(claims))
                                    .arg(expected)));
            // ...and the identity itself, against the same hand-derived value.
            QVERIFY2(JwtUtils::hasUsableLifetime(claims) ==
                             (JwtUtils::expiryDefect(claims, /*graceSecs=*/0) ==
                              JwtUtils::ExpiryDefect::None),
                     qPrintable(QStringLiteral("case %1 broke the documented identity")
                                    .arg(static_cast<qulonglong>(i))));
            if (JwtUtils::hasUsableLifetime(claims)) {
                ++usable;
            }
        }
        // Sanity on the table itself: exactly the last two rows may be usable,
        // so a table that quietly stopped exercising the gate cannot pass.
        QCOMPARE(usable, std::size_t{2});
    }

    // ------------------------------------------------------------------
    // AuthHeaders
    // ------------------------------------------------------------------

    void authHeadersApplyPostFields() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "POST",
                QByteArrayLiteral("{}"));

        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/feed/")));
        headers.apply(request);

        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer jwt-value"));
        QCOMPARE(request.rawHeader("Origin"), QByteArray("https://suno.com"));
        QCOMPARE(request.rawHeader("Referer"), QByteArray("https://suno.com/"));
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray(kBrowserUserAgent));
        QCOMPARE(request.rawHeader("Accept"), QByteArray("*/*"));
        QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
        QCOMPARE(request.rawHeader("Device-Id"), QByteArray("uuid-1234"));
        QVERIFY(request.rawHeader("Browser-Token").isEmpty());
        QVERIFY(request.attribute(QNetworkRequest::RedirectPolicyAttribute)
                        .value<QNetworkRequest::RedirectPolicy>() ==
                QNetworkRequest::ManualRedirectPolicy);
    }

    void authHeadersGetOmitsJsonContentType() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "GET", {});
        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/session/")));
        request.setRawHeader("Content-Type", QByteArray("application/json"));
        headers.apply(request);

        QVERIFY(request.rawHeader("Content-Type").isEmpty());
        QCOMPARE(request.rawHeader("Accept"), QByteArray("*/*"));
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer jwt-value"));
    }

    void authHeadersEmptyPostOmitsContentType() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "POST", {});
        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/action")));
        request.setRawHeader("Content-Type", QByteArray("application/json"));
        headers.apply(request);

        QVERIFY(request.rawHeader("Content-Type").isEmpty());
    }

    void authHeadersWithoutBearerFailClosed() {
        const StudioApiHeaders headers =
                makeStudioApiHeaders({}, QStringLiteral("uuid-1234"), "GET", {});
        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/session/")));
        request.setRawHeader("Authorization", QByteArray("Bearer stale"));
        headers.apply(request);

        QVERIFY(request.rawHeader("Authorization").isEmpty());
    }

    void authHeadersDoNotSynthesizeBrowserToken() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "POST",
                QByteArrayLiteral("{}"));
        QNetworkRequest request(
                QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/feed/v3")));
        headers.apply(request);
        QVERIFY(request.rawHeader("Browser-Token").isEmpty());
    }

    void studioApiHostPolicy_data() {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");

        QTest::newRow("captured-host")
                << QStringLiteral("https://studio-api-prod.suno.com/api/feed/v3") << true;
        QTest::newRow("explicit-default-port")
                << QStringLiteral("https://studio-api-prod.suno.com:443/api/session/") << true;
        QTest::newRow("modal") << QStringLiteral(
                "https://suno-ai--orpheus-prod-web.modal.run/v1/orchestrator/chat") << false;
        QTest::newRow("auth-host") << QStringLiteral("https://auth.suno.com/v1/client") << false;
        QTest::newRow("suffix-confusion")
                << QStringLiteral("https://studio-api-prod.suno.com.evil.test/api") << false;
        QTest::newRow("cleartext")
                << QStringLiteral("http://studio-api-prod.suno.com/api") << false;
        QTest::newRow("user-info")
                << QStringLiteral("https://user@studio-api-prod.suno.com/api") << false;
        QTest::newRow("non-default-port")
                << QStringLiteral("https://studio-api-prod.suno.com:444/api") << false;
        QTest::newRow("relative") << QStringLiteral("/api/session/") << false;
    }

    void studioApiHostPolicy() {
        QFETCH(QString, url);
        QFETCH(bool, allowed);
        QCOMPARE(isAllowedStudioApiUrl(QUrl(url)), allowed);
    }

    void cookieNormalizationPreservesOpaqueRemainder_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("unlabelled")
                << QStringLiteral(" __client=abc==; __session=Keep-Case ")
                << QStringLiteral("__client=abc==; __session=Keep-Case");
        QTest::newRow("mixed-case-label")
                << QStringLiteral("cOoKiE:__client=abc==; opaque=A%2FB")
                << QStringLiteral("__client=abc==; opaque=A%2FB");
        QTest::newRow("embedded-label")
                << QStringLiteral("Cookie: __client=Cookie:opaque")
                << QStringLiteral("__client=Cookie:opaque");
        QTest::newRow("label-only") << QStringLiteral(" Cookie: ") << QString();
    }

    void cookieNormalizationPreservesOpaqueRemainder() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(normalizeCookieHeader(input), expected);
    }

    // ------------------------------------------------------------------
    // CredentialStore (forced FILE backend)
    // ------------------------------------------------------------------

    void credentialStoreRoundtrip() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("suno/default", QStringLiteral("cookie-a=1; cookie-b=2"))
                        .isOk());

        auto loaded = store.load("suno/default");
        QVERIFY(loaded.isOk());
        QCOMPARE(loaded.value(), QStringLiteral("cookie-a=1; cookie-b=2"));
    }

    void credentialStoreOverwrite() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("suno/default", QStringLiteral("first")).isOk());
        QVERIFY(store.store("suno/default", QStringLiteral("second")).isOk());

        auto loaded = store.load("suno/default");
        QVERIFY(loaded.isOk());
        QCOMPARE(loaded.value(), QStringLiteral("second"));
    }

    void credentialStoreRemoveThenLoadFails() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("suno/session", QStringLiteral("secret")).isOk());
        QVERIFY(store.remove("suno/session").isOk());

        auto loaded = store.load("suno/session");
        QVERIFY(loaded.isErr());
    }

    void credentialStoreFilePermissions() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("perm/check", QStringLiteral("x")).isOk());
#if defined(Q_OS_UNIX)
        const QString path = tempRoot_->path() + QStringLiteral(
                                                   "/chadvis-projectm-qt_chadvis_perm_check");
        QFile file(path);
        QVERIFY(file.exists());
        const QFile::Permissions perms = file.permissions();
        QVERIFY(perms & QFileDevice::ReadOwner);
        QVERIFY(perms & QFileDevice::WriteOwner);
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup)));
        QVERIFY(!(perms & (QFileDevice::ReadOther | QFileDevice::WriteOther)));
#endif
    }

    void redactNeverLeaksSecrets() {
        QCOMPARE(CredentialStore::redact(QStringLiteral("hello")),
                 QStringLiteral("****(len 5)"));
        QCOMPARE(CredentialStore::redact(QString()), QStringLiteral("****(len 0)"));
        const QString secret = QStringLiteral("__client=super-secret-value");
        QVERIFY(!CredentialStore::redact(secret).contains(QStringLiteral("secret")));
    }

private:
    std::unique_ptr<QTemporaryDir> tempRoot_;
};

int runTestAuthModule(int argc, char** argv) {
    TestAuthModule tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_AuthModule.moc"
