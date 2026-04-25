#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QString>
#include <QVariantMap>
#include <QTimer>

namespace qml_bridge {

/**
 * @brief SettingsBridge provides access to core engine settings from QML.
 *
 * All setters automatically trigger a debounced auto-save (2s delay).
 * Call save() explicitly for immediate persistence (e.g. on app quit).
 */
class SettingsBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Audio Settings
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(int audioSampleRate READ audioSampleRate WRITE setAudioSampleRate NOTIFY audioSampleRateChanged)

    // Visualizer Settings
    Q_PROPERTY(int visualizerFps READ visualizerFps WRITE setVisualizerFps NOTIFY visualizerFpsChanged)
    Q_PROPERTY(int visualizerMeshX READ visualizerMeshX WRITE setVisualizerMeshX NOTIFY visualizerMeshXChanged)
    Q_PROPERTY(int visualizerMeshY READ visualizerMeshY WRITE setVisualizerMeshY NOTIFY visualizerMeshYChanged)
    Q_PROPERTY(double visualizerBeatSensitivity READ visualizerBeatSensitivity WRITE setVisualizerBeatSensitivity NOTIFY visualizerBeatSensitivityChanged)
    Q_PROPERTY(int visualizerPresetDuration READ visualizerPresetDuration WRITE setVisualizerPresetDuration NOTIFY visualizerPresetDurationChanged)
    Q_PROPERTY(int visualizerSmoothPresetDuration READ visualizerSmoothPresetDuration WRITE setVisualizerSmoothPresetDuration NOTIFY visualizerSmoothPresetDurationChanged)
    Q_PROPERTY(bool visualizerShufflePresets READ visualizerShufflePresets WRITE setVisualizerShufflePresets NOTIFY visualizerShufflePresetsChanged)
    Q_PROPERTY(bool visualizerAspectCorrection READ visualizerAspectCorrection WRITE setVisualizerAspectCorrection NOTIFY visualizerAspectCorrectionChanged)

    // Recorder Settings
    Q_PROPERTY(int recorderCrf READ recorderCrf WRITE setRecorderCrf NOTIFY recorderCrfChanged)
    Q_PROPERTY(QString recorderPreset READ recorderPreset WRITE setRecorderPreset NOTIFY recorderPresetChanged)
    Q_PROPERTY(int recorderWidth READ recorderWidth WRITE setRecorderWidth NOTIFY recorderWidthChanged)
    Q_PROPERTY(int recorderHeight READ recorderHeight WRITE setRecorderHeight NOTIFY recorderHeightChanged)
    Q_PROPERTY(int recorderFps READ recorderFps WRITE setRecorderFps NOTIFY recorderFpsChanged)
    Q_PROPERTY(QString recorderAudioCodec READ recorderAudioCodec WRITE setRecorderAudioCodec NOTIFY recorderAudioCodecChanged)
    Q_PROPERTY(int recorderAudioBitrate READ recorderAudioBitrate WRITE setRecorderAudioBitrate NOTIFY recorderAudioBitrateChanged)

    // Keyboard Shortcuts (read-only from QML for now)
    Q_PROPERTY(QString keyboardPlayPause READ keyboardPlayPause NOTIFY keyboardPlayPauseChanged)
    Q_PROPERTY(QString keyboardNextTrack READ keyboardNextTrack NOTIFY keyboardNextTrackChanged)
    Q_PROPERTY(QString keyboardPrevTrack READ keyboardPrevTrack NOTIFY keyboardPrevTrackChanged)
    Q_PROPERTY(QString keyboardToggleRecord READ keyboardToggleRecord NOTIFY keyboardToggleRecordChanged)
    Q_PROPERTY(QString keyboardToggleFullscreen READ keyboardToggleFullscreen NOTIFY keyboardToggleFullscreenChanged)
    Q_PROPERTY(QString keyboardNextPreset READ keyboardNextPreset NOTIFY keyboardNextPresetChanged)
    Q_PROPERTY(QString keyboardPrevPreset READ keyboardPrevPreset NOTIFY keyboardPrevPresetChanged)

    // Karaoke Settings
    Q_PROPERTY(QString karaokeFont READ karaokeFont WRITE setKaraokeFont NOTIFY karaokeFontChanged)
    Q_PROPERTY(double karaokeYPosition READ karaokeYPosition WRITE setKaraokeYPosition NOTIFY karaokeYPositionChanged)
    Q_PROPERTY(bool karaokeEnabled READ karaokeEnabled WRITE setKaraokeEnabled NOTIFY karaokeEnabledChanged)

