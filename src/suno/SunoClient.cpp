#include "SunoClient.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDesktopServices>
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

    // Parse all cookies into a map (handles multiple __client* and __session* cookies)
    QMap<QString, QString> cookieMap;
    QStringList parts = QString::fromStdString(cookie).split(';');
    for (const QString& part : parts) {
        QString trimmed = part.trimmed();
        int eqPos = trimmed.indexOf('=');
        if (eqPos > 0) {
            QString name = trimmed.left(eqPos);
            QString value = trimmed.mid(eqPos + 1);
            // Store all versions, we'll prefer newer ones later
            cookieMap[name] = value;
        }
    }

    // Extract __session cookie - prefer newer ones (no suffix > _Jnxw-muT > others)
    QString sessionValue;
    if (cookieMap.contains("__session")) {
        sessionValue = cookieMap["__session"];  // Newest, no suffix
    } else if (cookieMap.contains("__session_Jnxw-muT")) {
        sessionValue = cookieMap["__session_Jnxw-muT"];
    } else {
        // Find any __session* cookie
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
                tokenChanged.emitSignal(token_);
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

void SunoClient::login(const std::string& email, const std::string& password, std::function<void(bool)> callback) {
    LOG_INFO("SunoClient: Attempting automated login for {}", email);

    auto updateCookies = [this](QNetworkReply* reply) {
        QVariant cookieVar = reply->header(QNetworkRequest::SetCookieHeader);
        if (!cookieVar.isValid()) return;

        QList<QNetworkCookie> cookies = qvariant_cast<QList<QNetworkCookie>>(cookieVar);
        if (cookies.isEmpty()) return;

        // Parse existing cookies into a map to avoid duplicates and handle updates
        QMap<QString, QString> cookieMap;
        auto parseIntoMap = [&](const std::string& cookieStr) {
            QStringList parts = QString::fromStdString(cookieStr).split(';');
            for (const QString& part : parts) {
                QString trimmed = part.trimmed();
                int eqPos = trimmed.indexOf('=');
                if (eqPos > 0) {
                    cookieMap[trimmed.left(eqPos)] = trimmed.mid(eqPos + 1);
                }
            }
        };

        if (!cookie_.empty()) parseIntoMap(cookie_);

        for (const auto& cookie : cookies) {
            cookieMap[QString::fromUtf8(cookie.name())] = QString::fromUtf8(cookie.value());
        }

        // Rebuild cookie string
        QStringList finalCookies;
        for (auto it = cookieMap.begin(); it != cookieMap.end(); ++it) {
            finalCookies << (it.key() + "=" + it.value());
        }
        cookie_ = finalCookies.join("; ").toStdString();
        LOG_DEBUG("SunoClient: Updated session cookies");
    };

    // Step 1: Create sign-in
    QString url = QString("%1/client/sign_ins?_clerk_js_version=%2")
                          .arg(CLERK_BASE)
                          .arg(QString::fromStdString(clerkVersion_));
    
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    
    QJsonObject json;
    json["identifier"] = QString::fromStdString(email);
    QByteArray body = QJsonDocument(json).toJson();

    QNetworkReply* reply = manager_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, password, callback, updateCookies]() {
        reply->deleteLater();
        updateCookies(reply);

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoClient: Login Step 1 failed: {}", reply->errorString().toStdString());
            if (callback) callback(false);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString signInId = doc.object()["response"].toObject()["id"].toString();
        if (signInId.isEmpty()) {
            LOG_ERROR("SunoClient: Failed to get sign-in ID");
            if (callback) callback(false);
            return;
        }

        // Step 2: Attempt first factor (password)
        QString step2Url = QString("%1/client/sign_ins/%2/attempt_first_factor?_clerk_js_version=%3")
                                   .arg(CLERK_BASE)
                                   .arg(signInId)
                                   .arg(QString::fromStdString(clerkVersion_));
        
        QNetworkRequest step2Req((QUrl(step2Url)));
        step2Req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        step2Req.setRawHeader("Cookie", QString::fromStdString(cookie_).toUtf8());
        step2Req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

        QJsonObject step2Json;
        step2Json["strategy"] = "password";
        step2Json["password"] = QString::fromStdString(password);
        QByteArray step2Body = QJsonDocument(step2Json).toJson();

        QNetworkReply* step2Reply = manager_->post(step2Req, step2Body);
        connect(step2Reply, &QNetworkReply::finished, this, [this, step2Reply, callback, updateCookies]() {
            step2Reply->deleteLater();
            updateCookies(step2Reply);

            if (step2Reply->error() != QNetworkReply::NoError) {
                LOG_ERROR("SunoClient: Login Step 2 failed: {}", step2Reply->errorString().toStdString());
                if (callback) callback(false);
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(step2Reply->readAll());
            QString status = doc.object()["response"].toObject()["status"].toString();
            
            if (status == "complete") {
                LOG_INFO("SunoClient: Login successful, refreshing token...");
                // Now we have the session cookies, we can refresh the token
                refreshAuthToken(callback);
            } else {
                LOG_ERROR("SunoClient: Login incomplete, status: {}", status.toStdString());
                if (callback) callback(false);
            }
        });
    });
}

