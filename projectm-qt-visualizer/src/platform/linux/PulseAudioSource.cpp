#include "PulseAudioSource.hpp"
#include "projectm/ProjectMWrapper.hpp"
#include <QDebug>
#include <cstring>

PulseAudioSource::PulseAudioSource(ProjectMWrapper* projectM)
    : m_projectM(projectM)
{
    qDebug() << "PulseAudioSource constructor";
}

PulseAudioSource::~PulseAudioSource()
{
    qDebug() << "PulseAudioSource destructor";
    stop();
}

bool PulseAudioSource::start()
{
    qDebug() << "=== PulseAudioSource::start() ===";
    
    if (m_running) {
        qWarning() << "Already running";
        return true;
    }
    
    // Sample spec
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.rate = SAMPLE_RATE;
    ss.channels = CHANNELS;
    
    // Buffer attributes
    pa_buffer_attr ba;
    ba.tlength = pa_usec_to_bytes(50000, &ss);
    ba.minreq = -1;
    ba.maxlength = -1;
    ba.prebuf = -1;
    ba.fragsize = pa_usec_to_bytes(20000, &ss);
    
    // Try default.monitor first
    int error;
    m_stream = pa_simple_new(
        nullptr,
        "projectM-Visualizer",
        PA_STREAM_RECORD,
        "default.monitor",
        "projectM Audio",
        &ss,
        nullptr,
        &ba,
        &error
    );
    
    if (!m_stream) {
        qDebug() << "default.monitor failed, trying default";
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
        m_error = "Failed: " + std::string(pa_strerror(error));
        qCritical() << m_error.c_str();
        return false;
    }
    
    qDebug() << "✓ Stream opened";
    
    m_running = true;
    m_thread = std::make_unique<std::thread>(&PulseAudioSource::captureThread, this);
    
    qDebug() << "✓ Thread started";
    return true;
}

void PulseAudioSource::stop()
{
    if (!m_running) return;
    
    qDebug() << "Stopping PulseAudioSource...";
    m_running = false;
    
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    
    if (m_stream) {
        pa_simple_free(m_stream);
        m_stream = nullptr;
    }
    
    qDebug() << "Stopped";
}

void PulseAudioSource::captureThread()
{
    std::vector<float> buffer(BUFFER_SIZE * CHANNELS);
    int error;
    
    qDebug() << "Capture thread running";
    
    int frameCount = 0;
    while (m_running && m_stream) {
        int result = pa_simple_read(
            m_stream,
            buffer.data(),
            buffer.size() * sizeof(float),
            &error
        );
        
        if (result < 0) {
            if (m_running) {
                m_error = "Read error: " + std::string(pa_strerror(error));
                qWarning() << m_error.c_str();
            }
            break;
        }
        
        if (m_projectM && m_projectM->isInitialized()) {
            m_projectM->addPCMData(buffer.data(), BUFFER_SIZE);
            
            frameCount++;
            if (frameCount % 100 == 0) {
                qDebug() << "Fed" << frameCount << "audio frames to projectM";
            }
        }
    }
    
    qDebug() << "Capture thread exiting";
}
