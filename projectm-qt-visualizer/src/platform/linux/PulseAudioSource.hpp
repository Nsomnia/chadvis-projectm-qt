/**
 * @file PulseAudioSource.hpp
 * @brief PulseAudio/PipeWire audio capture for projectM
 *
 * Captures system audio via PulseAudio and feeds it to projectM.
 * Works with both PulseAudio and PipeWire (via pipewire-pulse compatibility layer).
 *
 * @note Requires libpulse development package
 */
#ifndef PULSEAUDIOSOURCE_HPP
#define PULSEAUDIOSOURCE_HPP

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <string>

class ProjectMWrapper;

class PulseAudioSource
{
public:
    PulseAudioSource(ProjectMWrapper* projectM);
    ~PulseAudioSource();
    
    // Start capturing system audio
    bool start();
    
    // Stop capturing
    void stop();
    
    // Is actively capturing?
    bool isRunning() const { return m_running; }
    
    // Get error message if any
    std::string getError() const { return m_error; }

private:
    void captureThread();
    
    ProjectMWrapper* m_projectM;
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
    std::string m_error;
    
    // PulseAudio stream
    pa_simple* m_stream{nullptr};
    
    // Audio format
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int BUFFER_SIZE = 1024; // Samples per channel
};

#endif // PULSEAUDIOSOURCE_HPP
