#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>
#include "QmlSingletonBridge.hpp"

namespace vc {
class AudioEngine;
}
namespace vc::suno {
class SunoClient;
}

/// Live QMediaDevices instance, kept only for audioOutputsChanged(). Global
/// namespace, which is where Qt declares it.
class QMediaDevices;

namespace qml_bridge {

/**
 * @brief SettingsBridge provides access to core engine settings from QML.
 *
 * All setters automatically trigger a debounced auto-save (2s delay).
 * Call save() explicitly for immediate persistence (e.g. on app quit).
 */
class SettingsBridge : public QObject,
                       public QmlSingletonBridge<SettingsBridge, SingletonPolicy::CachedQmlParented> {

// The CRTP mixin constructs this singleton via its private
// constructor; grant only the exact instantiation access.
friend class QmlSingletonBridge<SettingsBridge, SingletonPolicy::CachedQmlParented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Build metadata. SettingsBridge is the app-level bridge — it already owns
    // keyboard bindings and UI state, i.e. facts about the application rather
    // than about one engine subsystem — so the version belongs here. It is a
    // build-time fact from version.txt, NOT a persisted setting, hence CONSTANT
    // with no setter and no auto-save.
    Q_PROPERTY(QString version READ version CONSTANT)

    // Audio Settings
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(int audioSampleRate READ audioSampleRate WRITE setAudioSampleRate NOTIFY audioSampleRateChanged)
    // Output device by description, or "default" for the system default.
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)
    // Selectable descriptions, always led by the system-default entry. Populated
    // from QMediaDevices::audioOutputs() and kept current by its
    // audioOutputsChanged signal, so plugging in headphones updates the list.
    Q_PROPERTY(QStringList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(bool audioDevicesAvailable READ audioDevicesAvailable NOTIFY audioDevicesChanged)
    // Always non-empty. Why the device combo is disabled, or that a change is
    // saved for the next start rather than applied live. A control that cannot
    // do what it looks like it does has to say so.
    Q_PROPERTY(QString audioDeviceNotice READ audioDeviceNotice NOTIFY audioDeviceNoticeChanged)
    // Last engine failure. Mirrors AudioEngine::errorSignal, whose empty string
    // is the documented "recovered" value.
    Q_PROPERTY(QString audioError READ audioError NOTIFY audioErrorChanged)
    // What the output is actually running: device, conversion window, requested
    // rate against the device's range, and the observed sink rate.
    Q_PROPERTY(QString audioOutputStatus READ audioOutputStatus NOTIFY audioOutputStatusChanged)

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
    // Build metadata
    QString version() const;

    // Audio
    int audioBufferSize() const;
    void setAudioBufferSize(int size);
    int audioSampleRate() const;
    void setAudioSampleRate(int rate);
    QString audioDevice() const;
    void setAudioDevice(const QString& device);
    QStringList audioDevices() const { return m_audioDevices; }
    bool audioDevicesAvailable() const { return m_audioDevicesAvailable; }
    QString audioDeviceNotice() const { return m_audioDeviceNotice; }
    QString audioError() const { return m_audioError; }
    QString audioOutputStatus() const { return m_audioOutputStatus; }

    /// Re-reads the whole [audio] section from the config and pushes it to the
    /// live engine.
    ///
    /// Exists because audioBufferSize and audioSampleRate are generated from the
    /// SettingsBridgeSettings.inc X-macro table, whose setters cannot run a
    /// post-set hook, so QML calls this after changing either of them. Once a
    /// real post-set hook exists in SettingMacros.hpp, both int setters can push
    /// themselves and this can go.
    Q_INVOKABLE void applyAudioConfig();

    /// Re-enumerates output devices. audioOutputsChanged() already covers
    /// hot-plug; this is the manual retry for a platform that reported nothing
    /// at startup.
    Q_INVOKABLE void refreshAudioDevices();

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
    static void setSunoClient(vc::suno::SunoClient* client);
    /// Hands the bridge the playback engine so an audio setting takes effect
    /// immediately. Until BridgeRegistration::registerBridges() calls this, a
    /// change is persisted and applied on the next start, and audioDeviceNotice
    /// says exactly that rather than pretending otherwise.
    static void setAudioEngine(vc::AudioEngine* engine);

signals:
    void audioBufferSizeChanged();
    void audioSampleRateChanged();
    void audioDeviceChanged();
    void audioDevicesChanged();
    void audioDeviceNoticeChanged();
    void audioErrorChanged();
    void audioOutputStatusChanged();
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
    ~SettingsBridge() override;
    void scheduleAutoSave();

    void attachSunoClient(vc::suno::SunoClient* client);
    void commitSunoCredential();
    void syncSunoCredentialFromClient();

    void attachAudioEngine(vc::AudioEngine* engine);
    void refreshAudioDeviceList();
    void updateAudioDeviceNotice();
    void setAudioError(const QString& message);
    /// Pushes the whole [audio] section to the engine, reverting the persisted
    /// device name if the engine refuses it, so the setting on disk and the open
    /// output can never disagree.
    void pushAudioConfigToEngine();
    /// Whether @p device is one this platform currently reports, or the default
    /// sentinel. Guards against persisting a name nothing can open.
    [[nodiscard]] bool isSelectableAudioDevice(const QString& device) const;

    QTimer m_autoSaveTimer;
    QTimer m_sunoCredentialTimer;
    vc::suno::SunoClient* m_sunoClient{nullptr};
    static vc::suno::SunoClient* s_sunoClient;

    QString m_sunoTokenCache;
    bool m_sunoCredentialDirty{false};

    vc::AudioEngine* m_audioEngine{nullptr};
    static vc::AudioEngine* s_audioEngine;
    /// Live QMediaDevices instance, kept only for audioOutputsChanged(). Null
    /// until the first refresh, in which case hot-plug is simply not reported.
    /// Parented to this bridge, so no manual teardown.
    QMediaDevices* m_mediaDevices{nullptr};
    QStringList m_audioDevices;
    bool m_audioDevicesAvailable{false};
    QString m_audioDeviceNotice;
    QString m_audioError;
    QString m_audioOutputStatus;
};

} // namespace qml_bridge
