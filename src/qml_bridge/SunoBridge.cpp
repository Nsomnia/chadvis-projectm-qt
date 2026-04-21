#include "SunoBridge.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoOrchestrator.hpp"
#include "ui/controllers/SunoController.hpp"
#include "core/Application.hpp"
#include <QQmlEngine>

namespace qml_bridge {

SunoBridge::SunoBridge(QObject* parent)
    : QObject(parent)
{
}

void SunoBridge::setSunoController(vc::suno::SunoController* controller)
{
    controller_ = controller;
    if (controller_) {
        client_ = controller_->client();
    }
}

void SunoBridge::connectSignals()
{
    if (client_) {
        client_->libraryFetched.connect([this](const std::vector<vc::suno::SunoClip>& clips) {
            onLibraryUpdated(clips);
        });
        
        client_->generationStarted.connect([this](const std::vector<vc::suno::SunoClip>& clips) {
            onGenerationStarted();
        });
    }
}

void SunoBridge::refreshLibrary(int page)
{
    if (!client_) return;
    isLoading_ = true;
    emit isLoadingChanged();
    client_->fetchLibrary(page);
}

void SunoBridge::loadMore()
{
    if (!client_ || isLoading_) return;
    isLoading_ = true;
    emit isLoadingChanged();
    client_->fetchLibrary(++currentPage_);
}

void SunoBridge::generate(const QString& prompt, const QString& tags, bool instrumental, const QString& modelId)
{
    if (!client_) return;
    client_->generate(prompt.toStdString(), tags.toStdString(), instrumental, modelId.toStdString());
}

void SunoBridge::sendChatMessage(const QString& message, const QString& workspaceId)
{
    // Orchestrator logic will go here
}

void SunoBridge::fetchChatHistory()
{
    // Orchestrator logic will go here
}

void SunoBridge::onLibraryUpdated(const std::vector<vc::suno::SunoClip>& clips)
{
    library_.clear();
    for (const auto& clip : clips) {
        QVariantMap map;
        map["id"] = QString::fromStdString(clip.id);
        map["title"] = QString::fromStdString(clip.title);
        map["image_url"] = QString::fromStdString(clip.image_url);
        map["audio_url"] = QString::fromStdString(clip.audio_url);
        map["status"] = QString::fromStdString(clip.status);
        
        QVariantMap metadata;
        metadata["prompt"] = QString::fromStdString(clip.metadata.prompt);
        metadata["tags"] = QString::fromStdString(clip.metadata.tags);
        map["metadata"] = metadata;
        
        library_.append(map);
    }
    
    isLoading_ = false;
    emit isLoadingChanged();
    emit libraryChanged();
}

void SunoBridge::onGenerationStarted()
{
    emit generationStarted();
}

void SunoBridge::onChatMessageReceived(const QString& response, const QString& workspaceId)
{
    emit chatMessageReceived(response, workspaceId);
}

} // namespace qml_bridge
