#include "SunoClient.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/Logger.hpp"

namespace vc::suno {

SunoClient::SunoClient(QObject* parent)
    : QObject(parent), manager_(new QNetworkAccessManager(this)) {
}

SunoClient::~SunoClient() = default;

void SunoClient::setToken(const std::string& token) {
    token_ = token;
}

bool SunoClient::isAuthenticated() const {
    return !token_.empty();
}

QNetworkRequest SunoClient::createRequest(const QString& endpoint) {
    QNetworkRequest request(QUrl(API_BASE + endpoint));
    request.setRawHeader("Authorization",
                         QString::fromStdString("Bearer " + token_).toUtf8());
    request.setRawHeader(
            "User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    return request;
}

void SunoClient::fetchLibrary(int page) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }

    QString url = QString("/feed/?page=%1").arg(page);
    QNetworkReply* reply = manager_->get(createRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLibraryReply(reply);
    });
}

void SunoClient::onLibraryReply(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray()) {
        // Sometimes it wraps in { "clips": [...] } or similar?
        // Based on Python code: [{"id": ...}, ...] or {"clips": [...]}
        if (doc.isObject() && doc.object().contains("clips")) {
            // Handle wrapped object
        } else {
            // errorOccurred.emitSignal("Invalid JSON response format");
            // return;
        }
    }

    std::vector<SunoClip> clips;
    QJsonArray array;

    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.isObject() && doc.object().contains("clips")) {
        array = doc.object()["clips"].toArray();
    } else if (doc.isObject() && doc.object().contains("project_clips")) {
        // Projects often wrap clips in "project_clips": [{"clip": {...}}]
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
    }
    errorOccurred.emitSignal(err);
    LOG_ERROR("SunoClient API Error: {}", err);
}

} // namespace vc::suno
