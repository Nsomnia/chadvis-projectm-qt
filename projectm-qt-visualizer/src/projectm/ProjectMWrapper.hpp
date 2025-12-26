/**
 * @file ProjectMWrapper.hpp
 * @brief C++ wrapper around projectM v4 C API.
 *
 * Single responsibility: Manage projectM lifecycle and provide C++ interface.
 * Based on projectm-visualizer/frontend-sdl2 implementation.
 *
 * AGENT NOTE: projectM v4 uses a C API. This wrapper makes it RAII-safe.
 * The wrapper is NOT copyable - projectm_handle is unique.
 */
#ifndef PROJECTMWRAPPER_HPP
#define PROJECTMWRAPPER_HPP

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include <string>
#include <vector>
#include <memory>

class PulseAudioSource;

class ProjectMWrapper
{
public:
    ProjectMWrapper();
    ~ProjectMWrapper();
    
    // No copying - projectm_handle is not copyable
    ProjectMWrapper(const ProjectMWrapper&) = delete;
    ProjectMWrapper& operator=(const ProjectMWrapper&) = delete;

    // Core functionality
    bool initialize();
    void destroy();
    void resize(int width, int height);
    void renderFrame();

    // Audio input
    void addPCMData(const float* data, unsigned int samples);
    void feedSilence();

    // Audio capture
    bool startAudioCapture();
    void stopAudioCapture();
    bool isAudioCapturing() const;

    // Preset control
    bool loadPreset(const std::string& path);
    void nextPreset();
    void previousPreset();
    void randomPreset();

    // State
    bool isInitialized() const { return m_handle != nullptr; }
    projectm_handle getHandle() { return m_handle; }

private:
    projectm_handle m_handle = nullptr;
    projectm_playlist_handle m_playlist = nullptr;
    std::vector<float> m_silenceBuffer;
    int m_width = 1280;
    int m_height = 720;

    // Audio capture
    std::unique_ptr<PulseAudioSource> m_audioSource;
};

#endif // PROJECTMWRAPPER_HPP
