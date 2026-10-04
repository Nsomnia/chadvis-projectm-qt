#pragma once
// HttpPolicy.hpp — the ONE owner of this tree's HTTP request policy.
//
// ── Why this file exists ────────────────────────────────────────────────────
// Found 2026-10-04 by grep: `src/suno/DownloadQueue` had been hardened to set a
// transfer timeout, check `write()`'s return, cap per-item bytes, and hand-parse
// `Retry-After` — and *nowhere else in the tree did any of that*. Five other
// sites still sent unbounded requests. The defect was never "someone forgot a
// timeout"; it was that no single file owned the policy, so a comment at each
// call site saying "add a timeout here" would have reproduced exactly this in
// six months. One owner is the fix; a checklist is not.
//
// The specific consequence for a credential client: SunoClient serialises its
// authenticated requests, so one hung endpoint holds a slot *forever* and the
// whole signed-in surface wedges. That is not a slow request, it is a
// permanently stuck client.
//
// ── Why the timeout API is usable at all ────────────────────────────────────
// `QNetworkRequest::setTransferTimeout` is an **idle/stall** deadline, not a
// total-transfer budget. Qt's own wording (doc.qt.io/qt-6/qnetworkrequest.h,
// "since 6.7"): *"Transfers are aborted if no bytes are transferred before the
// timeout expires"* — the timer restarts on every chunk. That distinction is the
// entire reason setting it on a media download is safe and would be *wrong* as a
// wall-clock cap: a 200 MB clip on a slow link may legitimately take ten
// minutes of elapsed time and must not be killed for it, whereas a socket that
// has gone quiet for 30 s is dead. It is also why the numbers below can differ
// per request class without any of them being a transfer-size budget in disguise.
//
// Two further measured facts about this Qt (6.11.1), both load-bearing:
//  * "If this function is not called, the timeout is **disabled** and has the
//    value zero." So the absence of a timeout is not a conservative default —
//    Qt's `DefaultTransferTimeoutConstant` (30000) is only applied when the
//    setter is *called with no argument*. A request that never calls the setter
//    waits forever, which is precisely the state this tree was in.
//  * Qt ships a related-but-different guard, `setDecompressedSafetyCheckThreshold`
//    (default 10 MiB). Its own documentation states it "only detects responses
//    with unusually high compression ratios. It does not impose an absolute
//    limit on the total decompressed output size", and explicitly advises
//    applications to "monitor QNetworkReply::bytesAvailable() ... and abort
//    transfers that exceed an acceptable size." BodyReader below is that
//    monitoring, and it is not redundant with that knob.

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

class QIODevice;

namespace vc::suno::http {

// QNetworkRequest::setTransferTimeout / transferTimeoutAsDuration are "since 6.7",
// and cmake/Dependencies.cmake:12 pins no Qt minimum. Stating the real floor
// here turns a silent "no timeout at all" on an older Qt into a configure-time
// complaint naming the reason. (DownloadQueue.cpp:409 already calls the setter
// unguarded, so the floor was in fact already required — this just says so.)
static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0),
              "vc::suno::http needs Qt >= 6.7 for QNetworkRequest::setTransferTimeout. "
              "Older than that, an unset transfer timeout means NO timeout rather than "
              "Qt's 30000 ms default, so the stall guard this file exists to provide "
              "would be silently absent. Pin find_package(Qt6 6.7 REQUIRED ...).");

/// Which policy applies. Named by *what the request is*, not by which subsystem
/// happens to make it, so a route moving between services cannot silently change
/// its timeout.
enum class RequestClass {
    /// Credential exchanges (`auth.suno.com` Clerk envelopes).
    Auth,
    /// The captured `/api/*` studio surface: small JSON documents.
    JsonApi,
    /// A clip or track body from a captured media origin.
    MediaDownload,
    /// A multipart POST of a local file to the captured storage origin.
    Upload,
};