    // Suno Settings
    Q_PROPERTY(QString sunoToken READ sunoToken WRITE setSunoToken NOTIFY sunoTokenChanged)
    Q_PROPERTY(QString sunoDownloadPath READ sunoDownloadPath WRITE setSunoDownloadPath NOTIFY sunoDownloadPathChanged)

    // UI State (persistent sidebar/accordion)
    Q_PROPERTY(QString expandedPanel READ expandedPanel WRITE setExpandedPanel NOTIFY expandedPanelChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(bool drawerOpen READ drawerOpen WRITE setDrawerOpen NOTIFY drawerOpenChanged)

public:
    static QObject* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Audio
    int audioBufferSize() const;
    void setAudioBufferSize(int size);
    int audioSampleRate() const;
    void setAudioSampleRate(int rate);

    // Visualizer
    int visualizerFps() const;
    void setVisualizerFps(int fps);
    int visualizerMeshX() const;
    void setVisualizerMeshX(int x);
    int visualizerMeshY() const;
    void setVisualizerMeshY(int y);
    double visualizerBeatSensitivity() const;
    void setVisualizerBeatSensitivity(double sensitivity);
    int visualizerPresetDuration() const;
    void setVisualizerPresetDuration(int duration);
    int visualizerSmoothPresetDuration() const;
    void setVisualizerSmoothPresetDuration(int duration);
    bool visualizerShufflePresets() const;
    void setVisualizerShufflePresets(bool shuffle);
    bool visualizerAspectCorrection() const;
    void setVisualizerAspectCorrection(bool correction);

    // Recorder
    int recorderCrf() const;
    void setRecorderCrf(int crf);
    QString recorderPreset() const;
    void setRecorderPreset(const QString& preset);
    int recorderWidth() const;
    void setRecorderWidth(int width);
    int recorderHeight() const;
    void setRecorderHeight(int height);
    int recorderFps() const;
    void setRecorderFps(int fps);
    QString recorderAudioCodec() const;
    void setRecorderAudioCodec(const QString& codec);
    int recorderAudioBitrate() const;
    void setRecorderAudioBitrate(int bitrate);

    // Keyboard Shortcuts (read-only)
    QString keyboardPlayPause() const;
    QString keyboardNextTrack() const;
    QString keyboardPrevTrack() const;
    QString keyboardToggleRecord() const;
    QString keyboardToggleFullscreen() const;
    QString keyboardNextPreset() const;
    QString keyboardPrevPreset() const;

    // Karaoke
    QString karaokeFont() const;
    void setKaraokeFont(const QString& font);
    double karaokeYPosition() const;
    void setKaraokeYPosition(double y);
    bool karaokeEnabled() const;
    void setKaraokeEnabled(bool enabled);

    // Suno
    QString sunoToken() const;
    void setSunoToken(const QString& token);
    QString sunoDownloadPath() const;
    void setSunoDownloadPath(const QString& path);

    // UI State
    QString expandedPanel() const;
    void setExpandedPanel(const QString& panel);
    int sidebarWidth() const;
    void setSidebarWidth(int width);
    bool drawerOpen() const;
    void setDrawerOpen(bool open);

    Q_INVOKABLE void save();
    Q_INVOKABLE void resetToDefaults();

    // Performance Presets
    Q_INVOKABLE void setPerformancePreset(const QString& preset);

signals:
    void audioBufferSizeChanged();
    void audioSampleRateChanged();
    void visualizerFpsChanged();
    void visualizerMeshXChanged();
    void visualizerMeshYChanged();
    void visualizerBeatSensitivityChanged();
    void visualizerPresetDurationChanged();
    void visualizerSmoothPresetDurationChanged();
    void visualizerShufflePresetsChanged();
    void visualizerAspectCorrectionChanged();
    void recorderCrfChanged();
    void recorderPresetChanged();
    void recorderWidthChanged();
    void recorderHeightChanged();
    void recorderFpsChanged();
    void recorderAudioCodecChanged();
    void recorderAudioBitrateChanged();
    void keyboardPlayPauseChanged();
    void keyboardNextTrackChanged();
    void keyboardPrevTrackChanged();
    void keyboardToggleRecordChanged();
    void keyboardToggleFullscreenChanged();
    void keyboardNextPresetChanged();
    void keyboardPrevPresetChanged();
    void karaokeFontChanged();
    void karaokeYPositionChanged();
    void karaokeEnabledChanged();
    void sunoTokenChanged();
    void sunoDownloadPathChanged();
    void expandedPanelChanged();
    void sidebarWidthChanged();
    void drawerOpenChanged();

private:
    explicit SettingsBridge(QObject* parent = nullptr);
    void scheduleAutoSave();

    static SettingsBridge* s_instance;
    QTimer m_autoSaveTimer;
};

} // namespace qml_bridge
