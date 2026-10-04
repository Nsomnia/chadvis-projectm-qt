#include "HttpPolicy.hpp"

#include "suno/DownloadQueue.hpp"

#include <QIODevice>

#include <algorithm>
#include <array>
#include <format>

namespace vc::suno::http {

namespace {

// Two owners must never disagree about one number. DownloadQueue was hardened
// first and carries its own copies of both constants for documentation; this
// assert is what stops those copies from drifting away from the policy the rest
// of the tree now uses. If it ever fires, the fix is to delete the duplicate in
// DownloadQueue.hpp, not to edit a number here.
//
// (It also proves the forwarder below is talking to the same implementation the
// DownloadQueue suite already pins.)
static_assert(transferTimeoutMs(RequestClass::MediaDownload) ==
              DownloadQueue::kDefaultTransferTimeoutMs,
              "media transfer timeout drifted from DownloadQueue::kDefaultTransferTimeoutMs");
static_assert(maxResponseBytes(RequestClass::MediaDownload) ==
              DownloadQueue::kDefaultMaxBytesPerItem,
              "media byte cap drifted from DownloadQueue::kDefaultMaxBytesPerItem");
static_assert(transferTimeoutMs(RequestClass::Auth) == 30'000 &&
                      transferTimeoutMs(RequestClass::JsonApi) == 30'000 &&
                      transferTimeoutMs(RequestClass::MediaDownload) == 30'000 &&
                      transferTimeoutMs(RequestClass::Upload) == 60'000,
              "a transfer timeout changed; the exact values are asserted in "
              "test_HttpPolicy.cpp and the reasoning lives in HttpPolicy.hpp");

// A timeout of zero does not mean "the default" to Qt -- it means no timer at
// all. Refuse to write one, so a future RequestClass with a mistyped value
// cannot quietly reintroduce the exact unbounded wait this file exists to close.
constexpr int kSmallestUsableTimeoutMs = 1'000;

} // namespace

void applyRequestPolicy(QNetworkRequest& request, const RequestClass requestClass)
{
    const int timeout = transferTimeoutMs(requestClass);
    if (timeout < kSmallestUsableTimeoutMs) {
        return;
    }
    request.setTransferTimeout(std::chrono::milliseconds(timeout));
}

std::string describePolicyFailure(const PolicyFailure& failure)
{
    switch (failure.error) {
        case PolicyError::ResponseTooLarge:
            // "at least" is load-bearing: BodyReader stops one byte past the cap
            // on purpose, so the total size of a hostile body is unknown and
            // pretending otherwise would be a lie in a diagnostic.
            return std::format("{} response exceeded the {} byte response cap "
                               "(at least {} bytes received)",
                               requestClassName(failure.requestClass),
                               failure.limitBytes, failure.observedBytes);
        case PolicyError::InvalidByteLimit:
            return std::format("{} response was given a {} byte response cap, which "
                               "is not a usable limit",
                               requestClassName(failure.requestClass),
                               failure.limitBytes);
        case PolicyError::None:
            break;
    }
    return std::format("{} request hit no policy failure", requestClassName(failure.requestClass));
}

BodyReader::BodyReader(const RequestClass requestClass, const qint64 capBytes)
    : requestClass_(requestClass), capBytes_(capBytes)
{
}

PolicyFailure BodyReader::refuseLimit() const
{
    return PolicyFailure{PolicyError::InvalidByteLimit, requestClass_, capBytes_, 0};
}

PolicyFailure BodyReader::refuseOverflow() const
{
    return PolicyFailure{PolicyError::ResponseTooLarge, requestClass_, capBytes_,
                         capBytes_ + 1};
}

std::expected<void, PolicyFailure> BodyReader::pump(QIODevice& device)
{
    if (!isUsableByteLimit(capBytes_)) {
        failure_ = refuseLimit();
        return std::unexpected(*failure_);
    }

    for (;;) {
        const qint64 held = static_cast<qint64>(body_.size());
        const qint64 remaining = capBytes_ - held;
        if (remaining <= 0) {
            // Sitting exactly on the cap is a PASS. One more byte is not, and
            // that is the only thing checked here -- which is why "exactly at the
            // limit" succeeds and "limit + 1" does not, with no off-by-one to get
            // wrong.
            if (device.bytesAvailable() <= 0) {
                return {};
            }
            failure_ = refuseOverflow();
            return std::unexpected(*failure_);
        }
        if (device.bytesAvailable() <= 0) {
            // Nothing available yet. Not an error and not EOF: the caller pumps
            // again on the next readyRead. QIODevice::read() is documented to
            // return -1 in this situation, so asking first is what keeps a
            // not-yet-arrived body from being reported as a failure.
            return {};
        }

        // Read ONE byte past the cap. That single extra byte is what makes an
        // overrun detectable, and reading it into a bounded probe rather than
        // into body_ is what makes the cap real: body_ can only ever be grown to
        // `remaining` bytes below.
        std::array<char, static_cast<std::size_t>(kProbeChunkBytes)> probe{};
        const qint64 want = std::min(remaining + 1, kProbeChunkBytes);
        const qint64 got = device.read(probe.data(), want);
        if (got <= 0) {
            // EOF or nothing further. Never an error: the transport's own error
            // is reported by the caller's reply handling, not invented here.
            return {};
        }
        if (got > remaining) {
            body_.append(probe.data(), static_cast<qsizetype>(remaining));
            failure_ = refuseOverflow();
            return std::unexpected(*failure_);
        }
        body_.append(probe.data(), static_cast<qsizetype>(got));
    }
}

std::expected<BodyReader, PolicyFailure> makeBodyReader(const RequestClass requestClass,
                                                        const qint64 capBytes)
{
    if (!isUsableByteLimit(capBytes)) {
        return std::unexpected(PolicyFailure{PolicyError::InvalidByteLimit, requestClass,
                                             capBytes, 0});
    }
    return BodyReader{requestClass, capBytes};
}

std::expected<QByteArray, PolicyFailure> readBodyBounded(QIODevice& device,
                                                         const RequestClass requestClass)
{
    auto reader = makeBodyReader(requestClass, maxResponseBytes(requestClass));
    if (!reader) {
        return std::unexpected(reader.error());
    }
    if (auto drained = reader->pump(device); !drained) {
        return std::unexpected(drained.error());
    }
    return reader->body();
}

std::optional<std::int64_t> parseRetryAfter(const QByteArray& headerValue,
                                            const std::int64_t nowEpochSecs)
{
    return vc::suno::parseRetryAfter(headerValue, nowEpochSecs);
}

} // namespace vc::suno::http