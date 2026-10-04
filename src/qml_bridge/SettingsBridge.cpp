#include "SettingsBridge.hpp"
#include "SettingMacros.hpp"
#include "audio/AudioEngine.hpp"
#include "core/CliUtils.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/SunoClient.hpp"
#include "suno/auth/AuthHeaders.hpp"
#include "util/Types.hpp"

// QAudioDevice is only forward-declared by QMediaDevices' own includes; the
// enumeration below needs it complete.
#include <QAudioDevice>
#include <QMediaDevices>

namespace qml_bridge {

namespace {

/// Display text for the system-default entry. Kept distinct from the persisted
/// sentinel ("default") so the combo shows a readable label while the config
/// keeps a stable, machine-comparable value.
const QString kSystemDefaultLabel = QStringLiteral("System default (auto)");

QString defaultDeviceSentinel()
{
    const std::string_view sentinel = vc::AudioEngine::kDefaultDeviceName;
    return QString::fromUtf8(sentinel.data(), static_cast<qsizetype>(sentinel.size()));
}

} // namespace

vc::suno::SunoClient* SettingsBridge::s_sunoClient = nullptr;
vc::AudioEngine* SettingsBridge::s_audioEngine = nullptr;

SettingsBridge::SettingsBridge(QObject* parent)
    : QObject(parent)
{
    // Debounced auto-save: 2s after any setting change, persist to disk
    m_autoSaveTimer.setSingleShot(true);
    m_autoSaveTimer.setInterval(2000);
    QObject::connect(&m_autoSaveTimer, &QTimer::timeout, this, []() {
        auto path = vc::Config::instance().configPath();
        if (vc::Config::instance().save(path)) {
            LOG_INFO("SettingsBridge: Auto-saved configuration to {}", path.string());
        } else {
            LOG_ERROR("SettingsBridge: Auto-save failed to {}", path.string());
        }
    });

    m_sunoCredentialTimer.setSingleShot(true);
    m_sunoCredentialTimer.setInterval(750);
    QObject::connect(&m_sunoCredentialTimer, &QTimer::timeout, this,
                     [this]() { commitSunoCredential(); });
    attachSunoClient(s_sunoClient);

    refreshAudioDeviceList();
    attachAudioEngine(s_audioEngine);
}

SettingsBridge::~SettingsBridge()
{
    commitSunoCredential();
    if (s_sunoClient == m_sunoClient) {
        s_sunoClient = nullptr;
    }
    if (s_audioEngine == m_audioEngine) {
        s_audioEngine = nullptr;
    }
    // m_mediaDevices is parented to this bridge and dies with it.
    setInstance(nullptr);
}

void SettingsBridge::setSunoClient(vc::suno::SunoClient* client)
{
    s_sunoClient = client;
    if (auto* bridge = instance()) {
        bridge->attachSunoClient(client);
    }
}

void SettingsBridge::attachSunoClient(vc::suno::SunoClient* client)
{
    if (m_sunoClient == client) {
        syncSunoCredentialFromClient();
        return;
    }

    if (m_sunoClient) {
        disconnect(m_sunoClient, nullptr, this, nullptr);
    }
    m_sunoClient = client;
    if (m_sunoClient) {
        connect(m_sunoClient, &vc::suno::SunoClient::credentialChanged,
                this, [this]() { syncSunoCredentialFromClient(); });
    }
    if (m_sunoCredentialDirty) {
        m_sunoCredentialTimer.start();
        return;
    }
    syncSunoCredentialFromClient();
}

void SettingsBridge::scheduleAutoSave()
{
    m_autoSaveTimer.start(); // Restarts the timer if already running (debounce)
}

// ═══════════════════════════════════════════════════════════
// BUILD METADATA
// ═══════════════════════════════════════════════════════════

QString SettingsBridge::version() const
{
    // Deliberately the same accessor the --version banner uses. CHADVIS_VERSION
    // is forwarded by CMake from version.txt at the repository root, so the UI
    // and the CLI cannot report different versions, and there is no second
    // place for a version string to live.
    const std::string_view v = vc::Cli::version();
    return QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()));
}

// ═══════════════════════════════════════════════════════════
// SETTING IMPLEMENTATIONS (generated from X-macro table)
// ═══════════════════════════════════════════════════════════
#include "SettingsBridgeSettings.inc"

// ═══════════════════════════════════════════════════════════
// AUDIO DEVICE
//
// audioBufferSize and audioSampleRate come from the X-macro table above and
// therefore cannot push themselves to the engine. Everything here is manual, for
// the same reason sunoDownloadPath is.
// ═══════════════════════════════════════════════════════════