void SunoClient::requestLoginCode(const std::string& email, std::function<void(bool, std::string)> callback) {
    LOG_INFO("SunoClient: Requesting login code for {}", email);

    auto updateCookies = [this](QNetworkReply* reply) {
        QVariant cookieVar = reply->header(QNetworkRequest::SetCookieHeader);
        if (!cookieVar.isValid()) return;
        QList<QNetworkCookie> cookies = qvariant_cast<QList<QNetworkCookie>>(cookieVar);
        if (cookies.isEmpty()) return;
        QMap<QString, QString> cookieMap;
        auto parseIntoMap = [&](const std::string& cookieStr) {
            QStringList parts = QString::fromStdString(cookieStr).split(';');
            for (const QString& part : parts) {
                QString trimmed = part.trimmed();
                int eqPos = trimmed.indexOf('=');
                if (eqPos > 0) cookieMap[trimmed.left(eqPos)] = trimmed.mid(eqPos + 1);
            }
        };
        if (!cookie_.empty()) parseIntoMap(cookie_);
        for (const auto& cookie : cookies) {
            cookieMap[QString::fromUtf8(cookie.name())] = QString::fromUtf8(cookie.value());
        }
        QStringList finalCookies;
        for (auto it = cookieMap.begin(); it != cookieMap.end(); ++it) finalCookies << (it.key() + "=" + it.value());
        cookie_ = finalCookies.join("; ").toStdString();
    };

    // Step 1: Create sign-in
    QString url = QString("%1/client/sign_ins?_clerk_js_version=%2")
                          .arg(CLERK_BASE)
                          .arg(QString::fromStdString(clerkVersion_));
    
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    
    QJsonObject json;
    json["identifier"] = QString::fromStdString(email);
    QByteArray body = QJsonDocument(json).toJson();

    QNetworkReply* reply = manager_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, updateCookies]() {
        reply->deleteLater();
        updateCookies(reply);

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoClient: OTP Step 1 failed: {}", reply->errorString().toStdString());
            if (callback) callback(false, "");
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject resp = doc.object()["response"].toObject();
        QString signInId = resp["id"].toString();
        
        // Step 2: Prepare first factor (email_code)
        QString step2Url = QString("%1/client/sign_ins/%2/prepare_first_factor?_clerk_js_version=%3")
                                   .arg(CLERK_BASE)
                                   .arg(signInId)
                                   .arg(QString::fromStdString(clerkVersion_));
        
        QNetworkRequest step2Req((QUrl(step2Url)));
        step2Req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        step2Req.setRawHeader("Cookie", QString::fromStdString(cookie_).toUtf8());
        step2Req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

        QJsonObject step2Json;
        step2Json["strategy"] = "email_code";
        QByteArray step2Body = QJsonDocument(step2Json).toJson();

        QNetworkReply* step2Reply = manager_->post(step2Req, step2Body);
        connect(step2Reply, &QNetworkReply::finished, this, [this, step2Reply, signInId, callback, updateCookies]() {
            step2Reply->deleteLater();
            updateCookies(step2Reply);

            if (step2Reply->error() != QNetworkReply::NoError) {
                LOG_ERROR("SunoClient: OTP Step 2 failed: {}", step2Reply->errorString().toStdString());
                if (callback) callback(false, "");
                return;
            }

            LOG_INFO("SunoClient: Login code sent to email");
            if (callback) callback(true, signInId.toStdString());
        });
    });
}

