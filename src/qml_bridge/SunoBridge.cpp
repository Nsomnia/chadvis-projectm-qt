#include "SunoBridge.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoModels.hpp"
#include <QQmlEngine>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
vc::suno::SunoClient* SunoBridge::s_client = nullptr;
SunoBridge* SunoBridge::s_instance = nullptr;

SunoBridge::SunoBridge(QObject* parent) : QObject(parent) {
    s_instance = this;
}

QObject* SunoBridge::create(QQmlEngine*, QJSEngine*) {
    if (!s_instance) {
        s_instance = new SunoBridge();
    }
    return s_instance;
}

void SunoBridge::setSunoController(vc::suno::SunoController* controller) {
    s_controller = controller;
    if (s_controller) {
        s_client = s_controller->client();
        connect(s_controller, &vc::suno::SunoController::libraryUpdated,
                s_instance, &SunoBridge::onLibraryUpdated);
    }
}

bool SunoBridge::loading() const { return loading_; }

QVariantList SunoBridge::clips() const { return clips_; }

int SunoBridge::totalClips() const {
    return clips_.size(); // Simplified for now
}

QVariantList SunoBridge::chatHistory() const { return chatHistory_; }

void SunoBridge::generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model) {
    if (s_client) {
        s_client->generate(prompt.toStdString(), tags.toStdString(), instrumental, model.toStdString());
        emit generationStarted();
    }
}

void SunoBridge::refreshLibrary(int page) {
    if (s_controller) {
        loading_ = true;
        emit loadingChanged();
        s_controller->refreshLibrary(page);
    }
}

void SunoBridge::sendChatMessage(const QString& message) {
    // Orchestrator integration would go here
    QVariantMap userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = message;
    chatHistory_.append(userMsg);
    emit chatHistoryChanged();
}

void SunoBridge::fetchChatHistory() {
    emit chatHistoryChanged();
}

void SunoBridge::onLibraryUpdated() {
    if (!s_controller) return;

    clips_.clear();
    const auto& clips = s_controller->clips();
    for (const auto& clip : clips) {
        QVariantMap map;
        map["id"] = QString::fromStdString(clip.id);
        map["title"] = QString::fromStdString(clip.title);
        map["status"] = QString::fromStdString(clip.status);
        map["image_url"] = QString::fromStdString(clip.image_url);
        
        QVariantMap meta;
        meta["tags"] = QString::fromStdString(clip.metadata.tags);
        meta["prompt"] = QString::fromStdString(clip.metadata.prompt);
        map["metadata"] = meta;
        
        clips_.append(map);
    }

    loading_ = false;
    emit loadingChanged();
    emit clipsChanged();
}

} // namespace qml_bridge
