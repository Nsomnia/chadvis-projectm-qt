// SunoWorkspaceBridge.cpp - QML-facing bridge for SunoWorkspace.

#include "SunoWorkspaceBridge.hpp"

#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace qml_bridge {

SunoWorkspaceBridge::SunoWorkspaceBridge(QObject* parent)
    : QObject(parent) {
    workspace_ = new vc::suno::SunoWorkspace(this);

    connect(workspace_, &vc::suno::SunoWorkspace::generationStarted,
            this, &SunoWorkspaceBridge::onGenerationStarted);
    connect(workspace_, &vc::suno::SunoWorkspace::generationCompleted,
            this, &SunoWorkspaceBridge::onGenerationCompleted);
    connect(workspace_, &vc::suno::SunoWorkspace::generationFailed,
            this, &SunoWorkspaceBridge::onGenerationFailed);
    connect(workspace_, &vc::suno::SunoWorkspace::regionAdded,
            this, &SunoWorkspaceBridge::onRegionAdded);
    connect(workspace_, &vc::suno::SunoWorkspace::regionsCleared,
            this, &SunoWorkspaceBridge::onRegionsCleared);
    connect(workspace_, &vc::suno::SunoWorkspace::stemsRequested,
            this, &SunoWorkspaceBridge::onStemsRequested);
    connect(workspace_, &vc::suno::SunoWorkspace::stemsReady,
            this, &SunoWorkspaceBridge::onStemsReady);
    connect(workspace_, &vc::suno::SunoWorkspace::lyrics_changed,
            this, &SunoWorkspaceBridge::onLyricsChanged);
    connect(workspace_, &vc::suno::SunoWorkspace::renderStarted,
            this, &SunoWorkspaceBridge::onRenderStarted);
    connect(workspace_, &vc::suno::SunoWorkspace::renderProgress,
            this, &SunoWorkspaceBridge::onRenderProgress);
    connect(workspace_, &vc::suno::SunoWorkspace::renderCompleted,
            this, &SunoWorkspaceBridge::onRenderCompleted);
    connect(workspace_, &vc::suno::SunoWorkspace::renderFailed,
            this, &SunoWorkspaceBridge::onRenderFailed);
    connect(workspace_, &vc::suno::SunoWorkspace::errorOccurred,
            this, &SunoWorkspaceBridge::onErrorOccurred);

    LOG_INFO("SunoWorkspaceBridge: initialized");
}

void SunoWorkspaceBridge::startGeneration(const QString& prompt, const QString& tags,
                                            bool instrumental, const QString& model) {
    workspace_->startGeneration(prompt.toUtf8().toChar(),
                                tags.toUtf8().toChar(),
                                instrumental,
                                model.toUtf8().toChar());
}

void SunoWorkspaceBridge::cancelGeneration() {
    workspace_->cancelGeneration();
}

QVariantList SunoWorkspaceBridge::generatedClips() const {
    QVariantList list;
    for (const auto& clip : workspace_->generatedClips()) {
        QJsonObject c;
        c["id"] = QString::fromUtf8(clip.id);
        c["title"] = QString::fromUtf8(clip.title);
        list.append(c);
    }
    return list;
}

void SunoWorkspaceBridge::addRegion(const QString& clipId, double startS, double endS,
                                     const QString& label) {
    workspace_->addRegion(clipId.toUtf8().toChar(), startS, endS,
                           label.toUtf8().toChar());
}

void SunoWorkspaceBridge::removeRegion(int index) {
    workspace_->removeRegion(static_cast<size_t>(index));
}

void SunoWorkspaceBridge::clearRegions() {
    workspace_->clearRegions();
}

QVariantList SunoWorkspaceBridge::regions() const {
    QVariantList list;
    for (const auto& r : workspace_->state().regions) {
        QJsonObject ro;
        ro["clip_id"] = QString::fromUtf8(r.clipId);
        ro["start_s"] = r.startSeconds;
        ro["end_s"] = r.endSeconds;
        ro["label"] = QString::fromUtf8(r.label);
        list.append(ro);
    }
    return list;
}

