#pragma once
// ProjectMBridge.hpp - LibProjectM integration
// Where the magic happens (literally, ProjectM is magic)

#include "PresetManager.hpp"
#include "RenderTarget.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

// include <projectM-4/projectM.h>
// include <external/projectm-install/include/projectM-4/projectM.h>
// #include "external/projectm-install/include/projectM-4/projectM.h"
#include "projectM-4/projectM.h"
#include <memory>

namespace vc {

struct ProjectMConfig {
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    f32 beatSensitivity{1.0f};
    fs::path presetPath;
    u32 presetDuration{30};
    u32 transitionDuration{3};
    bool shufflePresets{true};
    std::string forcePreset{};
    bool useDefaultPreset{false};
    u32 meshX{128};
    u32 meshY{96};
};

class ProjectMBridge {
public:
    ProjectMBridge();
    ~ProjectMBridge();

    // Non-copyable
    ProjectMBridge(const ProjectMBridge&) = delete;
    ProjectMBridge& operator=(const ProjectMBridge&) = delete;

    // Initialize (must be called with active GL context)
    Result<void> init(const ProjectMConfig& config);
    void shutdown();
    bool isInitialized() const {
        return projectM_ != nullptr;
    }

    // Rendering
    void render();
    void renderToTarget(RenderTarget& target);

    // Audio input
    void addPCMData(const f32* data, u32 samples, u32 channels);
    void addPCMDataInterleaved(const f32* data, u32 frames, u32 channels);

    // Configuration
    void resize(u32 width, u32 height);
    void setFPS(u32 fps);
    void setBeatSensitivity(f32 sensitivity);
    void setShuffleEnabled(bool enabled) {
        shuffleEnabled_ = enabled;
    }

    // Preset control
    PresetManager& presets() {
        return presets_;
    }
    const PresetManager& presets() const {
        return presets_;
    }

    void loadPreset(const fs::path& path, bool smooth = true);
    void nextPreset(bool smooth = true);
    void previousPreset(bool smooth = true);
    void randomPreset(bool smooth = true);
    void lockPreset(bool locked);
    bool isPresetLocked() const {
        return presetLocked_;
    }

    // Info
    u32 width() const {
        return width_;
    }
    u32 height() const {
        return height_;
    }
    std::string currentPresetName() const;

    // Internal access (for VisualizerWindow to load presets with GL context)
    projectm_handle getHandle() {
        return projectM_;
    }

    // Signals
    Signal<std::string> presetChanged;
    Signal<bool> presetLoading; // Notify about preset loading state

private:
    void onPresetManagerChanged(const PresetInfo* preset);

    projectm_handle projectM_{nullptr};
    PresetManager presets_;

    u32 width_{1920};
    u32 height_{1080};
    bool presetLocked_{false};
    bool shuffleEnabled_{false};
};

} // namespace vc
