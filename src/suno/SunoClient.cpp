#include "SunoClient.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include "core/Logger.hpp"

#include <QTimer>
#include <deque>

namespace vc::suno {

SunoClient::SunoClient(QObject* parent)
    : QObject(parent),
      manager_(new QNetworkAccessManager(this)),
      queueTimer_(new QTimer(this)) {
    queueTimer_->setInterval(1000);
    connect(queueTimer_, &QTimer::timeout, this, &SunoClient::processQueue);
}

SunoClient::~SunoClient() = default;

void SunoClient::processQueue() {
    if (requestQueue_.empty()) {
        queueTimer_->stop();
        return;
    }

    auto pending = requestQueue_.front();
    requestQueue_.pop_front();

    QNetworkReply* reply;
    if (pending.method == "POST") {
        reply = manager_->post(pending.request, pending.data);
    } else {
        reply = manager_->get(pending.request);
    }

    if (pending.callback) {
        connect(reply, &QNetworkReply::finished, [reply, pending]() {
            pending.callback(reply);
        });
    }

    if (requestQueue_.empty()) {
        queueTimer_->stop();
    }
}

void SunoClient::setToken(const std::string& token) {
    token_ = token;
}

void SunoClient::setCookie(const std::string& cookie) {
    cookie_ = cookie;

    QRegularExpression re("__session[^=]*=([^;]+)");
    QRegularExpressionMatch match = re.match(QString::fromStdString(cookie_));
    
    if (match.hasMatch()) {
        std::string potentialToken = match.captured(1).toStdString();
        if (potentialToken.starts_with("eyJ")) {
            token_ = potentialToken;
            LOG_INFO("SunoClient: Extracted JWT from session cookie");
            
            std::string sid = extractSidFromToken(token_);
            if (!sid.empty()) {
                clerkSid_ = sid;
                LOG_INFO("SunoClient: Extracted Clerk SID from JWT: {}", clerkSid_);
            }
        }
    }
}

std::string SunoClient::extractSidFromToken(const std::string& token) {
    QString qtoken = QString::fromStdString(token);
    QStringList parts = qtoken.split('.');
    if (parts.size() < 2) return "";
    
    QByteArray payload = QByteArray::fromBase64(parts[1].toUtf8(), QByteArray::Base64UrlEncoding);
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isNull() && doc.isObject()) {
        return doc.object()["sid"].toString().toStdString();
    }
    return "";
}

bool SunoClient::isAuthenticated() const {
    return !token_.empty() || !cookie_.empty();
}

void SunoClient::refreshAuthToken(std::function<void(bool)> callback) {
    if (cookie_.empty()) {
        if (callback)
            callback(false);
        return;
    }

    if (clerkSid_.empty()) {
        QString url = QString("%1/client?_is_native=true&_clerk_js_version=%2")
                              .arg(CLERK_BASE)
                              .arg(QString::fromStdString(clerkVersion_));
        QNetworkRequest req((QUrl(url)));
        req.setRawHeader("Cookie", QString::fromStdString(cookie_).toUtf8());
        req.setRawHeader(
                "User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

        QNetworkReply* reply = manager_->get(req);
        connect(reply,
                &QNetworkReply::finished,
                this,
                [this, reply, callback]() {
                    reply->deleteLater();
                    if (reply->error() == QNetworkReply::NoError) {
                        QJsonDocument doc =
                                QJsonDocument::fromJson(reply->readAll());
                        
                        QJsonObject resp = doc.object()["response"].toObject();
                        std::string sid = resp["last_active_session_id"].toString().toStdString();
                        
                        if (sid.empty()) {
                            QJsonArray sessions = resp["sessions"].toArray();
                            if (!sessions.isEmpty()) {
                                sid = sessions[0].toObject()["id"].toString().toStdString();
                            }
                        }

                        clerkSid_ = sid;
                        if (!clerkSid_.empty()) {
                            refreshAuthToken(callback);
                        } else {
                            LOG_ERROR("SunoClient: Failed to extract Clerk SID from response");
                            if (callback)
                                callback(false);
                        }
                    } else {
                        LOG_ERROR("SunoClient: Clerk Session ID request failed: {}",
                                  reply->errorString().toStdString());
                        if (callback)
                            callback(false);
                    }
                });
        return;
    }

    QString url = QString("%1/client/sessions/%2/tokens?_is_native=true&_clerk_js_version=%3")
                          .arg(CLERK_BASE)
                          .arg(QString::fromStdString(clerkSid_))
                          .arg(QString::fromStdString(clerkVersion_));
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Cookie", QString::fromStdString(cookie_).toUtf8());
    req.setRawHeader(
            "User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    QNetworkReply* reply = manager_->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            token_ = doc.object()["jwt"].toString().toStdString();
            if (token_.empty()) {
                token_ = doc.object()["response"].toObject()["jwt"].toString().toStdString();
            }
            
            if (!token_.empty()) {
                LOG_INFO("SunoClient: Refreshed auth token ({}...)", token_.substr(0, 10));
            } else {
                LOG_ERROR("SunoClient: JWT missing in refresh response");
            }
            
            if (callback)
                callback(!token_.empty());
        } else {
            LOG_ERROR("SunoClient: Clerk Token request failed: {}",
                      reply->errorString().toStdString());
            if (callback)
                callback(false);
        }
    });
}

