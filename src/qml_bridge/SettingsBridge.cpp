#include "SettingsBridge.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/Types.hpp"
#include <QQmlEngine>

namespace qml_bridge {

SettingsBridge* SettingsBridge::s_instance = nullptr;

SettingsBridge::SettingsBridge(QObject* parent)
    : QObject(parent)
{
    // Debounced auto-save: 2s after any setting change, persist to disk
    m_autoSaveTimer.setSingleShot(true);
    m_autoSaveTimer.setInterval(2000);
    QObject::connect(&m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        auto path = vc::Config::instance().configPath();
        if (vc::Config::instance().save(path)) {
            LOG_INFO("SettingsBridge: Auto-saved configuration to {}", path.string());
        } else {
            LOG_ERROR("SettingsBridge: Auto-save failed to {}", path.string());
        }
    });
}

void SettingsBridge::scheduleAutoSave()
{
    m_autoSaveTimer.start(); // Restarts the timer if already running (debounce)
}

QObject* SettingsBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new SettingsBridge(qmlEngine);
    }
    return s_instance;
}

// ═══════════════════════════════════════════════════════════
// AUDIO
// ═══════════════════════════════════════════════════════════

int SettingsBridge::audioBufferSize() const {
    return static_cast<int>(vc::Config::instance().audio().bufferSize);
}
void SettingsBridge::setAudioBufferSize(int size) {
    if (size != audioBufferSize()) {
        vc::Config::instance().audio().bufferSize = static_cast<vc::u32>(size);
        emit audioBufferSizeChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::audioSampleRate() const {
    return static_cast<int>(vc::Config::instance().audio().sampleRate);
}
void SettingsBridge::setAudioSampleRate(int rate) {
    if (rate != audioSampleRate()) {
        vc::Config::instance().audio().sampleRate = static_cast<vc::u32>(rate);
        emit audioSampleRateChanged();
        scheduleAutoSave();
    }
}

// ═══════════════════════════════════════════════════════════
// VISUALIZER
// ═══════════════════════════════════════════════════════════

int SettingsBridge::visualizerFps() const {
    return static_cast<int>(vc::Config::instance().visualizer().fps);
}
void SettingsBridge::setVisualizerFps(int fps) {
    if (fps != visualizerFps()) {
        vc::Config::instance().visualizer().fps = static_cast<vc::u32>(fps);
        emit visualizerFpsChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::visualizerMeshX() const {
    return static_cast<int>(vc::Config::instance().visualizer().meshX);
}
void SettingsBridge::setVisualizerMeshX(int x) {
    if (x != visualizerMeshX()) {
        vc::Config::instance().visualizer().meshX = static_cast<vc::u32>(x);
        emit visualizerMeshXChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::visualizerMeshY() const {
    return static_cast<int>(vc::Config::instance().visualizer().meshY);
}
void SettingsBridge::setVisualizerMeshY(int y) {
    if (y != visualizerMeshY()) {
        vc::Config::instance().visualizer().meshY = static_cast<vc::u32>(y);
        emit visualizerMeshYChanged();
        scheduleAutoSave();
    }
}

double SettingsBridge::visualizerBeatSensitivity() const {
    return static_cast<double>(vc::Config::instance().visualizer().beatSensitivity);
}
void SettingsBridge::setVisualizerBeatSensitivity(double sensitivity) {
    if (qAbs(sensitivity - visualizerBeatSensitivity()) > 0.001) {
        vc::Config::instance().visualizer().beatSensitivity = static_cast<vc::f32>(sensitivity);
        emit visualizerBeatSensitivityChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::visualizerPresetDuration() const {
    return static_cast<int>(vc::Config::instance().visualizer().presetDuration);
}
void SettingsBridge::setVisualizerPresetDuration(int duration) {
    if (duration != visualizerPresetDuration()) {
        vc::Config::instance().visualizer().presetDuration = static_cast<vc::u32>(duration);
        emit visualizerPresetDurationChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::visualizerSmoothPresetDuration() const {
    return static_cast<int>(vc::Config::instance().visualizer().smoothPresetDuration);
}
void SettingsBridge::setVisualizerSmoothPresetDuration(int duration) {
    if (duration != visualizerSmoothPresetDuration()) {
        vc::Config::instance().visualizer().smoothPresetDuration = static_cast<vc::u32>(duration);
        emit visualizerSmoothPresetDurationChanged();
        scheduleAutoSave();
    }
}

bool SettingsBridge::visualizerShufflePresets() const {
    return vc::Config::instance().visualizer().shufflePresets;
}
void SettingsBridge::setVisualizerShufflePresets(bool shuffle) {
    if (shuffle != visualizerShufflePresets()) {
        vc::Config::instance().visualizer().shufflePresets = shuffle;
        emit visualizerShufflePresetsChanged();
        scheduleAutoSave();
    }
}

bool SettingsBridge::visualizerAspectCorrection() const {
    return vc::Config::instance().visualizer().aspectCorrection;
}
void SettingsBridge::setVisualizerAspectCorrection(bool correction) {
    if (correction != visualizerAspectCorrection()) {
        vc::Config::instance().visualizer().aspectCorrection = correction;
        emit visualizerAspectCorrectionChanged();
        scheduleAutoSave();
    }
}

// ═══════════════════════════════════════════════════════════
// RECORDER
// ═══════════════════════════════════════════════════════════

int SettingsBridge::recorderCrf() const {
    return static_cast<int>(vc::Config::instance().recording().video.crf);
}
void SettingsBridge::setRecorderCrf(int crf) {
    if (crf != recorderCrf()) {
        vc::Config::instance().recording().video.crf = static_cast<vc::u32>(crf);
        emit recorderCrfChanged();
        scheduleAutoSave();
    }
}

QString SettingsBridge::recorderPreset() const {
    return QString::fromStdString(vc::Config::instance().recording().video.preset);
}
void SettingsBridge::setRecorderPreset(const QString& preset) {
    if (preset != recorderPreset()) {
        vc::Config::instance().recording().video.preset = preset.toStdString();
        emit recorderPresetChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::recorderWidth() const {
    return static_cast<int>(vc::Config::instance().recording().video.width);
}
void SettingsBridge::setRecorderWidth(int width) {
    if (width != recorderWidth()) {
        vc::Config::instance().recording().video.width = static_cast<vc::u32>(width);
        emit recorderWidthChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::recorderHeight() const {
    return static_cast<int>(vc::Config::instance().recording().video.height);
}
void SettingsBridge::setRecorderHeight(int height) {
    if (height != recorderHeight()) {
        vc::Config::instance().recording().video.height = static_cast<vc::u32>(height);
        emit recorderHeightChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::recorderFps() const {
    return static_cast<int>(vc::Config::instance().recording().video.fps);
}
void SettingsBridge::setRecorderFps(int fps) {
    if (fps != recorderFps()) {
        vc::Config::instance().recording().video.fps = static_cast<vc::u32>(fps);
        emit recorderFpsChanged();
        scheduleAutoSave();
    }
}

QString SettingsBridge::recorderAudioCodec() const {
    return QString::fromStdString(vc::Config::instance().recording().audio.codec);
}
void SettingsBridge::setRecorderAudioCodec(const QString& codec) {
    if (codec != recorderAudioCodec()) {
        vc::Config::instance().recording().audio.codec = codec.toStdString();
        emit recorderAudioCodecChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::recorderAudioBitrate() const {
    return static_cast<int>(vc::Config::instance().recording().audio.bitrate);
}
void SettingsBridge::setRecorderAudioBitrate(int bitrate) {
    if (bitrate != recorderAudioBitrate()) {
        vc::Config::instance().recording().audio.bitrate = static_cast<vc::u32>(bitrate);
        emit recorderAudioBitrateChanged();
        scheduleAutoSave();
    }
}

// ═══════════════════════════════════════════════════════════
// KEYBOARD SHORTCUTS (read-only)
// ═══════════════════════════════════════════════════════════

QString SettingsBridge::keyboardPlayPause() const { return QString::fromStdString(vc::Config::instance().keyboard().playPause); }
QString SettingsBridge::keyboardNextTrack() const { return QString::fromStdString(vc::Config::instance().keyboard().nextTrack); }
QString SettingsBridge::keyboardPrevTrack() const { return QString::fromStdString(vc::Config::instance().keyboard().prevTrack); }
QString SettingsBridge::keyboardToggleRecord() const { return QString::fromStdString(vc::Config::instance().keyboard().toggleRecord); }
QString SettingsBridge::keyboardToggleFullscreen() const { return QString::fromStdString(vc::Config::instance().keyboard().toggleFullscreen); }
QString SettingsBridge::keyboardNextPreset() const { return QString::fromStdString(vc::Config::instance().keyboard().nextPreset); }
QString SettingsBridge::keyboardPrevPreset() const { return QString::fromStdString(vc::Config::instance().keyboard().prevPreset); }

// ═══════════════════════════════════════════════════════════
// KARAOKE
// ═══════════════════════════════════════════════════════════

QString SettingsBridge::karaokeFont() const {
    return QString::fromStdString(vc::Config::instance().karaoke().fontFamily);
}
void SettingsBridge::setKaraokeFont(const QString& font) {
    if (font != karaokeFont()) {
        vc::Config::instance().karaoke().fontFamily = font.toStdString();
        emit karaokeFontChanged();
        scheduleAutoSave();
    }
}

double SettingsBridge::karaokeYPosition() const {
    return static_cast<double>(vc::Config::instance().karaoke().yPosition);
}
void SettingsBridge::setKaraokeYPosition(double y) {
    if (qAbs(y - karaokeYPosition()) > 0.001) {
        vc::Config::instance().karaoke().yPosition = static_cast<vc::f32>(y);
        emit karaokeYPositionChanged();
        scheduleAutoSave();
    }
}

bool SettingsBridge::karaokeEnabled() const {
    return vc::Config::instance().karaoke().enabled;
}
void SettingsBridge::setKaraokeEnabled(bool enabled) {
    if (enabled != karaokeEnabled()) {
        vc::Config::instance().karaoke().enabled = enabled;
        emit karaokeEnabledChanged();
        scheduleAutoSave();
    }
}

// ═══════════════════════════════════════════════════════════
// SUNO
// ═══════════════════════════════════════════════════════════

QString SettingsBridge::sunoToken() const {
    return QString::fromStdString(vc::Config::instance().suno().token);
}
void SettingsBridge::setSunoToken(const QString& token) {
    if (token != sunoToken()) {
        vc::Config::instance().suno().token = token.toStdString();
        emit sunoTokenChanged();
        scheduleAutoSave();
    }
}

QString SettingsBridge::sunoDownloadPath() const {
    return QString::fromStdString(vc::Config::instance().suno().downloadPath.string());
}
void SettingsBridge::setSunoDownloadPath(const QString& path) {
    if (path != sunoDownloadPath()) {
        vc::Config::instance().suno().downloadPath = path.toStdString();
        emit sunoDownloadPathChanged();
        scheduleAutoSave();
    }
}

// ═══════════════════════════════════════════════════════════
// UI STATE (persistent sidebar/accordion)
// ═══════════════════════════════════════════════════════════

QString SettingsBridge::expandedPanel() const {
    return QString::fromStdString(vc::Config::instance().ui().expandedPanel);
}
void SettingsBridge::setExpandedPanel(const QString& panel) {
    if (panel != expandedPanel()) {
        vc::Config::instance().ui().expandedPanel = panel.toStdString();
        emit expandedPanelChanged();
        scheduleAutoSave();
    }
}

int SettingsBridge::sidebarWidth() const {
    return static_cast<int>(vc::Config::instance().ui().sidebarWidth);
}
void SettingsBridge::setSidebarWidth(int width) {
    if (width != sidebarWidth()) {
        vc::Config::instance().ui().sidebarWidth = static_cast<vc::i32>(width);
        emit sidebarWidthChanged();
        scheduleAutoSave();
    }
}

bool SettingsBridge::drawerOpen() const {
    return vc::Config::instance().ui().drawerOpen;
}
void SettingsBridge::setDrawerOpen(bool open) {
    if (open != drawerOpen()) {
        vc::Config::instance().ui().drawerOpen = open;
        emit drawerOpenChanged();
        scheduleAutoSave();
    }
}

// ═══════════════════════════════════════════════════════════
// ACTIONS
// ═══════════════════════════════════════════════════════════

void SettingsBridge::save()
{
    // Stop any pending auto-save — we're saving now
    m_autoSaveTimer.stop();

    auto path = vc::Config::instance().configPath();
    if (vc::Config::instance().save(path)) {
        LOG_INFO("SettingsBridge: Configuration saved to {}", path.string());
    } else {
        LOG_ERROR("SettingsBridge: Failed to save configuration to {}", path.string());
    }
}

void SettingsBridge::resetToDefaults()
{
    auto& config = vc::Config::instance();

    // Stop any pending auto-save during bulk reset
    m_autoSaveTimer.stop();

    // Reset each section to default-constructed values
    config.audio() = vc::AudioConfig{};
    config.visualizer() = vc::VisualizerConfig{};
    config.recording() = vc::RecordingConfig{};
    config.karaoke() = vc::KaraokeConfig{};
    config.suno() = vc::SunoConfig{};
    config.ui() = vc::UIConfig{};

    // Emit all signals so QML bindings update
    emit audioBufferSizeChanged();
    emit audioSampleRateChanged();
    emit visualizerFpsChanged();
    emit visualizerMeshXChanged();
    emit visualizerMeshYChanged();
    emit visualizerBeatSensitivityChanged();
    emit visualizerPresetDurationChanged();
    emit visualizerSmoothPresetDurationChanged();
    emit visualizerShufflePresetsChanged();
    emit visualizerAspectCorrectionChanged();
    emit recorderCrfChanged();
    emit recorderPresetChanged();
    emit recorderWidthChanged();
    emit recorderHeightChanged();
    emit recorderFpsChanged();
    emit recorderAudioCodecChanged();
    emit recorderAudioBitrateChanged();
    emit keyboardPlayPauseChanged();
    emit keyboardNextTrackChanged();
    emit keyboardPrevTrackChanged();
    emit keyboardToggleRecordChanged();
    emit keyboardToggleFullscreenChanged();
    emit keyboardNextPresetChanged();
    emit keyboardPrevPresetChanged();
    emit karaokeFontChanged();
    emit karaokeYPositionChanged();
    emit karaokeEnabledChanged();
    emit sunoTokenChanged();
    emit sunoDownloadPathChanged();
    emit expandedPanelChanged();
    emit sidebarWidthChanged();
    emit drawerOpenChanged();

    // Save the reset config to disk
    auto path = config.configPath();
    if (config.save(path)) {
        LOG_INFO("SettingsBridge: Configuration reset to defaults and saved to {}", path.string());
    } else {
        LOG_ERROR("SettingsBridge: Failed to save reset configuration to {}", path.string());
    }
}

void SettingsBridge::setPerformancePreset(const QString& preset)
{
    if (preset == "Performance") {
        setVisualizerMeshX(32);
        setVisualizerMeshY(24);
        setVisualizerFps(60);
        setAudioBufferSize(2048);
    } else if (preset == "Balanced") {
        setVisualizerMeshX(64);
        setVisualizerMeshY(48);
        setVisualizerFps(120);
        setAudioBufferSize(1024);
    } else if (preset == "High Fidelity") {
        setVisualizerMeshX(128);
        setVisualizerMeshY(96);
        setVisualizerFps(144);
        setAudioBufferSize(512);
    } else if (preset == "Ultra (Chad)") {
        setVisualizerMeshX(256);
        setVisualizerMeshY(192);
        setVisualizerFps(240);
        setAudioBufferSize(256);
    }
    LOG_INFO("SettingsBridge: Performance preset applied: {}", preset.toStdString());
}

} // namespace qml_bridge
