#include "SunoAuth.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include "core/Logger.hpp"

namespace vc::suno {

SunoAuth::SunoAuth(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent)
    , manager_(manager)
    , autoRefreshTimer_(new QTimer(this)) {
    
    autoRefreshTimer_->setInterval(DEFAULT_REFRESH_INTERVAL);
    connect(autoRefreshTimer_, &QTimer::timeout, this, &SunoAuth::onAutoRefreshTimer);
}

SunoAuth::~SunoAuth() {
    stopAutoRefresh();
}

void SunoAuth::setCookie(const std::string& cookie) {
    state_.cookie = cookie;
    state_.clerkSid.clear();
    extractTokenFromCookie();
}

void SunoAuth::setToken(const std::string& token) {
    state_.token = token;
    state_.lastRefresh = std::chrono::steady_clock::now();
}

void SunoAuth::extractTokenFromCookie() {
    QMap<QString, QString> cookieMap;
    QStringList parts = QString::fromStdString(state_.cookie).split(';');
    
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
        state_.token = sessionValue.toStdString();
        state_.clerkSid = extractSidFromToken(state_.token);
        LOG_INFO("SunoAuth: Extracted JWT from __session cookie");
        emit tokenChanged(state_.token);
    }
}

std::string SunoAuth::extractSidFromToken(const std::string& token) {
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

void SunoAuth::startAutoRefresh(int intervalMs) {
    if (autoRefreshActive_) return;
    
    if (!isAuthenticated()) {
        LOG_WARN("SunoAuth: Cannot start auto-refresh without authentication");
        return;
    }
    
    autoRefreshTimer_->setInterval(intervalMs);
    autoRefreshActive_ = true;
    state_.lastRefresh = std::chrono::steady_clock::now();
    autoRefreshTimer_->start();
    LOG_INFO("SunoAuth: Auto-refresh started (interval: {}s)", intervalMs / 1000);
}

void SunoAuth::stopAutoRefresh() {
    autoRefreshActive_ = false;
    if (autoRefreshTimer_) {
        autoRefreshTimer_->stop();
    }
    LOG_INFO("SunoAuth: Auto-refresh stopped");
}

void SunoAuth::onAutoRefreshTimer() {
    if (!isAuthenticated()) {
        LOG_WARN("SunoAuth: Session expired, stopping auto-refresh");
        stopAutoRefresh();
        emit sessionExpired();
        return;
    }
    
    LOG_DEBUG("SunoAuth: Performing scheduled token refresh");
    refresh([this](bool success) {
        if (success) {
            state_.lastRefresh = std::chrono::steady_clock::now();
            LOG_DEBUG("SunoAuth: Scheduled token refresh successful");
        } else {
            LOG_ERROR("SunoAuth: Scheduled token refresh failed");
        }
    });
}

void SunoAuth::refresh(std::function<void(bool)> callback) {
    if (state_.cookie.empty()) {
        if (callback) callback(false);
        return;
    }
    
    if (state_.clerkSid.empty()) {
        fetchSessionId(callback);
        return;
    }
    
    refreshToken(callback);
}

void SunoAuth::fetchSessionId(std::function<void(bool)> callback) {
    QString url = QString("%1/client?_is_native=true&_clerk_js_version=%2")
                          .arg(QString::fromLatin1(CLERK_BASE))
                          .arg(QString::fromStdString(state_.clerkVersion));
    
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Cookie", QString::fromStdString(state_.cookie).toUtf8());
    req.setRawHeader("User-Agent", 
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    
    QNetworkReply* reply = manager_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject resp = doc.object()["response"].toObject();
            std::string sid = resp["last_active_session_id"].toString().toStdString();
            
            if (sid.empty()) {
                QJsonArray sessions = resp["sessions"].toArray();
                if (!sessions.isEmpty()) {
                    sid = sessions[0].toObject()["id"].toString().toStdString();
                }
            }
            
            state_.clerkSid = sid;
            if (!state_.clerkSid.empty()) {
                refreshToken(callback);
            } else {
                LOG_ERROR("SunoAuth: Failed to extract Clerk SID");
                if (callback) callback(false);
            }
        } else {
            LOG_ERROR("SunoAuth: Clerk session request failed: {}", 
                      reply->errorString().toStdString());
            if (callback) callback(false);
        }
    });
}

void SunoAuth::refreshToken(std::function<void(bool)> callback) {
    QString url = QString("%1/client/sessions/%2/tokens?_is_native=true&_clerk_js_version=%3")
                          .arg(QString::fromLatin1(CLERK_BASE))
                          .arg(QString::fromStdString(state_.clerkSid))
                          .arg(QString::fromStdString(state_.clerkVersion));
    
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Cookie", QString::fromStdString(state_.cookie).toUtf8());
    req.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    
    QNetworkReply* reply = manager_->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            std::string newToken = doc.object()["jwt"].toString().toStdString();
            
            if (newToken.empty()) {
                newToken = doc.object()["response"].toObject()["jwt"].toString().toStdString();
            }
            
            if (!newToken.empty()) {
                state_.token = newToken;
                state_.lastRefresh = std::chrono::steady_clock::now();
                LOG_INFO("SunoAuth: Refreshed auth token ({}...)", newToken.substr(0, 10));
                emit tokenChanged(newToken);
                if (callback) callback(true);
            } else {
                LOG_ERROR("SunoAuth: JWT missing in refresh response");
                emit refreshFailed("JWT missing in refresh response");
                if (callback) callback(false);
            }
        } else {
            LOG_ERROR("SunoAuth: Token refresh failed: {}", reply->errorString().toStdString());
            emit refreshFailed(reply->errorString().toStdString());
            if (callback) callback(false);
        }
    });
}

} // namespace vc::suno
