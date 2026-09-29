// FakeNetworkReply.hpp — a scriptable QNetworkReply stand-in shared by the
// suno unit suites that need to drive a transfer to completion.
//
// Nothing here touches the network: the queue's injectable ReplyFactory seam
// (src/suno/DownloadQueue.hpp:76) hands one of these back instead of asking a
// QNetworkAccessManager, and the test calls succeed()/fail()/openStream()
// directly. DownloadQueue's own suite grew the first copy of this; it has not
// been migrated yet, so there are two implementations in the tree — see
// TODO.md. New suites should include this header rather than grow a third.
//
// Header-only and inline: it is test scaffolding, not library code.

#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>

#include <cstring>
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

    void abort() override {
        if (finishing_) return;
        finishing_ = true;
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
    void succeed(QByteArray body, int httpStatus = 200) {
        openStream(body, httpStatus);
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
