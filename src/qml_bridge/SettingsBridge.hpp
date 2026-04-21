#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QString>
#include <QVariantMap>

namespace qml_bridge {

/**
 * @brief SettingsBridge provides access to core engine settings from QML.
 * 
 * It interfaces with vc::Config to persist and apply settings for:
 * - Audio Engine (buffer size, sample rate)
 * - Visualizer (mesh complexity, FPS, beat sensitivity)
 * - Video Recorder (CRF, preset, resolution)
 * - Karaoke (font, position, colors)
 * - Suno (token, download path)
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

    // Recorder Settings
    Q_PROPERTY(int recorderCrf READ recorderCrf WRITE setRecorderCrf NOTIFY recorderCrfChanged)
    Q_PROPERTY(QString recorderPreset READ recorderPreset WRITE setRecorderPreset NOTIFY recorderPresetChanged)
    Q_PROPERTY(int recorderWidth READ recorderWidth WRITE setRecorderWidth NOTIFY recorderWidthChanged)
    Q_PROPERTY(int recorderHeight READ recorderHeight WRITE setRecorderHeight NOTIFY recorderHeightChanged)

    // Karaoke Settings
    Q_PROPERTY(QString karaokeFont READ karaokeFont WRITE setKaraokeFont NOTIFY karaokeFontChanged)
    Q_PROPERTY(double karaokeYPosition READ karaokeYPosition WRITE setKaraokeYPosition NOTIFY karaokeYPositionChanged)
    Q_PROPERTY(bool karaokeEnabled READ karaokeEnabled WRITE setKaraokeEnabled NOTIFY karaokeEnabledChanged)

    // Suno Settings
    Q_PROPERTY(QString sunoToken READ sunoToken WRITE setSunoToken NOTIFY sunoTokenChanged)
    Q_PROPERTY(QString sunoDownloadPath READ sunoDownloadPath WRITE setSunoDownloadPath NOTIFY sunoDownloadPathChanged)

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

    // Recorder
    int recorderCrf() const;
    void setRecorderCrf(int crf);
    QString recorderPreset() const;
    void setRecorderPreset(const QString& preset);
    int recorderWidth() const;
    void setRecorderWidth(int width);
    int recorderHeight() const;
    void setRecorderHeight(int height);

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

    Q_INVOKABLE void save();
    Q_INVOKABLE void resetToDefaults();

signals:
    void audioBufferSizeChanged();
    void audioSampleRateChanged();
    void visualizerFpsChanged();
    void visualizerMeshXChanged();
    void visualizerMeshYChanged();
    void visualizerBeatSensitivityChanged();
    void recorderCrfChanged();
    void recorderPresetChanged();
    void recorderWidthChanged();
    void recorderHeightChanged();
    void karaokeFontChanged();
    void karaokeYPositionChanged();
    void karaokeEnabledChanged();
    void sunoTokenChanged();
    void sunoDownloadPathChanged();

private:
    explicit SettingsBridge(QObject* parent = nullptr);
    static SettingsBridge* s_instance;
};

} // namespace qml_bridge
