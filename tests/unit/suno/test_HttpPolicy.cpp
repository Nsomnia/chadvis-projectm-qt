#include <QtTest>

#include "suno/HttpPolicy.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QNetworkRequest>
#include <QUrl>

#include <chrono>
#include <limits>
#include <string>
#include <string_view>

using namespace vc::suno::http;

/// Alias so the fully-qualified form is available too. Written out because the
/// qualified name is the point: `vc::suno::parseRetryAfter` also exists and these
/// tests must be unmistakably about the policy's entry point.
namespace http = vc::suno::http;

namespace {

/// A sequential in-memory device. QBuffer is the right shape for these tests on
/// purpose: it is the one QIODevice whose `bytesAvailable()` and `read()`
/// contract is exact, so "the reader stopped one byte past the cap" is an
/// assertion about real behaviour rather than about a fake's bookkeeping.
class BodyFixture
{
public:
    explicit BodyFixture(const qint64 byteCount, const char fill = 'x')
        : payload_(static_cast<qsizetype>(byteCount), fill), buffer_(&payload_)
    {
        opened_ = buffer_.open(QIODevice::ReadOnly);
    }

    [[nodiscard]] bool isOpen() const { return opened_; }
    [[nodiscard]] QBuffer& device() { return buffer_; }
    /// How far into the stream the reader got. This is the assertion that proves
    /// the refusal happened *during* the read rather than after the fact.
    [[nodiscard]] qint64 consumed() const { return static_cast<qint64>(buffer_.pos()); }

private:
    QByteArray payload_;
    QBuffer buffer_;
    bool opened_{false};
};

/// 1994-11-06T08:49:37Z. This is simultaneously RFC 2822's own example date and
/// the canonical IMF-fixdate sample, which is why it is the right fixture: if the
/// hand-rolled parser ever regresses to something that only handles RFC 850, this
/// number stops matching and the tests say so.
constexpr std::int64_t kRfcExampleEpochSecs = 784111777;

} // namespace

class TestHttpPolicy : public QObject
{
    Q_OBJECT

private slots:
    // ── timeouts ────────────────────────────────────────────────────────────

