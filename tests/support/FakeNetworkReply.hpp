// FakeNetworkReply.hpp — a scriptable QNetworkReply stand-in shared by the
// suno unit suites that need to drive a transfer to completion.
//
// Nothing here touches the network: the queue's injectable ReplyFactory seam
// (src/suno/DownloadQueue.hpp:76) hands one of these back instead of asking a
// QNetworkAccessManager, and the tests call succeed()/fail()/openStream()/
// respond() directly. This is the one implementation: every suno suite that
// needs a fake reply includes this header (via the test_support INTERFACE
// library) instead of defining a private copy.
//
// Header-only and inline: it is test scaffolding, not library code. The class
// deliberately carries no Q_OBJECT macro — QNetworkReply supplies the moc
// machinery, and a Q_OBJECT in an unnamed namespace can defeat moc and link
// with an undefined vtable.
//
// The suite-facing drivers are the union of what the former copies offered:
//   * succeed()/fail()/openStream() — the DownloadQueue seam shape.
//   * respond(body, status, retryAfter) — the SunoClient factory shape: a
//     guarded single-shot completion that leaves error() as NoError, so the
//     status alone drives the client's classification.
//   * ctor(request, method) — POST-aware: SunoClient chooses the operation,
//     so a fake that always reports GetOperation misrepresents POST retries.
//   * wasAborted() — the RequestEpoch abort probe.

#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fake_net {

/// Emits readyRead/downloadProgress/finished on demand, in the order
/// QNetworkAccessManager would.
class FakeReply : public QNetworkReply {
public:
    explicit FakeReply(const QNetworkRequest& request) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setOpenMode(ReadOnly | Unbuffered);
    }

    /// POST-aware: `method == "POST"` reports PostOperation, anything else
    /// reports GetOperation.
    FakeReply(const QNetworkRequest& request, const std::string& method) {
        setRequest(request);
        setUrl(request.url());
        setOperation(method == "POST" ? QNetworkAccessManager::PostOperation
                                      : QNetworkAccessManager::GetOperation);
        setOpenMode(ReadOnly | Unbuffered);
    }

    void abort() override {
        if (finishing_) return;
        finishing_ = true;
        aborted_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        emit finished();
    }

    qint64 bytesAvailable() const override {
        return (payload_.size() - offset_) + QNetworkReply::bytesAvailable();
    }

    qint64 readData(char* data, qint64 maxLen) override {
        if (offset_ >= payload_.size()) return 0;
        const qint64 n = qMin(maxLen, static_cast<qint64>(payload_.size()) - offset_);
        std::memcpy(data, payload_.constData() + offset_, static_cast<size_t>(n));
        offset_ += n;
        return n;
    }

    // ── test drivers ──

    /// Complete successfully: deliver everything, then finish.
    ///
    /// Streaming, like the real QNAM: `readyRead` fires before `finished`, which
    /// is what a consumer that writes bytes as they arrive needs (the downloader
    /// suite pipes its payload to a file). A suite whose reply is tracked by
    /// `SunoClient` cannot use this and then read the reply itself — the armed
    /// BodyReader drains it on `readyRead`. Use complete() there.
    void succeed(QByteArray body, int httpStatus = 200) {
        openStream(body, httpStatus);
        finishing_ = true;
        emit finished();
    }

    /// Complete in one shot: the payload is present for a final drain, but no
    /// `readyRead` fires, so a reply tracked by `SunoClient` is not drained
    /// before the test's own callback can read it (T0099: test_SunoRequestEpoch
    /// reads the body from inside its callback).
    void complete(QByteArray body, int httpStatus = 200) {
        setHeaders(httpStatus);
        payload_ = std::move(body);
        offset_ = 0;
        finishing_ = true;
        emit finished();
    }

    /// Fail with a network error and/or an HTTP status.
    void fail(QNetworkReply::NetworkError err, int httpStatus = 0) {
        setHeaders(httpStatus);
        setError(err, QStringLiteral("scripted failure"));
        finishing_ = true;
        emit finished();
    }

    /// Deliver bytes and stay open, so the caller can then fail() the reply or
    /// trigger abort() through the queue's cancel path.
    void openStream(QByteArray prefix, int httpStatus = 200) {
        setHeaders(httpStatus);
        payload_ = std::move(prefix);
        offset_ = 0;
        if (!payload_.isEmpty()) emit readyRead();
        emit downloadProgress(payload_.size(), payload_.size());
    }

    /// Complete with a status and an optional body. `error()` is deliberately
    /// left as NoError: that is the shape the SunoClient retry/classification
    /// logic branches on. Guarded: a second call after completion is a no-op.
    void respond(QByteArray body, int status, const QByteArray& retryAfter = {}) {
        if (finishing_) return;
        payload_ = std::move(body);
        offset_ = 0;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!retryAfter.isEmpty()) {
            setRawHeader("Retry-After", retryAfter);
        }
        finishing_ = true;
        emit finished();
    }

    [[nodiscard]] bool wasAborted() const { return aborted_; }

private:
    void setHeaders(int httpStatus) {
        setOpenMode(ReadOnly | Unbuffered);
        if (httpStatus > 0) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        }
    }

    QByteArray payload_;
    qint64 offset_ = 0;
    bool finishing_ = false;
    bool aborted_ = false;
};

/// Hands back a FakeReply per request and keeps every request and reply so a
/// test can assert on what was asked for and drive each transfer in turn.
class FakeServer {
public:
    std::vector<QNetworkRequest> requests;
    std::vector<FakeReply*> replies;

    /// Matches vc::suno::ReplyFactory. The returned lambda borrows `this`, so
    /// the FakeServer must outlive the object under test.
    [[nodiscard]] auto factory() {
        return [this](const QNetworkRequest& request) -> QNetworkReply* {
            requests.push_back(request);
            auto* reply = new FakeReply(request);
            replies.push_back(reply);
            return reply;
        };
    }

    /// Index of the first request whose path ends with `suffix`, or -1.
    [[nodiscard]] int requestIndexForPath(const QString& suffix) const {
        for (int i = 0; i < static_cast<int>(requests.size()); ++i) {
            if (requests[i].url().toString().endsWith(suffix)) return i;
        }
        return -1;
    }
};

} // namespace fake_net