void SunoClient::submitLoginCode(const std::string& signInId, const std::string& code, std::function<void(bool)> callback) {
    LOG_INFO("SunoClient: Submitting login code for {}", signInId);

    auto updateCookies = [this](QNetworkReply* reply) {
        QVariant cookieVar = reply->header(QNetworkRequest::SetCookieHeader);
        if (!cookieVar.isValid()) return;
        QList<QNetworkCookie> cookies = qvariant_cast<QList<QNetworkCookie>>(cookieVar);
        if (cookies.isEmpty()) return;
        QMap<QString, QString> cookieMap;
        auto parseIntoMap = [&](const std::string& cookieStr) {
            QStringList parts = QString::fromStdString(cookieStr).split(';');
            for (const QString& part : parts) {
                QString trimmed = part.trimmed();
                int eqPos = trimmed.indexOf('=');
                if (eqPos > 0) cookieMap[trimmed.left(eqPos)] = trimmed.mid(eqPos + 1);
            }
        };
        if (!cookie_.empty()) parseIntoMap(cookie_);
        for (const auto& cookie : cookies) {
            cookieMap[QString::fromUtf8(cookie.name())] = QString::fromUtf8(cookie.value());
        }
        QStringList finalCookies;
        for (auto it = cookieMap.begin(); it != cookieMap.end(); ++it) finalCookies << (it.key() + "=" + it.value());
        cookie_ = finalCookies.join("; ").toStdString();
    };

    QString url = QString("%1/client/sign_ins/%2/attempt_first_factor?_clerk_js_version=%3")
                          .arg(CLERK_BASE)
                          .arg(QString::fromStdString(signInId))
                          .arg(QString::fromStdString(clerkVersion_));
    
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Cookie", QString::fromStdString(cookie_).toUtf8());
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    QJsonObject json;
    json["strategy"] = "email_code";
    json["code"] = QString::fromStdString(code);
    QByteArray body = QJsonDocument(json).toJson();

    QNetworkReply* reply = manager_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, updateCookies]() {
        reply->deleteLater();
        updateCookies(reply);

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoClient: OTP Attempt failed: {}", reply->errorString().toStdString());
            if (callback) callback(false);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString status = doc.object()["response"].toObject()["status"].toString();
        
        if (status == "complete") {
            LOG_INFO("SunoClient: OTP Login successful, refreshing token...");
            refreshAuthToken(callback);
        } else {
            LOG_ERROR("SunoClient: OTP Login incomplete, status: {}", status.toStdString());
            if (callback) callback(false);
        }
    });
}

void SunoClient::startOAuthFlow(const std::string& provider) {
    LOG_INFO("SunoClient: Starting OAuth flow for {}", provider);

    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 8080)) {
        LOG_ERROR("SunoClient: Failed to start loopback listener on port 8080");
        delete server;
        return;
    }

    connect(server, &QTcpServer::newConnection, this, [this, server]() {
        QTcpSocket* socket = server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, server, socket]() {
            QByteArray data = socket->readAll();
            QString request(data);
            
            // Extract URL from GET request
            QRegularExpression re("GET (.*) HTTP");
            QRegularExpressionMatch match = re.match(request);
            if (match.hasMatch()) {
                QUrl url("http://localhost:8080" + match.captured(1));
                LOG_INFO("SunoClient: OAuth redirect captured");
                oauthRedirectReceived.emitSignal(url);
                
                // Send simple success response
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                              "<html><body><h1>Authentication Successful!</h1>"
                              "<p>You can close this window and return to ChadVis.</p></body></html>");
                socket->disconnectFromHost();
            }
            server->close();
            server->deleteLater();
        });
    });

    // Construct Suno OAuth URL (This is a guess, need exact URL from user logs)
    // Most Clerk apps use /v1/client/sign_ins/oauth/google
    QString oauthUrl = QString("https://clerk.suno.com/v1/client/sign_ins/oauth/%1"
                               "?_clerk_js_version=%2"
                               "&redirect_url=http://localhost:8080")
                               .arg(QString::fromStdString(provider))
                               .arg(QString::fromStdString(clerkVersion_));

    LOG_INFO("SunoClient: Opening browser for OAuth...");
    QDesktopServices::openUrl(QUrl(oauthUrl));
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

