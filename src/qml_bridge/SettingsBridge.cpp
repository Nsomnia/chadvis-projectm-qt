#include "SettingsBridge.hpp"
#include "SettingMacros.hpp"
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
// SETTING IMPLEMENTATIONS (generated from X-macro table)
// ═══════════════════════════════════════════════════════════
#include "SettingsBridgeSettings.inc"

// Special: sunoDownloadPath uses fs::path, not std::string — kept manual
QString SettingsBridge::sunoDownloadPath() const
{
    return QString::fromStdString(vc::Config::instance().suno().downloadPath.string());
}

void SettingsBridge::setSunoDownloadPath(const QString& path)
{
    if (path != sunoDownloadPath()) {
        vc::Config::instance().suno().downloadPath = path.toStdString();
        emit sunoDownloadPathChanged();
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

    // Emit all changed signals — generated from X-macro table
#undef SETTING_INT
#undef SETTING_BOOL
#undef SETTING_FLOAT
#undef SETTING_STRING
#undef SETTING_RO_STRING
#define SETTING_INT(prop, ...) emit prop##Changed();
#define SETTING_BOOL(prop, ...) emit prop##Changed();
#define SETTING_FLOAT(prop, ...) emit prop##Changed();
#define SETTING_STRING(prop, ...) emit prop##Changed();
#define SETTING_RO_STRING(prop, ...) emit prop##Changed();
#include "SettingsBridgeSettings.inc"
#undef SETTING_INT
#undef SETTING_BOOL
#undef SETTING_FLOAT
#undef SETTING_STRING
#undef SETTING_RO_STRING

    // Also emit the manual ones (sunoDownloadPath)
    emit sunoDownloadPathChanged();

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
