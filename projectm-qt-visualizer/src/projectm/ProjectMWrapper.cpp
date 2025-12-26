/**
 * @file ProjectMWrapper.cpp
 * @brief Implementation of projectM v4 C API wrapper.
 * 
 * Based on projectm-visualizer/frontend-sdl2 implementation.
 * Follows the exact initialization and rendering pattern.
 */
#include "ProjectMWrapper.hpp"
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

#ifdef HAVE_PULSEAUDIO
#include "../platform/linux/PulseAudioSource.hpp"
#endif

ProjectMWrapper::ProjectMWrapper()
: m_silenceBuffer(2048, 0.0f) // Pre-allocate silence buffer
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
        qWarning() << "ProjectMWrapper::initialize() called on already initialized instance";
        return true;
    }
    
    // STEP 1: Create projectM instance
    m_handle = projectm_create();
    if (!m_handle) {
        qCritical() << "Failed to create projectM instance!";
        qCritical() << "This usually means OpenGL context is not properly initialized.";
        return false;
    }
    
    qDebug() << "✓ projectM instance created successfully";
    
    // STEP 2: Configure core settings (SDL2 frontend defaults)
    projectm_set_window_size(m_handle, m_width, m_height);
    projectm_set_fps(m_handle, 60);
    projectm_set_mesh_size(m_handle, 48, 32);  // SDL2 default mesh
    projectm_set_aspect_correction(m_handle, true);
    
    // STEP 3: Configure preset display settings
    projectm_set_preset_duration(m_handle, 30.0);
    projectm_set_soft_cut_duration(m_handle, 3.0);
    
    // STEP 4: Configure beat detection
    projectm_set_beat_sensitivity(m_handle, 1.0);
    projectm_set_hard_cut_enabled(m_handle, false);
    projectm_set_hard_cut_duration(m_handle, 20.0);
    projectm_set_hard_cut_sensitivity(m_handle, 1.0);
    
    // STEP 5: Create playlist (required for proper preset management)
    m_playlist = projectm_playlist_create(m_handle);
    if (!m_playlist) {
        qCritical() << "Failed to create playlist!";
        return false;
    }
    
    // STEP 6: Configure playlist
    projectm_playlist_set_shuffle(m_playlist, true);
    
    // STEP 7: Add preset paths
    QStringList presetPaths;
    presetPaths << "/usr/share/projectM/presets"
                << "/usr/local/share/projectM/presets"
                << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.projectM/presets";
    
    for (const QString& path : presetPaths) {
        QDir dir(path);
        if (dir.exists()) {
            qDebug() << "Adding preset path:" << path;
            projectm_playlist_add_path(m_playlist, path.toUtf8().constData(), true, false);
        }
    }
    
    // STEP 8: Check playlist and load initial preset
    uint32_t playlistSize = projectm_playlist_size(m_playlist);
    if (playlistSize == 0) {
        qWarning() << "No presets found, using idle:// preset";
        projectm_load_preset_file(m_handle, "idle://", false);
    } else {
        qDebug() << "Playlist has" << playlistSize << "presets";
        // Play first preset
        projectm_playlist_set_position(m_playlist, 0, true);
    }
    
    qDebug() << "✓ projectM initialized successfully";
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
        qDebug() << "projectM destroyed";
    }
}

void ProjectMWrapper::resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    
    m_width = width;
    m_height = height;
    
    if (m_handle) {
        projectm_set_window_size(m_handle, width, height);
        qDebug() << "projectM resized to" << width << "x" << height;
    }
}

void ProjectMWrapper::renderFrame()
{
    if (!m_handle) {
        return;
    }
    
    // Check if mesh needs updating (SDL2 pattern)
    size_t currentMeshX, currentMeshY;
    projectm_get_mesh_size(m_handle, &currentMeshX, &currentMeshY);
    if (currentMeshX != 48 || currentMeshY != 32) {
        projectm_set_mesh_size(m_handle, 48, 32);
    }
    
    // Render
    projectm_opengl_render_frame(m_handle);
}

void ProjectMWrapper::addPCMData(const float* data, unsigned int samples)
{
    if (!m_handle || !data || samples == 0) {
        return;
    }
    
    projectm_pcm_add_float(m_handle, data, samples, PROJECTM_STEREO);
}

void ProjectMWrapper::feedSilence()
{
    if (!m_handle) {
        return;
    }
    
    // Only feed silence if NOT capturing audio
#ifdef HAVE_PULSEAUDIO
    if (m_audioSource && m_audioSource->isRunning()) {
        return;
    }
#endif
    
    projectm_pcm_add_float(m_handle, m_silenceBuffer.data(),
                          m_silenceBuffer.size() / 2, PROJECTM_STEREO);
}

bool ProjectMWrapper::loadPreset(const std::string& path)
{
    if (!m_handle) {
        return false;
    }
    
    projectm_load_preset_file(m_handle, path.c_str(), true);
    qDebug() << "Loaded preset:" << QString::fromStdString(path);
    return true;
}

void ProjectMWrapper::nextPreset()
{
    if (m_playlist && m_handle) {
        projectm_playlist_play_next(m_playlist, true);
    } else {
        qWarning() << "Cannot switch preset - playlist not initialized";
    }
}

void ProjectMWrapper::previousPreset()
{
    if (m_playlist && m_handle) {
        projectm_playlist_play_previous(m_playlist, true);
    } else {
        qWarning() << "Cannot switch preset - playlist not initialized";
    }
}

void ProjectMWrapper::randomPreset()
{
    if (m_playlist && m_handle) {
        projectm_playlist_set_shuffle(m_playlist, true);
        projectm_playlist_play_next(m_playlist, true);
    } else {
        qWarning() << "Cannot randomize - playlist not initialized";
    }
}

// Audio capture methods
bool ProjectMWrapper::startAudioCapture()
{
#ifdef HAVE_PULSEAUDIO
    if (!m_handle) {
        qWarning() << "Cannot start audio capture - projectM not initialized";
        return false;
    }
    
    if (m_audioSource) {
        if (m_audioSource->isRunning()) {
            qDebug() << "Audio capture already running";
            return true;
        }
        m_audioSource.reset();
    }
    
    m_audioSource = std::make_unique<PulseAudioSource>(this);
    if (!m_audioSource->start()) {
        qCritical() << "Failed to start PulseAudio capture:" << m_audioSource->getError().c_str();
        m_audioSource.reset();
        return false;
    }
    
    qDebug() << "✓ Audio capture started successfully";
    return true;
#else
    qWarning() << "PulseAudio not available - audio capture disabled";
    return false;
#endif
}

void ProjectMWrapper::stopAudioCapture()
{
#ifdef HAVE_PULSEAUDIO
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
        qDebug() << "Audio capture stopped";
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
