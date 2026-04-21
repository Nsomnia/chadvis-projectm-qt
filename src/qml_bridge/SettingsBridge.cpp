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
}

QObject* SettingsBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new SettingsBridge(qmlEngine);
    }
    return s_instance;
}

// Audio
int SettingsBridge::audioBufferSize() const { return static_cast<int>(vc::Config::instance().audio().bufferSize); }
void SettingsBridge::setAudioBufferSize(int size) {
    if (size != audioBufferSize()) {
        vc::Config::instance().audio().bufferSize = static_cast<vc::u32>(size);
        emit audioBufferSizeChanged();
    }
}

int SettingsBridge::audioSampleRate() const { return static_cast<int>(vc::Config::instance().audio().sampleRate); }
void SettingsBridge::setAudioSampleRate(int rate) {
    if (rate != audioSampleRate()) {
        vc::Config::instance().audio().sampleRate = static_cast<vc::u32>(rate);
        emit audioSampleRateChanged();
    }
}

// Visualizer
int SettingsBridge::visualizerFps() const { return static_cast<int>(vc::Config::instance().visualizer().fps); }
void SettingsBridge::setVisualizerFps(int fps) {
    if (fps != visualizerFps()) {
        vc::Config::instance().visualizer().fps = static_cast<vc::u32>(fps);
        emit visualizerFpsChanged();
    }
}

int SettingsBridge::visualizerMeshX() const { return static_cast<int>(vc::Config::instance().visualizer().meshX); }
void SettingsBridge::setVisualizerMeshX(int x) {
    if (x != visualizerMeshX()) {
        vc::Config::instance().visualizer().meshX = static_cast<vc::u32>(x);
        emit visualizerMeshXChanged();
    }
}

int SettingsBridge::visualizerMeshY() const { return static_cast<int>(vc::Config::instance().visualizer().meshY); }
void SettingsBridge::setVisualizerMeshY(int y) {
    if (y != visualizerMeshY()) {
        vc::Config::instance().visualizer().meshY = static_cast<vc::u32>(y);
        emit visualizerMeshYChanged();
    }
}

double SettingsBridge::visualizerBeatSensitivity() const { return static_cast<double>(vc::Config::instance().visualizer().beatSensitivity); }
void SettingsBridge::setVisualizerBeatSensitivity(double sensitivity) {
    if (qAbs(sensitivity - visualizerBeatSensitivity()) > 0.001) {
        vc::Config::instance().visualizer().beatSensitivity = static_cast<vc::f32>(sensitivity);
        emit visualizerBeatSensitivityChanged();
    }
}

// Recorder
int SettingsBridge::recorderCrf() const { return static_cast<int>(vc::Config::instance().recording().video.crf); }
void SettingsBridge::setRecorderCrf(int crf) {
    if (crf != recorderCrf()) {
        vc::Config::instance().recording().video.crf = static_cast<vc::u32>(crf);
        emit recorderCrfChanged();
    }
}

QString SettingsBridge::recorderPreset() const { return QString::fromStdString(vc::Config::instance().recording().video.preset); }
void SettingsBridge::setRecorderPreset(const QString& preset) {
    if (preset != recorderPreset()) {
        vc::Config::instance().recording().video.preset = preset.toStdString();
        emit recorderPresetChanged();
    }
}

int SettingsBridge::recorderWidth() const { return static_cast<int>(vc::Config::instance().recording().video.width); }
void SettingsBridge::setRecorderWidth(int width) {
    if (width != recorderWidth()) {
        vc::Config::instance().recording().video.width = static_cast<vc::u32>(width);
        emit recorderWidthChanged();
    }
}

int SettingsBridge::recorderHeight() const { return static_cast<int>(vc::Config::instance().recording().video.height); }
void SettingsBridge::setRecorderHeight(int height) {
    if (height != recorderHeight()) {
        vc::Config::instance().recording().video.height = static_cast<vc::u32>(height);
        emit recorderHeightChanged();
    }
}

// Karaoke
QString SettingsBridge::karaokeFont() const { return QString::fromStdString(vc::Config::instance().karaoke().fontFamily); }
void SettingsBridge::setKaraokeFont(const QString& font) {
    if (font != karaokeFont()) {
        vc::Config::instance().karaoke().fontFamily = font.toStdString();
        emit karaokeFontChanged();
    }
}

double SettingsBridge::karaokeYPosition() const { return static_cast<double>(vc::Config::instance().karaoke().yPosition); }
void SettingsBridge::setKaraokeYPosition(double y) {
    if (qAbs(y - karaokeYPosition()) > 0.001) {
        vc::Config::instance().karaoke().yPosition = static_cast<vc::f32>(y);
        emit karaokeYPositionChanged();
    }
}

bool SettingsBridge::karaokeEnabled() const { return vc::Config::instance().karaoke().enabled; }
void SettingsBridge::setKaraokeEnabled(bool enabled) {
    if (enabled != karaokeEnabled()) {
        vc::Config::instance().karaoke().enabled = enabled;
        emit karaokeEnabledChanged();
    }
}

// Suno
QString SettingsBridge::sunoToken() const { return QString::fromStdString(vc::Config::instance().suno().token); }
void SettingsBridge::setSunoToken(const QString& token) {
    if (token != sunoToken()) {
        vc::Config::instance().suno().token = token.toStdString();
        emit sunoTokenChanged();
    }
}

QString SettingsBridge::sunoDownloadPath() const { return QString::fromStdString(vc::Config::instance().suno().downloadPath.string()); }
void SettingsBridge::setSunoDownloadPath(const QString& path) {
    if (path != sunoDownloadPath()) {
        vc::Config::instance().suno().downloadPath = path.toStdString();
        emit sunoDownloadPathChanged();
    }
}

void SettingsBridge::save()
{
    auto path = vc::Config::instance().configPath();
    if (vc::Config::instance().save(path)) {
        LOG_INFO("SettingsBridge: Configuration saved to {}", path.string());
    } else {
        LOG_ERROR("SettingsBridge: Failed to save configuration to {}", path.string());
    }
}

void SettingsBridge::resetToDefaults()
{
    LOG_WARN("SettingsBridge: resetToDefaults not fully implemented yet");
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
