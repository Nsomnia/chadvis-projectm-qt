#include "SunoClient.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QRegularExpression>
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include "SunoLyrics.hpp"

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
    if (token_ != token) {
        token_ = token;
        tokenChanged.emitSignal(token_);
    }
}

void SunoClient::setCookie(const std::string& cookie) {
    cookie_ = cookie;
    clerkSid_.clear();

    QMap<QString, QString> cookieMap;
    QStringList parts = QString::fromStdString(cookie).split(';');
    for (const QString& part : parts) {
        QString trimmed = part.trimmed();
        int eqPos = trimmed.indexOf('=');
        if (eqPos > 0) {
            QString name = trimmed.left(eqPos);
            QString value = trimmed.mid(eqPos + 1);
            cookieMap[name] = value;
        }
    }

    QString sessionValue;
    if (cookieMap.contains("__session")) {
        sessionValue = cookieMap["__session"];
    } else if (cookieMap.contains("__session_Jnxw-muT")) {
        sessionValue = cookieMap["__session_Jnxw-muT"];
    } else {
        for (auto it = cookieMap.begin(); it != cookieMap.end(); ++it) {
            if (it.key().startsWith("__session")) {
                sessionValue = it.value();
                break;
            }
        }
    }
    
    if (!sessionValue.isEmpty() && sessionValue.startsWith("eyJ")) {
        std::string newToken = sessionValue.toStdString();
        if (token_ != newToken) {
            token_ = newToken;
            tokenChanged.emitSignal(token_);
            LOG_INFO("SunoClient: Extracted JWT from __session cookie");
        }
        
        std::string sid = extractSidFromToken(token_);
        if (!sid.empty()) {
            clerkSid_ = sid;
            LOG_INFO("SunoClient: Extracted Clerk SID from JWT: {}", clerkSid_);
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
        if (callback) callback(false);
        return;
    }

    if (clerkSid_.empty()) {
        QString url = QString("%1/client?_is_native=true&_clerk_js_version=%2")
                              .arg(CLERK_BASE)
                              .arg(QString::fromStdString(clerkVersion_));
        QNetworkRequest req((QUrl(url)));
        req.setRawHeader("Cookie", QString::fromStdString(cookie_).toUtf8());
        req.setRawHeader("User-Agent", "Mozilla/5.0");

        QNetworkReply* reply = manager_->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonObject resp = doc.object()["response"].toObject();
                std::string sid = resp["last_active_session_id"].toString().toStdString();
                if (sid.empty()) {
                    QJsonArray sessions = resp["sessions"].toArray();
                    if (!sessions.isEmpty()) sid = sessions[0].toObject()["id"].toString().toStdString();
                }
                clerkSid_ = sid;
                if (!clerkSid_.empty()) refreshAuthToken(callback);
                else {
                    LOG_ERROR("SunoClient: Failed to extract Clerk SID");
                    if (callback) callback(false);
                }
            } else {
                LOG_ERROR("SunoClient: Clerk Session ID request failed: {}", reply->errorString().toStdString());
                if (callback) callback(false);
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
    req.setRawHeader("User-Agent", "Mozilla/5.0");

    QNetworkReply* reply = manager_->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            token_ = doc.object()["jwt"].toString().toStdString();
            if (token_.empty()) token_ = doc.object()["response"].toObject()["jwt"].toString().toStdString();
            if (!token_.empty()) {
                LOG_INFO("SunoClient: Refreshed auth token");
                tokenChanged.emitSignal(token_);
            }
            if (callback) callback(!token_.empty());
        } else {
            LOG_ERROR("SunoClient: Clerk Token request failed: {}", reply->errorString().toStdString());
            if (callback) callback(false);
        }
    });
}

QNetworkRequest SunoClient::createAuthenticatedRequest(const QString& endpoint) {
    QUrl url = endpoint.startsWith("http") ? QUrl(endpoint) : QUrl(API_BASE + endpoint);
    QNetworkRequest request(url);
    if (!token_.empty()) request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(token_));
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Content-Type", "application/json");
    return request;
}

void SunoClient::enqueueRequest(const QNetworkRequest& req, const std::string& method, const QByteArray& data, std::function<void(QNetworkReply*)> callback) {
    requestQueue_.push_back({req, method, data, callback});
    if (!queueTimer_->isActive()) queueTimer_->start();
}