void SunoWorkspaceBridge::requestStems(const QString& clipId) {
    workspace_->requestStems(clipId.toUtf8().toChar());
}

QVariantList SunoWorkspaceBridge::stems() const {
    QVariantList list;
    for (const auto& s : workspace_->state().stems) {
        QJsonObject so;
        so["type"] = QString::fromUtf8(s.type);
        so["downloaded"] = s.downloaded;
        list.append(so);
    }
    return list;
}

void SunoWorkspaceBridge::setLyricsText(const QString& text) {
    workspace_->setLyricsText(text.toUtf8().toChar());
}

bool SunoWorkspaceBridge::lyricsAvailable() const {
    return !workspace_->state().lyricsText.empty();
}

QString SunoWorkspaceBridge::lyricsText() const {
    return QString::fromUtf8(workspace_->state().lyricsText);
}

void SunoWorkspaceBridge::startRender(int mode, double durationS, bool includeVideo,
                                        bool includeKaraoke) {
    workspace_->startRender(static_cast<vc::suno::RenderMode>(mode), durationS,
                             includeVideo, includeKaraoke);
}

void SunoWorkspaceBridge::cancelRender() {
    workspace_->cancelRender();
}

bool SunoWorkspaceBridge::saveWorkspace(const QString& path) {
    auto result = workspace_->saveWorkspace(path.toStdWString());
    return static_cast<bool>(result);
}

bool SunoWorkspaceBridge::loadWorkspace(const QString& path) {
    auto result = workspace_->loadWorkspace(path.toStdWString());
    return static_cast<bool>(result);
}

void SunoWorkspaceBridge::onGenerationStarted() {
    emit generationStartedChanged(true);
}

void SunoWorkspaceBridge::onGenerationCompleted(const std::vector<vc::suno::SunoClip>& clips) {
    Q_UNUSED(clips)
    emit generationStartedChanged(false);
    emit generatedClipsChanged(generatedClips());
}

void SunoWorkspaceBridge::onGenerationFailed(const std::string& error) {
    LOG_ERROR("SunoWorkspaceBridge: generation failed: {}", error);
    emit generationStartedChanged(false);
}

void SunoWorkspaceBridge::onRegionAdded(const vc::suno::ClipRegion& region) {
    Q_UNUSED(region)
    emit regionsChanged(regions());
}

void SunoWorkspaceBridge::onRegionsCleared() {
    emit regionsChanged(regions());
}

void SunoWorkspaceBridge::onStemsRequested(const std::string& clipId) {
    Q_UNUSED(clipId)
}

void SunoWorkspaceBridge::onStemsReady(const std::vector<vc::suno::StemTrack>& stems) {
    Q_UNUSED(stems)
    emit stemsChanged(this->stems());
}

void SunoWorkspaceBridge::onLyricsChanged(const std::string& text) {
    Q_UNUSED(text)
    emit lyricsChanged();
}

void SunoWorkspaceBridge::onRenderStarted(vc::suno::RenderMode mode) {
    Q_UNUSED(mode)
    emit renderStartedChanged(true);
}

void SunoWorkspaceBridge::onRenderProgress(f32 percent, const std::string& stage) {
    renderProgress_ = percent;
    emit renderProgressChanged(percent, QString::fromUtf8(stage));
}

void SunoWorkspaceBridge::onRenderCompleted(const std::filesystem::path& outputPath) {
    Q_UNUSED(outputPath)
    renderProgress_ = 1.0f;
    emit renderProgressChanged(1.0f, "complete");
    emit renderStartedChanged(false);
}

void SunoWorkspaceBridge::onRenderFailed(const std::string& error) {
    LOG_ERROR("SunoWorkspaceBridge: render failed: {}", error);
    renderProgress_ = 0.0f;
    emit renderProgressChanged(0.0f, QString::fromUtf8(error));
    emit renderStartedChanged(false);
}

void SunoWorkspaceBridge::onErrorOccurred(const std::string& message) {
    LOG_ERROR("SunoWorkspaceBridge: {}", message);
}

} // namespace qml_bridge