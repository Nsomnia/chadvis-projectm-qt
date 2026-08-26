#include "SettingsBridge.hpp"
#include "SettingMacros.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/auth/CredentialStore.hpp"
#include "util/Types.hpp"

namespace qml_bridge {

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

// ─────────────────────────────────────────────────────────────
// Special: sunoToken is a SECRET — persisted via CredentialStore (OS keychain
// where available), never written to config.toml. Kept outside the X-macro
// table because that table reads/writes plain Config fields. The value is
// picked up by SunoClient::reloadStoredCredentials() on the next sync.
// ─────────────────────────────────────────────────────────────

QString SettingsBridge::sunoToken() const
{
    if (m_sunoTokenCacheDirty) {
        m_sunoTokenCacheDirty = false;
        vc::suno::auth::CredentialStore store;
        if (auto loaded = store.load("suno/default"); loaded.isOk()) {
            m_sunoTokenCache = loaded.value();
        }
    }
    return m_sunoTokenCache;
}

void SettingsBridge::setSunoToken(const QString& token)
{
    if (token == sunoToken()) {
        return;
    }
    m_sunoTokenCache = token;
    m_sunoTokenCacheDirty = false;

    vc::suno::auth::CredentialStore store;
    if (token.isEmpty()) {
        std::ignore = store.remove("suno/default");
        std::ignore = store.remove("suno/bearer");
    } else {
        auto result = store.store("suno/default", token);
        if (result.isErr()) {
            LOG_ERROR("SettingsBridge: failed to persist Suno credential ({})",
                      vc::suno::auth::CredentialStore::redact(token).toStdString());
        }
    }
    // No TOML write: nothing in the config changed (secrets never live there).
    emit sunoTokenChanged();
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

    // Also emit the manual ones (sunoDownloadPath) and refresh the secret
    // property from the store (the stored credential itself is NOT deleted
    // by a settings reset — that is an explicit user action).
    emit sunoDownloadPathChanged();
    m_sunoTokenCacheDirty = true;
    emit sunoTokenChanged();

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