/// Stall deadline in milliseconds. **Not** a wall-clock budget — see the file
/// header. Every value is asserted exactly by tests/unit/suno/test_HttpPolicy.cpp.
[[nodiscard]] constexpr int transferTimeoutMs(const RequestClass requestClass) noexcept
{
    switch (requestClass) {
        case RequestClass::Auth:
        case RequestClass::JsonApi:
            // 30 s == QNetworkRequest::DefaultTransferTimeoutConstant, and it is
            // Qt's own number rather than an invented one. These bodies are small
            // server-computed documents on a TLS connection to one origin; there
            // is no legitimate 30 s in which a single JSON response exchanges no
            // bytes at all. Any such gap is a dead socket or a wedged origin.
            // This is also the number that bounds the wedge the brief describes:
            // a serialised request slot is back in the pool inside 30 s instead of
            // never.
            return 30'000;
        case RequestClass::MediaDownload:
            // Same 30 s, and deliberately identical to
            // DownloadQueue::kDefaultTransferTimeoutMs — a static_assert in the
            // .cpp pins the two, because two owners disagreeing about one number
            // is the bug this file was written to end. Safe for media precisely
            // because it is a stall deadline: a slow-but-progressing 200 MB
            // transfer restarts the timer on every chunk.
            return 30'000;
        case RequestClass::Upload:
            // 60 s, double the others, for the one direction where long silent
            // gaps are real: a large multipart POST over a slow uplink. A 100 MB
            // upload on a 10 Mbit line has no legitimate minute with zero bytes
            // leaving the socket, but a congested one can stall far longer than a
            // download can — TCP back-pressure fills buffers and then waits. Note
            // what this does NOT buy: an upload that trickles one byte per 59 s
            // still runs forever, which is why the response *cap* and the
            // `write()`-return check matter and are not optional extras.
            return 60'000;
    }
    return 30'000;
}

/// Ceiling on one response body, in bytes. Zero is **not** a valid value and
/// never means "unlimited" — see `isUsableByteLimit` and `makeBodyReader`.
[[nodiscard]] constexpr qint64 maxResponseBytes(const RequestClass requestClass) noexcept
{
    switch (requestClass) {
        case RequestClass::Auth:
            // 1 MiB for the Clerk client envelope. That envelope carries a full
            // session list with a JWT per session; at ~1.5 KB per token, 1 MiB is
            // roughly 60 sessions. It never fires on a real envelope and it caps
            // the one reply that gates whether the user is signed in at all.
            return 1LL * 1024 * 1024;
        case RequestClass::JsonApi:
            // 16 MiB. The largest JSON this client reads is a library or Explore
            // page carrying per-clip metadata plus aligned per-word lyrics; a
            // 50-clip page with lyrics lands around 1–2 MiB, so this is roughly
            // an order of magnitude of headroom on the biggest legitimate body
            // and still bounds a hostile origin to 16 MiB per reply instead of
            // the 2 GB an unbounded readAll() will happily allocate.
            return 16LL * 1024 * 1024;
        case RequestClass::MediaDownload:
            // 256 MiB — the same figure as
            // DownloadQueue::kDefaultMaxBytesPerItem, for the same reason (a
            // static_assert pins them). Not consumed by BodyReader, which buffers;
            // DownloadQueue streams to a scratch file and enforces it per chunk.
            // The value exists here so the number has one home.
            return 256LL * 1024 * 1024;
        case RequestClass::Upload:
            // 1 MiB. The captured storage POST answers 204 with an empty body;
            // an S3 multipart error is a short XML document. There is nothing
            // legitimate here that approaches a megabyte.
            return 1LL * 1024 * 1024;
    }
    return 1LL * 1024 * 1024;
}

/// A cap must be strictly positive and must fit a 32-bit `qsizetype`, because
/// QByteArray's length is `qsizetype` (qint32 on the 32-bit builds this project
/// still has to configure for). A cap above that cannot be honoured — it would
/// truncate silently — so it is rejected rather than clamped.
inline constexpr qint64 kMaxSaneByteLimit = 2LL * 1024 * 1024 * 1024;

[[nodiscard]] constexpr bool isUsableByteLimit(const qint64 capBytes) noexcept
{
    return capBytes > 0 && capBytes <= kMaxSaneByteLimit;
}

[[nodiscard]] constexpr std::string_view requestClassName(const RequestClass requestClass) noexcept
{
    switch (requestClass) {
        case RequestClass::Auth: return "Clerk auth";
        case RequestClass::JsonApi: return "studio API";
        case RequestClass::MediaDownload: return "media download";
        case RequestClass::Upload: return "audio upload";
    }
    return "unknown request class";
}