QNetworkRequest SunoClient::createAuthenticatedRequest(const QString& endpoint) {
    QUrl url;
    if (endpoint.startsWith("http")) {
        url = QUrl(endpoint);
    } else {
        url = QUrl(API_BASE + endpoint);
    }
    QNetworkRequest request(url);
    
    // Use Bearer token (matching working Sidetrack implementation)
    if (!token_.empty()) {
        request.setRawHeader("Authorization", QString::fromStdString("Bearer " + token_).toUtf8());
    }
    
    // Simple headers like working implementation
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    
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
        // Use GET with Bearer token (matching working Sidetrack implementation)
        QString url = QString("/feed/v2?hide_disliked=true&hide_gen_stems=true&hide_studio_clips=true&page=%1").arg(page - 1);
        enqueueRequest(createAuthenticatedRequest(url), "GET", {}, [this](QNetworkReply* reply) {
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
        QString url = QString("/gen/%1/aligned_lyrics/v2")
                              .arg(QString::fromStdString(clipId));
        
        LOG_INFO("SunoClient: Fetching aligned lyrics for {}", clipId);
        
        // Use createAuthenticatedRequest to ensure Bearer token is used
        enqueueRequest(createAuthenticatedRequest(url), "GET", {}, [this, clipId](QNetworkReply* reply) {
            reply->deleteLater();
            
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                if (data.isEmpty()) {
                    LOG_WARN("SunoClient: Empty lyrics response for {}", clipId);
                } else {
                    // Check for "Processing lyrics" message
                    if (data.contains("Processing lyrics")) {
                        LOG_WARN("SunoClient: Lyrics still processing for {}. Will retry later.", clipId);
                        // Treat as error to trigger retry logic if applicable, or custom signal?
                        // For now, logging it clearly. Controller handles queue.
                        // Ideally we should emit a specific signal or error code.
                        // But current flow assumes success = alignedLyricsFetched.
                        // Let's emit error so controller can re-queue?
                        errorOccurred.emitSignal("Lyrics processing: " + clipId);
                    } else {
                        // Log first 100 chars of response for debugging
                        std::string preview = data.left(100).toStdString();
                        LOG_INFO("SunoClient: Received lyrics data ({} bytes). Preview: {}", data.size(), preview);
                        alignedLyricsFetched.emitSignal(clipId, data.toStdString());
                    }
                }
            } else if (status == 404) {
                LOG_WARN("SunoClient: v2 lyrics not found, trying fallback for {}", clipId);
                QString lyricsUrl = QString("/lyrics/%1")
                                      .arg(QString::fromStdString(clipId));
                enqueueRequest(createAuthenticatedRequest(lyricsUrl), "GET", {}, [this, clipId](QNetworkReply* r2) {
                    r2->deleteLater();
                    if (r2->error() == QNetworkReply::NoError) {
                        QByteArray data = r2->readAll();
                        LOG_INFO("SunoClient: Received fallback lyrics data ({} bytes)", data.size());
                        alignedLyricsFetched.emitSignal(clipId, data.toStdString());
                    } else {
                        LOG_ERROR("SunoClient: Fallback lyrics fetch failed: {}", r2->errorString().toStdString());
                    }
                });
            } else {
                std::string err = reply->errorString().toStdString();
                // If 401, treat same as handleNetworkError to trigger refresh logic
                if (status == 401) {
                    err = "Unauthorized: Token expired or invalid";
                    // DO NOT clear token here if it's going to be used by other concurrent requests
                    // But we MUST clear it so next fetch triggers refresh
                    token_.clear();
                }
                
                LOG_ERROR("SunoClient: Aligned lyrics fetch failed: {} (Status: {})", 
                          err, status);
                errorOccurred.emitSignal(err);
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
        
        if (clip.title.empty()) {
            clip.title = obj["name"].toString().toStdString();
        }

        LOG_DEBUG("SunoClient: Parsing clip {} - {}", clip.id, clip.title);

        clip.video_url = obj["video_url"].toString().toStdString();
        clip.audio_url = obj["audio_url"].toString().toStdString();
        clip.image_url = obj["image_url"].toString().toStdString();
        
        // Handle model info which can be at root or in metadata
        clip.major_model_version = obj["major_model_version"].toString().toStdString();
        clip.model_name = obj["model_name"].toString().toStdString();
        
        QJsonObject meta = obj["metadata"].toObject();
        if (clip.major_model_version.empty()) 
            clip.major_model_version = meta["major_model_version"].toString().toStdString();
        if (clip.model_name.empty())
            clip.model_name = meta["model_name"].toString().toStdString();
            
        if (meta.contains("mv"))
            clip.mv = meta["mv"].toString().toStdString();
        
        if (meta.contains("control_sliders")) {
             QJsonObject sliders = meta["control_sliders"].toObject();
             if (sliders.contains("weirdness_constraint"))
                 clip.metadata.weirdness = sliders["weirdness_constraint"].toDouble();
             if (sliders.contains("style_weight"))
                 clip.metadata.style_weight = sliders["style_weight"].toDouble();
        } else {
             if (meta.contains("weirdness_constraint"))
                 clip.metadata.weirdness = meta["weirdness_constraint"].toDouble();
             if (meta.contains("style_weight"))
                 clip.metadata.style_weight = meta["style_weight"].toDouble();
        }
        
        if (meta.contains("make_instrumental")) {
            QJsonValue val = meta["make_instrumental"];
            if (val.isBool()) clip.metadata.make_instrumental = val.toBool();
            else if (val.isString()) clip.metadata.make_instrumental = (val.toString() == "true");
        }

        clip.display_name = obj["display_name"].toString().toStdString();
        clip.handle = obj["handle"].toString().toStdString();
        clip.is_liked = obj["is_liked"].toBool() || obj["is_liked"].toInt() != 0;
        clip.is_trashed = obj["is_trashed"].toBool() || obj["is_trashed"].toInt() != 0;
        clip.is_public = obj["is_public"].toBool() || obj["is_public"].toInt() != 0;
        
        // created_at can be "created_at" or "created_at" in metadata
        clip.created_at = obj["created_at"].toString().toStdString();
        if (clip.created_at.empty()) {
             clip.created_at = meta["created_at"].toString().toStdString();
        }
        
        clip.status = obj["status"].toString().toStdString();

        clip.metadata.prompt = meta["prompt"].toString().toStdString();
        clip.metadata.tags = meta["tags"].toString().toStdString();
        clip.metadata.lyrics = meta["lyrics"].toString().toStdString();
        
        if (clip.metadata.lyrics.empty()) {
             std::string prompt = meta["prompt"].toString().toStdString();
             if (!prompt.empty()) {
                 clip.metadata.lyrics = prompt;
             }
        }

        clip.metadata.type = meta["type"].toString().toStdString();
        
        if (meta.contains("duration")) {
            if (meta["duration"].isDouble()) {
                double secs = meta["duration"].toDouble();
                clip.metadata.duration = file::formatDuration(Duration(static_cast<i64>(secs * 1000)));
            } else {
                clip.metadata.duration = meta["duration"].toString().toStdString();
            }
        }
        
        clip.metadata.error_message = meta["error_message"].toString().toStdString();

        LOG_DEBUG("  Model: {}, Version: {}, Duration: {}, Created: {}", 
                 clip.model_name, clip.major_model_version, clip.metadata.duration, clip.created_at);

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
