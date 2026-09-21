#pragma once
// SunoWorkspaceBridge.hpp - QML-facing bridge for SunoWorkspace.
//
// Exposes the local song-creation workspace to QML: generation, region
// editing, stem extraction, lyrics editing, and rendering.

#include <QObject>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QVariantList>
#include <QString>
#include "QmlSingletonBridge.hpp"
#include "suno/SunoWorkspace.hpp"

namespace qml_bridge {

class SunoWorkspaceBridge : public QObject,
                             public QmlSingletonBridge<SunoWorkspaceBridge,
                                                       SingletonPolicy::CachedUnparented> {
    friend class QmlSingletonBridge<SunoWorkspaceBridge, SingletonPolicy::CachedUnparented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit SunoWorkspaceBridge(QObject* parent = nullptr);

    // Generation
    Q_PROPERTY(bool generating READ generating NOTIFY generationStartedChanged)
    Q_PROPERTY(QVariantList generatedClips READ generatedClips NOTIFY generatedClipsChanged)
    Q_INVOKABLE void startGeneration(const QString& prompt, const QString& tags,
                                      bool instrumental, const QString& model);
    Q_INVOKABLE void cancelGeneration();
    bool generating() const { return workspace_ ? workspace_->isGenerating() : false; }
    QVariantList generatedClips() const;

    // Regions (mashup timeline)
    Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
    Q_INVOKABLE void addRegion(const QString& clipId, double startS, double endS,
                                const QString& label);
    Q_INVOKABLE void removeRegion(int index);
    Q_INVOKABLE void clearRegions();
    QVariantList regions() const;

    // Stems
    Q_PROPERTY(QVariantList stems READ stems NOTIFY stemsChanged)
    Q_INVOKABLE void requestStems(const QString& clipId);
    QVariantList stems() const;

    // Lyrics
    Q_PROPERTY(bool lyricsAvailable READ lyricsAvailable NOTIFY lyricsChanged)
    Q_INVOKABLE void setLyricsText(const QString& text);
    bool lyricsAvailable() const;
    QString lyricsText() const;

    // Rendering
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderStartedChanged)
    Q_PROPERTY(f32 renderProgress READ renderProgress NOTIFY renderProgressChanged)
    Q_INVOKABLE void startRender(int mode, double durationS, bool includeVideo,
                                  bool includeKaraoke);
    Q_INVOKABLE void cancelRender();
    bool rendering() const { return workspace_ ? workspace_->isRendering() : false; }
    f32 renderProgress() const { return renderProgress_; }

    // Workspace persistence
    Q_INVOKABLE bool saveWorkspace(const QString& path);
    Q_INVOKABLE bool loadWorkspace(const QString& path);

signals:
    void generationStartedChanged(bool generating);
    void generatedClipsChanged(const QVariantList& clips);
    void regionsChanged(const QVariantList& regions);
    void stemsChanged(const QVariantList& stems);
    void lyricsChanged();
    void renderStartedChanged(bool rendering);
    void renderProgressChanged(f32 progress, const QString& stage);

private slots:
    void onGenerationStarted();
    void onGenerationCompleted(const std::vector<vc::suno::SunoClip>& clips);
    void onGenerationFailed(const std::string& error);
    void onRegionAdded(const vc::suno::ClipRegion& region);
    void onRegionsCleared();
    void onStemsRequested(const std::string& clipId);
    void onStemsReady(const std::vector<vc::suno::StemTrack>& stems);
    void onLyricsChanged(const std::string& text);
    void onRenderStarted(vc::suno::RenderMode mode);
    void onRenderProgress(f32 percent, const std::string& stage);
    void onRenderCompleted(const std::filesystem::path& outputPath);
    void onRenderFailed(const std::string& error);
    void onErrorOccurred(const std::string& message);

private:
    vc::suno::SunoWorkspace* workspace_{nullptr};
    f32 renderProgress_{0.0f};
};

} // namespace qml_bridge