/// Stamp the class's transfer timeout onto a request.
///
/// Deliberately does **not** touch RedirectPolicyAttribute. The manual-redirect
/// policy is uniform in the tree, but it is uniform for a *security* reason — it
/// exists so a redirect can be re-validated against the captured-host allowlist
/// before any cookie or bearer is sent to the new host (AGENTS.md §1, "fail
/// closed on unverified hosts") — and the verification lives in each caller's
/// redirect handler, not here. Folding it in would make this function look like
/// it owned that check while silently providing none of it.
void applyRequestPolicy(QNetworkRequest& request, RequestClass requestClass);

/// Named refusals. Distinct values with distinct prose, because "network error"
/// tells a user nothing and a future reader nothing either.
enum class PolicyError {
    None,
    /// More bytes arrived than the class's cap allows. Refused *during* the read.
    ResponseTooLarge,
    /// The cap itself is unusable (zero, negative, or beyond a 32-bit qsizetype).
    /// Never silently reinterpreted as "unlimited".
    InvalidByteLimit,
};

/// Named, and returning `const char*` deliberately: Qt's QCOMPARE finds a
/// `toString` overload by ADL and requires exactly that return type, so an enum
/// with a `std::string_view`-returning toString cannot be compared in a test at
/// all. (The codebase convention, e.g. vc::suno::toString(DownloadState), is
/// `const char*` for exactly this reason.)
[[nodiscard]] constexpr const char* toString(const PolicyError error) noexcept
{
    switch (error) {
        case PolicyError::None: return "none";
        case PolicyError::ResponseTooLarge: return "response-too-large";
        case PolicyError::InvalidByteLimit: return "invalid-byte-limit";
    }
    return "unknown";
}

struct PolicyFailure {
    PolicyError error{PolicyError::None};
    RequestClass requestClass{RequestClass::JsonApi};
    /// The limit that was breached or rejected. Always populated, for both
    /// errors, so a diagnostic can always name its number.
    qint64 limitBytes{0};
    /// ResponseTooLarge only: the *minimum* number of bytes observed. The reader
    /// stops at the cap, so this is `limitBytes + 1` and never a count of the
    /// whole body — the point of stopping is that we do not know how big it was.
    qint64 observedBytes{0};
};

/// The user-facing sentence: names which request, which limit, and — for an
/// overflow — that the count is a lower bound rather than a total.
[[nodiscard]] std::string describePolicyFailure(const PolicyFailure& failure);

[[nodiscard]] inline QString describePolicyFailureText(const PolicyFailure& failure)
{
    return QString::fromStdString(describePolicyFailure(failure));
}

/// Bounded, incremental body reader.
///
/// The contract that matters: `body()` never exceeds `capBytes()`, and the
/// refusal happens on the read that crosses it rather than after the fact. A
/// post-hoc `readAll().size() > limit` check is not a cap — the multi-megabyte
/// allocation has already happened by the time you look, and a hostile origin
/// that never stops sending gets to choose your heap size. Each `pump()` reads at
/// most `min(remaining + 1, kProbeChunkBytes)` bytes, so the single byte of
/// overflow is *detected* without ever being *stored*, and the underlying device
/// is left positioned exactly one byte past the cap.
///
/// `pump()` is idempotent and cheap to call repeatedly, which is what lets a
/// caller drive it from `readyRead` and abort a reply mid-stream instead of
/// discovering the overrun after the transfer has already completed.
class BodyReader {
public:
    /// Bounded read size. 16 KiB keeps the probe off the large-object heap while
    /// still amortising a syscall over a useful run of bytes.
    static constexpr qint64 kProbeChunkBytes = 16 * 1024;

    /// Default-constructed readers hold `capBytes() == 0` and therefore refuse:
    /// a reader nobody configured must not read without a limit.
    BodyReader() = default;
    BodyReader(RequestClass requestClass, qint64 capBytes);

    /// Drain whatever the device currently offers. Success means "nothing more
    /// is available right now"; call again on the next `readyRead`.
    std::expected<void, PolicyFailure> pump(QIODevice& device);