QString SettingsBridge::audioDevice() const
{
    // Via a const reference so a read does not mark the config dirty: the
    // mutable Config::audio() accessor does, and a getter has no business
    // changing state.
    const vc::Config& config = vc::Config::instance();
    return QString::fromStdString(config.audio().device);
}

void SettingsBridge::setAudioDevice(const QString& device)
{
    if (device == audioDevice()) {
        return;
    }

    if (!isSelectableAudioDevice(device)) {
        // Refused rather than persisted. The combo can only produce names this
        // platform reported, so reaching here means the list changed under the
        // UI; storing the name anyway would persist a device that can never be
        // opened. Emitted unconditionally so the combo's index binding snaps back
        // to the value that is actually stored.
        setAudioError(QStringLiteral("Audio device \"%1\" is not available.").arg(device));
        emit audioDeviceChanged();
        return;
    }

    // The combo's system-default row is a display label; what is persisted is
    // the sentinel, so config.toml never carries UI wording and the value stays
    // comparable across platforms and languages.
    const QString stored =
        (device == kSystemDefaultLabel) ? defaultDeviceSentinel() : device;
    vc::Config::instance().audio().device = stored.toStdString();
    emit audioDeviceChanged();
    scheduleAutoSave();
    pushAudioConfigToEngine();
}

void SettingsBridge::applyAudioConfig()
{
    pushAudioConfigToEngine();
}

void SettingsBridge::refreshAudioDevices()
{
    refreshAudioDeviceList();
}

void SettingsBridge::refreshAudioDeviceList()
{
    if (!m_mediaDevices) {
        m_mediaDevices = new QMediaDevices(this);
        connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
                &SettingsBridge::refreshAudioDeviceList);
    }

    // Enumerated once: QMediaDevices::audioOutputs() is a platform query (CoreAudio
    // enumerates every device), so calling it twice per refresh is a real cost on
    // the settings panel's open path.
    const auto outputs = QMediaDevices::audioOutputs();

    QStringList devices{kSystemDefaultLabel};
    devices.reserve(outputs.size() + 1);
    for (const auto& device : outputs) {
        devices.append(device.description());
    }

    const bool available = !outputs.isEmpty();
    const bool changed = devices != m_audioDevices || available != m_audioDevicesAvailable;
    m_audioDevices = std::move(devices);
    m_audioDevicesAvailable = available;
    if (changed) {
        emit audioDevicesChanged();
    }
    updateAudioDeviceNotice();
}

void SettingsBridge::updateAudioDeviceNotice()
{
    QString notice;
    if (!m_audioDevicesAvailable) {
        // No output at all: a combo listing only the system-default entry would
        // look enabled and promise something that cannot happen.
        notice = QStringLiteral("This system reports no audio output device.");
    } else if (!m_audioEngine) {
        notice = QStringLiteral(
            "Playback engine not attached; the change is saved and applies on next start.");
    } else {
        notice = QStringLiteral("Applied to the running output.");
    }

    if (notice == m_audioDeviceNotice) {
        return;
    }
    m_audioDeviceNotice = notice;
    emit audioDeviceNoticeChanged();
}

bool SettingsBridge::isSelectableAudioDevice(const QString& device) const
{
    if (device.isEmpty() || device == defaultDeviceSentinel() || device == kSystemDefaultLabel) {
        return true;
    }
    for (const auto& candidate : QMediaDevices::audioOutputs()) {
        if (candidate.description() == device) {
            return true;
        }
    }
    return false;
}

void SettingsBridge::pushAudioConfigToEngine()
{
    if (!m_audioEngine) {
        updateAudioDeviceNotice();
        return;
    }

    // A copy: applyAudioConfig stores it, and passing the live reference would
    // alias the member it assigns.
    const vc::AudioConfig audio{vc::Config::instance().audio()};
    if (auto applied = m_audioEngine->applyAudioConfig(audio); !applied) {
        LOG_ERROR("SettingsBridge: audio configuration refused: {}", applied.error().message);
        setAudioError(QString::fromStdString(applied.error().message));
        // Revert the persisted name to the one the engine is actually using, so
        // the stored setting and the open output cannot disagree.
        const QString live = QString::fromStdString(m_audioEngine->audioConfig().device);
        if (audioDevice() != live) {
            vc::Config::instance().audio().device = live.toStdString();
            emit audioDeviceChanged();
            scheduleAutoSave();
        }
        return;
    }

    if (!m_audioError.isEmpty()) {
        // Safe to clear because applyAudioConfig() resets the engine's counters,
        // so a fault that is still live is re-reported as its first occurrence.
        setAudioError(QString());
    }
    const QString status = m_audioEngine->audioOutputStatus();
    if (m_audioOutputStatus != status) {
        m_audioOutputStatus = status;
        emit audioOutputStatusChanged();
    }
    updateAudioDeviceNotice();
}