void SunoClient::fetchLibrary(int page) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }
    auto proceed = [this, page] {
        QString url = QString("/feed/v3?hide_disliked=true&hide_gen_stems=true&hide_studio_clips=true&page=%1").arg(page - 1);
        enqueueRequest(createAuthenticatedRequest(url), "GET", {}, [this](QNetworkReply* reply) {
            onLibraryReply(reply);
        });
    };
    if (token_.empty() && !cookie_.empty()) {
        refreshAuthToken([this, proceed](bool success) {
            if (success) proceed();
            else errorOccurred.emitSignal("Auth refresh failed");
        });
    } else proceed();
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
    if (doc.isObject() && doc.object().contains("clips")) array = doc.object()["clips"].toArray();
    else if (doc.isArray()) array = doc.array();

    for (const auto& item : array) {
        QJsonObject obj = item.toObject();
        SunoClip clip;
        clip.id = obj["id"].toString().toStdString();
        clip.title = obj["title"].toString().toStdString();
        if (clip.title.empty()) clip.title = obj["name"].toString().toStdString();
        clip.audio_url = obj["audio_url"].toString().toStdString();
        clip.image_url = obj["image_url"].toString().toStdString();
        clip.status = obj["status"].toString().toStdString();
        QJsonObject meta = obj["metadata"].toObject();
        clip.metadata.prompt = meta["prompt"].toString().toStdString();
        clip.metadata.tags = meta["tags"].toString().toStdString();
        clips.push_back(clip);
    }
    libraryFetched.emitSignal(clips);
}

void SunoClient::generate(const std::string& prompt, const std::string& tags, bool makeInstrumental, const std::string& model) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }

    auto proceed = [this, prompt, tags, makeInstrumental, model] {
        QJsonObject body;
        body["gpt_description_prompt"] = QString::fromStdString(prompt);
        body["prompt"] = ""; // Used for custom lyrics
        body["tags"] = QString::fromStdString(tags);
        body["mv"] = QString::fromStdString(model);
        body["make_instrumental"] = makeInstrumental;
        body["continue_clip_id"] = QJsonValue::Null;
        body["continue_at"] = QJsonValue::Null;

        QJsonDocument doc(body);
        enqueueRequest(createAuthenticatedRequest("/generate/v2-web/"), "POST", doc.toJson(), [this](QNetworkReply* reply) {
            onGenerateReply(reply);
        });
    };

    if (token_.empty() && !cookie_.empty()) {
        refreshAuthToken([this, proceed](bool success) { if (success) proceed(); });
    } else proceed();
}

void SunoClient::onGenerateReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply);
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray array;
    if (doc.isObject() && doc.object().contains("clips")) array = doc.object()["clips"].toArray();
    
    std::vector<SunoClip> clips;
    for (const auto& item : array) {
        QJsonObject obj = item.toObject();
        SunoClip clip;
        clip.id = obj["id"].toString().toStdString();
        clip.status = "pending";
        clips.push_back(clip);
    }
    generationStarted.emitSignal(clips);
}

void SunoClient::handleNetworkError(QNetworkReply* reply) {
    std::string err = reply->errorString().toStdString();
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
        err = "Unauthorized: Token expired";
        token_.clear();
    }
    errorOccurred.emitSignal(err);
    LOG_ERROR("SunoClient API Error: {}", err);
}

void SunoClient::fetchAlignedLyrics(const std::string& clipId) {
    if (!isAuthenticated()) return;
    auto proceed = [this, clipId] {
        QString url = QString("/gen/%1/aligned_lyrics/v2").arg(QString::fromStdString(clipId));
        enqueueRequest(createAuthenticatedRequest(url), "GET", {}, [this, clipId](QNetworkReply* reply) {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) alignedLyricsFetched.emitSignal(clipId, reply->readAll().toStdString());
            else errorOccurred.emitSignal("Lyrics fetch failed");
        });
    };
    if (token_.empty() && !cookie_.empty()) refreshAuthToken([this, proceed](bool success) { if (success) proceed(); });
    else proceed();
}

void SunoClient::initiateWavConversion(const std::string& clipId) {
    if (!isAuthenticated()) return;
    QString url = QString("/gen/%1/convert_wav/").arg(QString::fromStdString(clipId));
    enqueueRequest(createAuthenticatedRequest(url), "POST", {}, [this, clipId](QNetworkReply* reply) {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 202) {
            QTimer::singleShot(2000, this, [this, clipId]() { pollWavFile(clipId, 60); });
        }
    });
}

void SunoClient::pollWavFile(const std::string& clipId, int maxAttempts) {
    if (!isAuthenticated() || maxAttempts <= 0) return;
    QString url = QString("/gen/%1/wav_file/").arg(QString::fromStdString(clipId));
    enqueueRequest(createAuthenticatedRequest(url), "GET", {}, [this, clipId, maxAttempts](QNetworkReply* reply) {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (reply->error() == QNetworkReply::NoError && doc.object().contains("wav_file_url")) {
            wavConversionReady.emitSignal(clipId, doc.object()["wav_file_url"].toString().toStdString());
        } else {
            QTimer::singleShot(2000, this, [this, clipId, maxAttempts]() { pollWavFile(clipId, maxAttempts - 1); });
        }
    });
}

} // namespace vc::suno