    /// Sticky refusal. Set by `pump()` on every failure and never cleared, so a
    /// caller that aborts a reply mid-stream can still recover *which* limit was
    /// breached once the abort lands in `finished()` -- at which point the device
    /// reads as drained and a fresh `pump()` would report success. Without this,
    /// the one diagnostic that matters is the one lost to the teardown.
    [[nodiscard]] const std::optional<PolicyFailure>& failure() const { return failure_; }

    [[nodiscard]] const QByteArray& body() const { return body_; }
    [[nodiscard]] qint64 capBytes() const { return capBytes_; }
    [[nodiscard]] RequestClass requestClass() const { return requestClass_; }

private:
    [[nodiscard]] PolicyFailure refuseLimit() const;
    [[nodiscard]] PolicyFailure refuseOverflow() const;

    QByteArray body_;
    RequestClass requestClass_{RequestClass::JsonApi};
    qint64 capBytes_{0};
    std::optional<PolicyFailure> failure_;
};

/// Validated construction. A `capBytes` of 0, negative, or larger than a 32-bit
/// `qsizetype` can hold is an **error**, never a silent "unlimited". That is the
/// whole point of the constructor existing separately from the class body: the
/// invalid-cap failure has to be reportable with the number that was wrong, and
/// a default-constructed or implicitly-zeroed reader must still refuse on
/// `pump()` rather than read without a bound.
[[nodiscard]] std::expected<BodyReader, PolicyFailure> makeBodyReader(RequestClass requestClass,
                                                                     qint64 capBytes);

/// Convenience: build a reader at the class's own cap and drain the device in
/// one call. For a reply that has already finished, this is all the bounding a
/// caller can still do — it stops *our* retention and names the overflow, but
/// Qt has already buffered the transfer internally. Replies this code creates
/// should instead connect `readyRead` to `BodyReader::pump` and abort, which is
/// the version that actually prevents the bytes arriving at all.
[[nodiscard]] std::expected<QByteArray, PolicyFailure> readBodyBounded(QIODevice& device,
                                                                       RequestClass requestClass);

/// RFC 9110 `Retry-After`: delta-seconds or an HTTP-date. `std::nullopt` means
/// "unparseable, fall back to the backoff ladder" — never a guess, never an
/// exception. `nowEpochSecs` is injected so the date form is testable without
/// freezing a clock.
///
/// **This is a forwarder, not a second implementation.** The parsing lives in
/// `vc::suno::parseRetryAfter` (src/suno/DownloadQueue.cpp) and was written and
/// measured first; it is already exported from `suno/DownloadQueue.hpp`. Rather
/// than paste a third copy into this file, the body below delegates to it. The
/// follow-up extraction — move the implementation here and delete the original —
/// is written out in the W5-C report and is the only thing that makes this file
/// the sole owner; until then this declaration exists so callers have ONE place
/// to reach for, and so the tests pin the behaviour where it is documented.
///
/// Why not `QDateTime::fromString(..., Qt::RFC2822Date)`, measured on this Qt
/// 6.11.1 rather than assumed:
///  * `QDateTime::fromString("Sun, 06 Nov 1994 08:49:37 GMT", Qt::RFC2822Date)`
///    returns an **invalid** QDateTime — and so do the "UT" and "UTC" spellings.
///    That string is simultaneously RFC 2822's own example date and the
///    IMF-fixdate form RFC 9110 §5.6.7 *mandates* recipients accept. Every real
///    `Retry-After: <HTTP-date>` carries a named zone, so the one spelling that
///    matters is the one Qt cannot read.
///  * CORRECTION to the claim recorded at DownloadQueue.cpp:138-145: it says Qt
///    "silently ignores" a numeric offset. It does not. Measured here,
///    `"Sun, 06 Nov 1994 08:49:37 +0100"` parses to 784108177 and `-0500` to
///    784129777, both exactly correct. The original evidence compared against
///    `QDateTime::fromString("2026-01-01T00:00:00", ISODate).toUTC()`, which is a
///    *local-time* parse -- it was measuring the machine's UTC offset, not a Qt
///    bug. Numeric offsets work; named zones do not. One of the two facts that
///    justified hand-parsing was wrong, and the other is sufficient on its own.
[[nodiscard]] std::optional<std::int64_t> parseRetryAfter(const QByteArray& headerValue,
                                                          std::int64_t nowEpochSecs);

} // namespace vc::suno::http