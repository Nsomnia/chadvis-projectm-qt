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
    if (vc::Application::instance() && vc::Application::instance()->sunoController()) {
        client_ = vc::Application::instance()->sunoController()->client();
    }

    if (client_) {
        // Correct signal handling for vc::Signal
        client_->libraryFetched.connect([this](const std::vector<vc::suno::SunoClip>& clips) {
            onLibraryUpdated();
        });
        
        client_->generationStarted.connect([this](const std::vector<vc::suno::SunoClip>& clips) {
            onGenerationStarted();
        });
    }
}

void SunoBridge::refreshLibrary()
{
    if (!client_) return;
    isLoading_ = true;
    emit isLoadingChanged();
    client_->fetchLibrary(0);
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

void SunoBridge::onLibraryUpdated()
{
    if (!client_) return;
    
    library_.clear();
    const auto& clips = client_->library();
    for (const auto& clip : clips) {
        QVariantMap map;
        map["id"] = QString::fromStdString(clip.id);
        map["title"] = QString::fromStdString(clip.title);
        map["imageUrl"] = QString::fromStdString(clip.image_url);
        map["audioUrl"] = QString::fromStdString(clip.audio_url);
        map["prompt"] = QString::fromStdString(clip.metadata.prompt);
        map["tags"] = QString::fromStdString(clip.metadata.tags);
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
