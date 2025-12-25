/**
 * @file ProjectMWrapper.hpp
 * @brief C++ wrapper around projectM v4 C API.
 *
 * Single responsibility: Manage projectM lifecycle and provide C++ interface.
 *
 * AGENT NOTE: projectM v4 uses a C API. This wrapper makes it RAII-safe.
 *
 * API Reference:
 * #include <projectm-4/projectm.h>
 * projectm_handle projectm_create();
 * void projectm_destroy(projectm_handle);
 * void projectm_set_window_size(projectm_handle, size_t w, size_t h);
 * void projectm_opengl_render_frame(projectm_handle);
 * void projectm_pcm_add_float(projectm_handle, const float*, unsigned int, projectm_channels);
 */
#ifndef PROJECTMWRAPPER_HPP
#define PROJECTMWRAPPER_HPP
#include <projectm-4/projectm.h>
#include <string>
#include <vector>
class ProjectMWrapper
{
public:
ProjectMWrapper();
~ProjectMWrapper();
// No copying - projectm_handle is not copyable
ProjectMWrapper(const ProjectMWrapper&) = delete;
ProjectMWrapper& operator=(const ProjectMWrapper&) = delete;
bool initialize();
void destroy();
void resize(int width, int height);
void renderFrame();
// Audio input
void addPCMData(const float* data, unsigned int samples);
void feedSilence();
// Preset control
bool loadPreset(const std::string& path);
void nextPreset();
void previousPreset();
void randomPreset();
// State
bool isInitialized() const { return m_handle != nullptr; }
private:
projectm_handle m_handle = nullptr;
std::vector<float> m_silenceBuffer;
int m_width = 1280;
int m_height = 720;
};
#endif // PROJECTMWRAPPER_HPP