QNetworkRequest SunoClient::createRequest(const QString& endpoint) {
    QUrl url;
    if (endpoint.startsWith("http")) {
        url = QUrl(endpoint);
    } else {
        url = QUrl(API_BASE + endpoint);
    }
    QNetworkRequest request(url);
    if (!token_.empty()) {
        request.setRawHeader(
                "Authorization",
                QString::fromStdString("Bearer " + token_).toUtf8());
    }
    if (!cookie_.empty()) {
        request.setRawHeader("Cookie",
                             QString::fromStdString(cookie_).toUtf8());
    }
    request.setRawHeader(
            "User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    return request;
}

void SunoClient::enqueueRequest(const QNetworkRequest& req,
                               const std::string& method,
                               const QByteArray& data,
                               std::function<void(QNetworkReply*)> callback) {
    requestQueue_.push_back({req, method, data, callback});
    if (!queueTimer_->isActive()) {
        queueTimer_->start();
    }
}

void SunoClient::fetchLibrary(int page) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }

    auto proceed = [this, page] {
        QString url = QString("/feed/v2?page=%1").arg(page);
        enqueueRequest(createRequest(url), "GET", {}, [this](QNetworkReply* reply) {
            onLibraryReply(reply);
        });
    };

    if (token_.empty() && !cookie_.empty()) {
        refreshAuthToken([this, proceed](bool success) {
            if (!success) {
                errorOccurred.emitSignal("Authentication refresh failed");
                return;
            }
            proceed();
        });
    } else {
        proceed();
    }
}

void SunoClient::fetchAlignedLyrics(const std::string& clipId) {
    if (!isAuthenticated())
        return;

    auto proceed = [this, clipId] {
        QString url = QString("/gen/%1/aligned_lyrics/v2/")
                              .arg(QString::fromStdString(clipId));
        enqueueRequest(createRequest(url), "GET", {}, [this, clipId](QNetworkReply* reply) {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                alignedLyricsFetched.emitSignal(clipId,
                                                reply->readAll().toStdString());
            } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
                QString lyricsUrl = QString("/lyrics/%1")
                                      .arg(QString::fromStdString(clipId));
                enqueueRequest(createRequest(lyricsUrl), "GET", {}, [this, clipId](QNetworkReply* r2) {
                    r2->deleteLater();
                    if (r2->error() == QNetworkReply::NoError) {
                        alignedLyricsFetched.emitSignal(clipId, r2->readAll().toStdString());
                    }
                });
            }
        });
    };

    if (token_.empty() && !cookie_.empty()) {
        refreshAuthToken([this, proceed](bool success) {
            if (!success)
                return;
            proceed();
        });
    } else {
        proceed();
    }
}

void SunoClient::onLibraryReply(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    std::vector<SunoClip> clips;
    QJsonArray array;

    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.isObject() && doc.object().contains("clips")) {
        array = doc.object()["clips"].toArray();
    } else if (doc.isObject() && doc.object().contains("project_clips")) {
        QJsonArray projClips = doc.object()["project_clips"].toArray();
        for (const auto& item : projClips) {
            if (item.toObject().contains("clip")) {
                array.append(item.toObject()["clip"]);
            }
        }
    }

    for (const auto& item : array) {
        QJsonObject obj = item.toObject();
        SunoClip clip;
        clip.id = obj["id"].toString().toStdString();
        clip.title = obj["title"].toString().toStdString();
        clip.audio_url = obj["audio_url"].toString().toStdString();
        clip.image_url = obj["image_url"].toString().toStdString();
        clip.status = obj["status"].toString().toStdString();

        QJsonObject meta = obj["metadata"].toObject();
        clip.metadata.prompt = meta["prompt"].toString().toStdString();
        clip.metadata.tags = meta["tags"].toString().toStdString();
        clip.metadata.lyrics = meta["lyrics"].toString().toStdString();
        clip.metadata.type = meta["type"].toString().toStdString();

        clips.push_back(clip);
    }

    libraryFetched.emitSignal(clips);
}

void SunoClient::fetchProjects(int page) {
    // Skeleton implementation
}

void SunoClient::onProjectsReply(QNetworkReply* reply) {
    reply->deleteLater();
    // Skeleton implementation
}

void SunoClient::fetchProject(const std::string& projectId, int page) {
    // Skeleton implementation
}

void SunoClient::handleNetworkError(QNetworkReply* reply) {
    std::string err = reply->errorString().toStdString();
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() ==
        401) {
        err = "Unauthorized: Token expired or invalid";
        token_.clear(); // Clear token so next call attempts refresh
    }
    errorOccurred.emitSignal(err);
    LOG_ERROR("SunoClient API Error: {}", err);
}

} // namespace vc::suno
