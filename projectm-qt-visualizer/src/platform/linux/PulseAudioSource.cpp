/**
 * @file PulseAudioSource.cpp
 * @brief PulseAudio/PipeWire audio capture implementation
 *
 * Captures system audio via PulseAudio and feeds it to projectM.
 * Works with both PulseAudio and PipeWire (via pipewire-pulse).
 */
#include "PulseAudioSource.hpp"
#include "../projectm/ProjectMWrapper.hpp"
#include <QDebug>
#include <cstring>

PulseAudioSource::PulseAudioSource(ProjectMWrapper* projectM)
    : m_projectM(projectM)
{
}

PulseAudioSource::~PulseAudioSource()
{
    stop();
}

bool PulseAudioSource::start()
{
    if (m_running) {
        qWarning() << "PulseAudioSource already running";
        return true;
    }
    
    // PulseAudio sample specification
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.rate = SAMPLE_RATE;
    ss.channels = CHANNELS;
    
    // Buffer attributes - reduce latency
    pa_buffer_attr ba;
    ba.tlength = pa_usec_to_bytes(50000, &ss); // 50ms buffer
    ba.minreq = -1;
    ba.maxlength = -1;
    ba.prebuf = -1;
    ba.fragsize = pa_usec_to_bytes(20000, &ss); // 20ms fragments
    
    // Open stream to monitor (system audio output)
    // Try default.monitor first (PulseAudio)
    int error;
    m_stream = pa_simple_new(
        nullptr,                    // Default server
        "projectM-Visualizer",      // Application name
        PA_STREAM_RECORD,           // Recording stream
        "default.monitor",          // Source (monitor of default sink)
        "projectM Audio",           // Stream name
        &ss,                        // Sample spec
        nullptr,                    // Channel map (default)
        &ba,                        // Buffer attributes
        &error
    );
    
    if (!m_stream) {
        // Fallback: try without .monitor
        // This might capture microphone instead
        m_stream = pa_simple_new(
            nullptr,
            "projectM-Visualizer",
            PA_STREAM_RECORD,
            "default",
            "projectM Audio",
            &ss,
            nullptr,
            &ba,
            &error
        );
    }
    
    if (!m_stream) {
        m_error = "Failed to connect to PulseAudio: " + std::string(pa_strerror(error));
        qCritical() << m_error.c_str();
        return false;
    }
    
    m_running = true;
    m_thread = std::make_unique<std::thread>(&PulseAudioSource::captureThread, this);
    
    qDebug() << "PulseAudio capture started - capturing system audio";
    return true;
}

void PulseAudioSource::stop()
{
    if (!m_running) return;
    
    m_running = false;
    
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    
    if (m_stream) {
        pa_simple_free(m_stream);
        m_stream = nullptr;
    }
    
    qDebug() << "PulseAudio capture stopped";
}

void PulseAudioSource::captureThread()
{
    std::vector<float> buffer(BUFFER_SIZE * CHANNELS);
    int error;
    
    while (m_running && m_stream) {
        // Read audio data from PulseAudio
        // This blocks until data is available
        int result = pa_simple_read(
            m_stream,
            buffer.data(),
            buffer.size() * sizeof(float),
            &error
        );
        
        if (result < 0) {
            if (m_running) {
                m_error = "PulseAudio read error: " + std::string(pa_strerror(error));
                qWarning() << m_error.c_str();
            }
            break;
        }
        
        // Feed to projectM
        if (m_projectM && m_projectM->isInitialized()) {
            // projectM expects samples count (not bytes), and handles stereo internally
            m_projectM->addPCMData(buffer.data(), BUFFER_SIZE);
        }
    }
}
