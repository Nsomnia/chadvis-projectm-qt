#include "ProjectMWrapper.hpp"
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>

#ifdef HAVE_PULSEAUDIO
#include "../platform/linux/PulseAudioSource.hpp"
#endif

ProjectMWrapper::ProjectMWrapper()
: m_silenceBuffer(2048, 0.0f)
{
}

ProjectMWrapper::~ProjectMWrapper()
{
    stopAudioCapture();
    destroy();
}

bool ProjectMWrapper::initialize()
{
    if (m_handle) {
        qWarning() << "Already initialized";
        return true;
    }
    
    // Create projectM
    m_handle = projectm_create();
    if (!m_handle) {
        qCritical() << "projectm_create() failed - OpenGL context issue";
        return false;
    }
    
    // Configure
    projectm_set_window_size(m_handle, m_width, m_height);
    projectm_set_fps(m_handle, 60);
    projectm_set_mesh_size(m_handle, 64, 64);  // Square power-of-2
    projectm_set_aspect_correction(m_handle, true);
    projectm_set_preset_duration(m_handle, 30.0);
    projectm_set_soft_cut_duration(m_handle, 3.0);
    projectm_set_beat_sensitivity(m_handle, 1.0);
    projectm_set_hard_cut_enabled(m_handle, false);
    
    // Create playlist
    m_playlist = projectm_playlist_create(m_handle);
    if (!m_playlist) {
        qCritical() << "Failed to create playlist";
        return false;
    }
    
    // Add preset paths
    QStringList presetPaths;
    presetPaths << "/usr/share/projectM/presets/presets_milkdrop"
                << "/usr/share/projectM/presets/presets_stock"
                << "/usr/share/projectM/presets/presets_projectM";
    
    for (const QString& path : presetPaths) {
        QDir dir(path);
        if (dir.exists()) {
            projectm_playlist_add_path(m_playlist, path.toUtf8().constData(), true, false);
        }
    }
    
    uint32_t playlistSize = projectm_playlist_size(m_playlist);
    if (playlistSize > 0) {
        projectm_playlist_set_shuffle(m_playlist, true);
        projectm_playlist_play_next(m_playlist, true);
        qDebug() << "Loaded preset from playlist (" << playlistSize << "total)";
    } else {
        qWarning() << "No presets found, using idle://";
        projectm_load_preset_file(m_handle, "idle://", false);
    }
    
    return true;
}

void ProjectMWrapper::destroy()
{
    if (m_playlist) {
        projectm_playlist_destroy(m_playlist);
        m_playlist = nullptr;
    }
    
    if (m_handle) {
        projectm_destroy(m_handle);
        m_handle = nullptr;
    }
}

void ProjectMWrapper::resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    
    m_width = width;
    m_height = height;
    
    if (m_handle) {
        projectm_set_window_size(m_handle, width, height);
    }
}

void ProjectMWrapper::renderFrame()
{
    if (!m_handle) return;
    
    // Check mesh
    size_t currentMeshX, currentMeshY;
    projectm_get_mesh_size(m_handle, &currentMeshX, &currentMeshY);
    if (currentMeshX != 64 || currentMeshY != 64) {
        projectm_set_mesh_size(m_handle, 64, 64);
    }
    
    // Render
    projectm_opengl_render_frame(m_handle);
}

void ProjectMWrapper::addPCMData(const float* data, unsigned int samples)
{
    if (!m_handle || !data || samples == 0) return;
    projectm_pcm_add_float(m_handle, data, samples, PROJECTM_STEREO);
}

void ProjectMWrapper::feedSilence()
{
    if (!m_handle) return;
    
#ifdef HAVE_PULSEAUDIO
    if (m_audioSource && m_audioSource->isRunning()) return;
#endif
    
    projectm_pcm_add_float(m_handle, m_silenceBuffer.data(),
                          m_silenceBuffer.size() / 2, PROJECTM_STEREO);
}

bool ProjectMWrapper::loadPreset(const std::string& path)
{
    if (!m_handle) return false;
    projectm_load_preset_file(m_handle, path.c_str(), true);
    return true;
}

void ProjectMWrapper::nextPreset()
{
    if (m_playlist && m_handle) {
        projectm_playlist_play_next(m_playlist, true);
    }
}

void ProjectMWrapper::previousPreset()
{
    if (m_playlist && m_handle) {
        projectm_playlist_play_previous(m_playlist, true);
    }
}

void ProjectMWrapper::randomPreset()
{
    if (m_playlist && m_handle) {
        projectm_playlist_set_shuffle(m_playlist, true);
        projectm_playlist_play_next(m_playlist, true);
    }
}

bool ProjectMWrapper::startAudioCapture()
{
#ifdef HAVE_PULSEAUDIO
    if (!m_handle) return false;
    
    if (m_audioSource) {
        if (m_audioSource->isRunning()) return true;
        m_audioSource.reset();
    }
    
    m_audioSource = std::make_unique<PulseAudioSource>(this);
    if (!m_audioSource->start()) {
        qCritical() << "Failed to start audio:" << m_audioSource->getError().c_str();
        m_audioSource.reset();
        return false;
    }
    
    return true;
#else
    return false;
#endif
}

void ProjectMWrapper::stopAudioCapture()
{
#ifdef HAVE_PULSEAUDIO
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
#endif
}

bool ProjectMWrapper::isAudioCapturing() const
{
#ifdef HAVE_PULSEAUDIO
    return m_audioSource && m_audioSource->isRunning();
#else
    return false;
#endif
}