    void everyRequestClassHasAnExactTimeout()
    {
        // Asserted to the millisecond, because "some sensible timeout" is how a
        // media deadline silently becomes a total-transfer cap later. The
        // reasoning for each number is in HttpPolicy.hpp, not here.
        QCOMPARE(transferTimeoutMs(RequestClass::Auth), 30'000);
        QCOMPARE(transferTimeoutMs(RequestClass::JsonApi), 30'000);
        QCOMPARE(transferTimeoutMs(RequestClass::MediaDownload), 30'000);
        QCOMPARE(transferTimeoutMs(RequestClass::Upload), 60'000);
    }

    void anUntouchedRequestHasNoTimeoutAtAll()
    {
        // The measured Qt fact this policy exists to correct: "If this function is
        // not called, the timeout is disabled and has the value zero" (Qt docs,
        // setTransferTimeout). Qt's DefaultTransferTimeoutConstant of 30000 is
        // only applied when the setter is CALLED with no argument, so a request
        // nobody configured waits forever. If a future Qt ever changes this
        // default, this assertion is the one that notices.
        const QNetworkRequest pristine{
                QStringLiteral("https://studio-api-prod.suno.com/api/session/")};
        QCOMPARE(pristine.transferTimeout(), 0);
        QCOMPARE(pristine.transferTimeoutAsDuration().count(),
                 std::chrono::milliseconds::rep{0});
    }

    void theTimeoutIsActuallySetOnARealRequest_data()
    {
        QTest::addColumn<int>("requestClass");
        QTest::newRow("auth") << static_cast<int>(RequestClass::Auth);
        QTest::newRow("json-api") << static_cast<int>(RequestClass::JsonApi);
        QTest::newRow("media-download") << static_cast<int>(RequestClass::MediaDownload);
        QTest::newRow("upload") << static_cast<int>(RequestClass::Upload);
    }

    void theTimeoutIsActuallySetOnARealRequest()
    {
        QFETCH(int, requestClass);
        const auto cls = static_cast<RequestClass>(requestClass);

        QNetworkRequest request{QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/feed/v3/"))};
        applyRequestPolicy(request, cls);

        // Both accessors, exactly. This is the test that stops the attribute
        // being "applied" in a way that a later refactor silently drops -- a
        // constexpr table nobody reads looks identical to a working one.
        QCOMPARE(request.transferTimeout(), transferTimeoutMs(cls));
        QCOMPARE(request.transferTimeoutAsDuration().count(),
                 std::chrono::milliseconds::rep(transferTimeoutMs(cls)));
        QVERIFY(request.transferTimeout() > 0);
    }

    void reapplyingThePolicyReplacesTheTimeoutRatherThanKeepingTheFirst()
    {
        QNetworkRequest request{QUrl(QStringLiteral("https://auth.suno.com/v1/client"))};
        applyRequestPolicy(request, RequestClass::JsonApi);
        QCOMPARE(request.transferTimeout(), 30'000);
        applyRequestPolicy(request, RequestClass::Upload);
        QCOMPARE(request.transferTimeout(), 60'000);
    }

    void applyRequestPolicyDoesNotTouchRedirectPolicy()
    {
        // Redirect policy is a *host-allowlist* decision (AGENTS.md §1: fail
        // closed on unverified hosts), owned by each caller. Pinned here so that
        // widening this function into "the whole policy" cannot quietly start
        // setting a redirect mode whose verification lives somewhere else.
        QNetworkRequest request{
                QStringLiteral("https://studio-api-prod.suno.com/api/session/")};

        // Measured, and the reason this asserts "unset" rather than a policy: a
        // fresh QNetworkRequest has NO RedirectPolicyAttribute at all, so
        // attribute() returns an invalid QVariant and toInt() collapses it to 0.
        // 0 is ManualRedirectPolicy, so an assertion of "0" would pass for the
        // wrong reason -- and would also pass if this function started *setting*
        // manual redirects. isValid() is the honest check.
        QVERIFY(!request.attribute(QNetworkRequest::RedirectPolicyAttribute).isValid());

        applyRequestPolicy(request, RequestClass::JsonApi);

        QVERIFY(!request.attribute(QNetworkRequest::RedirectPolicyAttribute).isValid());

        // And on a request that already carries the fail-closed policy -- the
        // shape every call site in the tree actually has -- it is left as found,
        // rather than reset to Qt's own default.
        QNetworkRequest manual{QStringLiteral("https://studio-api-prod.suno.com/api/session/")};
        manual.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                            QNetworkRequest::ManualRedirectPolicy);
        applyRequestPolicy(manual, RequestClass::JsonApi);
        QCOMPARE(manual.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
    }

    // ── response caps ───────────────────────────────────────────────────────

    void everyRequestClassHasAnExactByteCap()
    {
        QCOMPARE(maxResponseBytes(RequestClass::Auth), 1LL * 1024 * 1024);
        QCOMPARE(maxResponseBytes(RequestClass::JsonApi), 16LL * 1024 * 1024);
        QCOMPARE(maxResponseBytes(RequestClass::MediaDownload), 256LL * 1024 * 1024);
        QCOMPARE(maxResponseBytes(RequestClass::Upload), 1LL * 1024 * 1024);
    }

    void everyClassCapIsUsable_data()
    {
        QTest::addColumn<int>("requestClass");
        QTest::newRow("auth") << static_cast<int>(RequestClass::Auth);
        QTest::newRow("json-api") << static_cast<int>(RequestClass::JsonApi);
        QTest::newRow("media-download") << static_cast<int>(RequestClass::MediaDownload);
        QTest::newRow("upload") << static_cast<int>(RequestClass::Upload);
    }

    void everyClassCapIsUsable()
    {
        QFETCH(int, requestClass);
        const qint64 cap = maxResponseBytes(static_cast<RequestClass>(requestClass));
        QVERIFY(isUsableByteLimit(cap));
        auto reader = makeBodyReader(static_cast<RequestClass>(requestClass), cap);
        QVERIFY(reader.has_value());
        QCOMPARE(reader->capBytes(), cap);
    }

    void aBodyExactlyAtTheCapIsAccepted()
    {
        constexpr qint64 kCap = 8;
        BodyFixture fixture{kCap};
        QVERIFY(fixture.isOpen());

        auto reader = makeBodyReader(RequestClass::JsonApi, kCap);
        QVERIFY(reader.has_value());
        const auto drained = reader->pump(fixture.device());

        QVERIFY(drained.has_value());
        QCOMPARE(static_cast<qint64>(reader->body().size()), kCap);
        QCOMPARE(fixture.consumed(), kCap);
    }

    void aBodyOneByteOverTheCapIsRefusedWithoutEverStoringTheOverflow()
    {
        constexpr qint64 kCap = 8;
        BodyFixture fixture{kCap + 1};
        QVERIFY(fixture.isOpen());

        auto reader = makeBodyReader(RequestClass::JsonApi, kCap);
        QVERIFY(reader.has_value());
        const auto drained = reader->pump(fixture.device());

        QVERIFY(!drained.has_value());
        QCOMPARE(drained.error().error, PolicyError::ResponseTooLarge);
        QCOMPARE(drained.error().requestClass, RequestClass::JsonApi);
        QCOMPARE(drained.error().limitBytes, kCap);
        QCOMPARE(drained.error().observedBytes, kCap + 1);

        // The three assertions that make this a cap and not a post-hoc check:
        QCOMPARE(static_cast<qint64>(reader->body().size()), kCap);   // never exceeded
        QCOMPARE(fixture.consumed(), kCap + 1);                      // read stopped there
    }

    void anOverflowIsRefusedMidStreamAcrossManyChunks()
    {
        // Same rule, exercised through the multi-chunk path so an implementation
        // that only gets the single-chunk arithmetic right cannot pass.
        constexpr qint64 kCap = BodyReader::kProbeChunkBytes * 2 + 1000;
        BodyFixture fixture{kCap + 1};
        QVERIFY(fixture.isOpen());

        auto reader = makeBodyReader(RequestClass::JsonApi, kCap);
        QVERIFY(reader.has_value());
        const auto drained = reader->pump(fixture.device());

        QVERIFY(!drained.has_value());
        QCOMPARE(drained.error().error, PolicyError::ResponseTooLarge);
        QCOMPARE(drained.error().limitBytes, kCap);
        QCOMPARE(static_cast<qint64>(reader->body().size()), kCap);
        QCOMPARE(fixture.consumed(), kCap + 1);
    }

    void pumpingIsIncrementalAndTheBodyIsNotLostBetweenCalls()
    {
        // The shape a real reply handler uses: pump from readyRead, several
        // times, and the bytes already accepted stay accepted.
        constexpr qint64 kCap = 4096;
        QByteArray payload(static_cast<qsizetype>(kCap), 'y');
        QBuffer buffer(&payload);
        QVERIFY(buffer.open(QIODevice::ReadOnly));

        auto reader = makeBodyReader(RequestClass::JsonApi, kCap);
        QVERIFY(reader.has_value());

        QCOMPARE(static_cast<qint64>(reader->body().size()), 0);
        QVERIFY(reader->pump(buffer).has_value());
        const qint64 afterFirst = static_cast<qint64>(reader->body().size());
        QVERIFY(afterFirst > 0);
        QVERIFY(reader->pump(buffer).has_value());
        const qint64 afterSecond = static_cast<qint64>(reader->body().size());
        QVERIFY(afterSecond >= afterFirst);
        QVERIFY(reader->pump(buffer).has_value());
        QCOMPARE(static_cast<qint64>(reader->body().size()), kCap);
    }

    void anUnusableCapIsRejectedRatherThanMeaningUnlimited_data()
    {
        QTest::addColumn<qint64>("capBytes");
        QTest::newRow("zero") << qint64{0};
        QTest::newRow("negative") << qint64{-1};
        QTest::newRow("one-below-the-qsizetype-ceiling")
            << (kMaxSaneByteLimit + 1);
        QTest::newRow("absurd") << (std::numeric_limits<qint64>::max());
    }

    void anUnusableCapIsRejectedRatherThanMeaningUnlimited()
    {
        QFETCH(qint64, capBytes);
        QVERIFY(!isUsableByteLimit(capBytes));

        auto reader = makeBodyReader(RequestClass::JsonApi, capBytes);
        QVERIFY(!reader.has_value());
        QCOMPARE(reader.error().error, PolicyError::InvalidByteLimit);
        QCOMPARE(reader.error().limitBytes, capBytes);
        QCOMPARE(reader.error().requestClass, RequestClass::JsonApi);
        QCOMPARE(reader.error().observedBytes, qint64{0});
    }

    void aDefaultConstructedReaderRefusesInsteadOfReadingUnbounded()
    {
        // The backstop that makes "0 means unlimited" unreachable even if someone
        // bypasses makeBodyReader entirely -- e.g. a default-initialised member.
        BodyReader reader;
        QCOMPARE(reader.capBytes(), qint64{0});

        BodyFixture fixture{4096};
        QVERIFY(fixture.isOpen());
        const auto drained = reader.pump(fixture.device());

        QVERIFY(!drained.has_value());
        QCOMPARE(drained.error().error, PolicyError::InvalidByteLimit);
        QCOMPARE(drained.error().limitBytes, qint64{0});
        QCOMPARE(static_cast<qint64>(reader.body().size()), qint64{0});
        QCOMPARE(fixture.consumed(), qint64{0});   // nothing was read at all
    }

    void readBodyBoundedAppliesTheClassCapItself()
    {
        const qint64 cap = maxResponseBytes(RequestClass::Auth);
        BodyFixture fixture{cap};
        QVERIFY(fixture.isOpen());

        const auto body = readBodyBounded(fixture.device(), RequestClass::Auth);
        QVERIFY(body.has_value());
        QCOMPARE(static_cast<qint64>(body->size()), cap);
    }

    // ── diagnostics ─────────────────────────────────────────────────────────

    void everyNamedErrorIsDistinct()
    {
        const std::string_view none = toString(PolicyError::None);
        const std::string_view tooLarge = toString(PolicyError::ResponseTooLarge);
        const std::string_view badLimit = toString(PolicyError::InvalidByteLimit);

        QVERIFY(!none.empty());
        QVERIFY(!tooLarge.empty());
        QVERIFY(!badLimit.empty());
        QVERIFY(none != tooLarge);
        QVERIFY(none != badLimit);
        QVERIFY(tooLarge != badLimit);
        QCOMPARE(toString(PolicyError::ResponseTooLarge), "response-too-large");
        QCOMPARE(toString(PolicyError::InvalidByteLimit), "invalid-byte-limit");
        QCOMPARE(toString(PolicyError::None), "none");
    }

    void everyRequestClassHasADistinctName()
    {
        QVERIFY(requestClassName(RequestClass::Auth) != requestClassName(RequestClass::JsonApi));
        QVERIFY(requestClassName(RequestClass::JsonApi) !=
                requestClassName(RequestClass::MediaDownload));
        QVERIFY(requestClassName(RequestClass::MediaDownload) !=
                requestClassName(RequestClass::Upload));
        QVERIFY(requestClassName(RequestClass::Auth) == std::string_view("Clerk auth"));
    }

    void everyDiagnosticNamesItsRequestAndItsLimit()
    {
        // "network error" is the failure mode this replaces. Each sentence must
        // carry the class and the number, or it is not a diagnosis.
        const std::string tooLarge = describePolicyFailure(
                PolicyFailure{PolicyError::ResponseTooLarge, RequestClass::Auth, 1'048'576, 1'048'577});
        QVERIFY(tooLarge.find("Clerk auth") != std::string::npos);
        QVERIFY(tooLarge.find("1048576") != std::string::npos);
        QVERIFY(tooLarge.find("1048577") != std::string::npos);
        QVERIFY(tooLarge.find("at least") != std::string::npos);

        const std::string badLimit = describePolicyFailure(
                PolicyFailure{PolicyError::InvalidByteLimit, RequestClass::JsonApi, 0, 0});
        QVERIFY(badLimit.find("studio API") != std::string::npos);
        QVERIFY(badLimit.find('0') != std::string::npos);

        QVERIFY(tooLarge != badLimit);

        const std::string upload = describePolicyFailure(
                PolicyFailure{PolicyError::ResponseTooLarge, RequestClass::Upload, 1'048'576, 1'048'577});
        QVERIFY(upload.find("audio upload") != std::string::npos);
        QVERIFY(upload != tooLarge);
    }

    // ── Retry-After / HTTP-date ─────────────────────────────────────────────

    void retryAfterReadsAnImfFixdateExactly()
    {
        // RFC 9110 5.6.7 IMF-fixdate, which is also RFC 2822's own example --
        // the exact string Qt's Qt::RFC2822Date parser returns an INVALID
        // QDateTime for on this Qt. So this one fixture is both the correctness
        // assertion and the justification for the hand-rolled parser.
        QCOMPARE(http::parseRetryAfter(QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37 GMT"),
                                       kRfcExampleEpochSecs)
                     .value_or(-1),
                 std::int64_t{0});

        // And the arithmetic, not just the parse: the same header seen from 120 s
        // earlier means "wait 120 s".
        QCOMPARE(http::parseRetryAfter(QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37 GMT"),
                                       kRfcExampleEpochSecs - 120)
                     .value_or(-1),
                 std::int64_t{120});

        // 784041777 + 70000 == 784111777; a one-digit slip in the fixture constant
        // must not pass silently.
        QCOMPARE(http::parseRetryAfter(QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37 GMT"),
                                       784'041'777)
                     .value_or(-1),
                 std::int64_t{70'000});
    }

    void retryAfterHonoursANumericZoneOffset()
    {
        // NOTE this is a test of OUR parser, not evidence about Qt. Measured on
        // Qt 6.11.1, Qt::RFC2822Date also gets these right -- see the correction
        // recorded in HttpPolicy.hpp. The claim that it "silently ignores" a
        // numeric offset is false; what it cannot read is the *named* zone, which
        // is the form real servers send. Pinned anyway because our parser has to
        // keep getting it right regardless of what Qt does.
        QCOMPARE(http::parseRetryAfter(QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37 +0100"),
                                       kRfcExampleEpochSecs - 3600)
                     .value_or(-1),
                 std::int64_t{0});
        QCOMPARE(http::parseRetryAfter(QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37 -0100"),
                                       kRfcExampleEpochSecs + 3600)
                     .value_or(-1),
                 std::int64_t{0});
    }

    void retryAfterReadsDeltaSeconds_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<std::int64_t>("expectedSecs");
        QTest::newRow("120") << QByteArrayLiteral("120") << std::int64_t{120};
        QTest::newRow("zero") << QByteArrayLiteral("0") << std::int64_t{0};
        QTest::newRow("padded") << QByteArrayLiteral("  30  ") << std::int64_t{30};
        QTest::newRow("an-hour") << QByteArrayLiteral("3600") << std::int64_t{3600};
    }

    void retryAfterReadsDeltaSeconds()
    {
        QFETCH(QByteArray, header);
        QFETCH(std::int64_t, expectedSecs);
        QCOMPARE(http::parseRetryAfter(header, kRfcExampleEpochSecs).value_or(-1),
                 expectedSecs);
    }

    void retryAfterFallsBackRatherThanGuessingOrCrashing_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("blank") << QByteArray("   ");
        QTest::newRow("prose") << QByteArrayLiteral("soon");
        QTest::newRow("trailing-unit") << QByteArrayLiteral("5s");
        QTest::newRow("negative") << QByteArrayLiteral("-5");
        QTest::newRow("overflowing") << QByteArrayLiteral("99999999999999999999999");
        QTest::newRow("no-weekday") << QByteArrayLiteral("06 Nov 1994 08:49:37 GMT");
        QTest::newRow("rfc850-form") << QByteArrayLiteral("Sunday, 06-Nov-94 08:49:37 GMT");
        QTest::newRow("no-zone") << QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37");
        QTest::newRow("unknown-zone") << QByteArrayLiteral("Sun, 06 Nov 1994 08:49:37 EST");
        QTest::newRow("bad-month") << QByteArrayLiteral("Sun, 06 Nop 1994 08:49:37 GMT");
    }

    void retryAfterFallsBackRatherThanGuessingOrCrashing()
    {
        QFETCH(QByteArray, header);
        // std::nullopt, never a guess and never an exception: a rate-limit hint
        // that cannot be understood must hand the decision back to the backoff
        // ladder rather than invent a delay.
        QVERIFY(!http::parseRetryAfter(header, kRfcExampleEpochSecs).has_value());
    }

    void retryAfterRejectsOutOfRangeFields_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<QString>("why");
        // QTime accepts an hour of 99 and QDateTime turns it into a real instant,
        // so an unchecked field yields a plausible wrong time rather than an
        // invalid one. RFC 9110's time-hour is 00-23.
        QTest::newRow("hour-99") << QByteArrayLiteral("Sun, 06 Nov 1994 99:49:37 GMT")
                                 << QStringLiteral("hour");
        QTest::newRow("hour-24") << QByteArrayLiteral("Sun, 06 Nov 1994 24:49:37 GMT")
                                 << QStringLiteral("hour");
        QTest::newRow("minute-99") << QByteArrayLiteral("Sun, 06 Nov 1994 08:99:37 GMT")
                                   << QStringLiteral("minute");
        // time-second is 00-60; 60 is a leap second and is passed through, 61 is not.
        QTest::newRow("second-61") << QByteArrayLiteral("Sun, 06 Nov 1994 08:49:61 GMT")
                                   << QStringLiteral("second");
        QTest::newRow("day-99") << QByteArrayLiteral("Sun, 99 Nov 1994 08:49:37 GMT")
                                << QStringLiteral("day");
        QTest::newRow("day-40") << QByteArrayLiteral("Sun, 40 Nov 1994 08:49:37 GMT")
                                << QStringLiteral("day");
    }

    void retryAfterRejectsOutOfRangeFields()
    {
        QFETCH(QByteArray, header);
        QFETCH(QString, why);
        Q_UNUSED(why)
        QVERIFY(!http::parseRetryAfter(header, kRfcExampleEpochSecs).has_value());
    }

    void aLeapSecondRetryAfterIsNeverReadAsMidnight()
    {
        // RFC 9110's time-second is 00-60, and 60 means a leap second. The parser
        // in src/suno/DownloadQueue.cpp intends to pass it through -- its own
        // comment claims "60 for a leap second, which QTime accepts and which we
        // pass through" -- but that claim is FALSE on this Qt, measured:
        //
        //   QTime(8,49,60).isValid()  -> false
        //   QDateTime(QDate(1994,11,6), QTime(8,49,60), UTC).isValid() -> TRUE
        //   ...and its toSecsSinceEpoch() is 784080000, i.e. MIDNIGHT that day.
        //
        // So the explicit range check lets 60 through, QTime rejects it,
        // QDateTime papers over the invalid time with midnight, and the caller is
        // handed a delay computed from the wrong instant -- here a NEGATIVE
        // number, which retryDelayMs then clamps to its 1 s floor. A server that
        // sent a leap-second Retry-After is read as "retry immediately".
        //
        // This asserts the invariant rather than one particular remedy, so it
        // holds whichever fix lands: accepting the leap second yields 60, and
        // rejecting it (QTime genuinely cannot represent 60, so falling back to
        // the ladder is a safe reading of an unreachable value) yields no value
        // at all. Reading the substituted midnight is neither. RED against the
        // current copy; the one-line fix is in the W5-C report.
        constexpr std::int64_t kSubstitutedMidnight = 784080000;
        const std::int64_t now = kRfcExampleEpochSecs - 60;

        const auto result = http::parseRetryAfter(
                QByteArrayLiteral("Sun, 06 Nov 1994 08:49:60 GMT"), now);

        if (result.has_value()) {
            QVERIFY(*result != kSubstitutedMidnight - now);   // never the substituted instant
            QCOMPARE(*result, std::int64_t{60});             // the real one
        }
    }
};

#include "test_HttpPolicy.moc"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestHttpPolicy test;
    return QTest::qExec(&test, argc, argv);
}