void SettingsBridge::setAudioError(const QString& message)
{
    if (m_audioError == message) {
        return;
    }
    m_audioError = message;
    emit audioErrorChanged();
}

void SettingsBridge::setAudioEngine(vc::AudioEngine* engine)
{
    s_audioEngine = engine;
    if (auto* bridge = instance()) {
        bridge->attachAudioEngine(engine);
    }
}

void SettingsBridge::attachAudioEngine(vc::AudioEngine* engine)
{
    if (m_audioEngine == engine) {
        return;
    }

    if (m_audioEngine) {
        disconnect(m_audioEngine, nullptr, this, nullptr);
    }
    m_audioEngine = engine;
    if (m_audioEngine) {
        connect(m_audioEngine, &vc::AudioEngine::errorSignal, this, [this](const std::string& error) {
            setAudioError(QString::fromStdString(error));
        });
        connect(m_audioEngine, &vc::AudioEngine::audioOutputStatusChanged, this,
                [this](const QString& status) {
                    m_audioOutputStatus = status;
                    emit audioOutputStatusChanged();
                });
        // Seeded, not just connected: an init-time failure happens before any
        // QML exists, so connecting to a signal that already fired would leave
        // the panel silent about exactly the case it exists to report.
        const QString status = m_audioEngine->audioOutputStatus();
        if (!status.isEmpty() && m_audioOutputStatus != status) {
            m_audioOutputStatus = status;
            emit audioOutputStatusChanged();
        }
    }
    updateAudioDeviceNotice();
}

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

QString SettingsBridge::sunoToken() const
{
    return m_sunoTokenCache;
}

void SettingsBridge::setSunoToken(const QString& token)
{
    const QString normalizedToken = vc::suno::auth::normalizeCookieHeader(token);
    if (normalizedToken == m_sunoTokenCache && !m_sunoCredentialDirty) {
        return;
    }
    m_sunoTokenCache = normalizedToken;
    m_sunoCredentialDirty = true;
    m_sunoCredentialTimer.start();
    emit sunoTokenChanged();
}

void SettingsBridge::commitSunoCredential()
{
    m_sunoCredentialTimer.stop();
    if (!m_sunoCredentialDirty) {
        return;
    }
    if (!m_sunoClient) {
        LOG_ERROR("SettingsBridge: Suno client unavailable; credential change was not applied");
        return;
    }
    m_sunoCredentialDirty = false;
    if (m_sunoTokenCache.isEmpty()) {
        m_sunoClient->clearLocalCredentials();
    } else {
        m_sunoClient->setCookie(m_sunoTokenCache.toStdString());
    }
}

void SettingsBridge::syncSunoCredentialFromClient()
{
    if (m_sunoCredentialDirty) {
        return;
    }
    const QString value = m_sunoClient
            ? m_sunoClient->configuredCredential()
            : QString();
    if (m_sunoTokenCache == value) {
        return;
    }
    m_sunoTokenCache = value;
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

    emit sunoDownloadPathChanged();
    syncSunoCredentialFromClient();

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
    // The audio buffer values are the engine's conversion window
    // (vc::pcm::conversionWindow), not a device period, so they are stepped
    // across the 4096-16384 range the window actually accepts. The old
    // 2048/1024/512/256 ladder sat entirely below it: all four presets clamped
    // to the same window, so the control did nothing at all in every one of them.
    if (preset == "Performance") {
        setVisualizerMeshX(32);
        setVisualizerMeshY(24);
        setVisualizerFps(60);
        setAudioBufferSize(16384);
    } else if (preset == "Balanced") {
        setVisualizerMeshX(64);
        setVisualizerMeshY(48);
        setVisualizerFps(120);
        setAudioBufferSize(8192);
    } else if (preset == "High Fidelity") {
        setVisualizerMeshX(128);
        setVisualizerMeshY(96);
        setVisualizerFps(144);
        setAudioBufferSize(4096);
    } else if (preset == "Ultra (Chad)") {
        setVisualizerMeshX(256);
        setVisualizerMeshY(192);
        setVisualizerFps(240);
        setAudioBufferSize(4096);
    }
    LOG_INFO("SettingsBridge: Performance preset applied: {}", preset.toStdString());
    // The window moved, so the live outputs have to hear about it. Left out of
    // the generated setters for the reason documented on applyAudioConfig().
    applyAudioConfig();
}

} // namespace qml_bridge
