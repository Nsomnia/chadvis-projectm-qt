#pragma once
// FFmpegAudioSource.hpp - FFmpeg-based audio source for projectM
// Decodes audio files and feeds PCM data to projectM

#include "util/Types.hpp"
#include <projectM-4/projectM.h>
#include <string>
#include <memory>
#include <thread>
#include <vector>

namespace vc {

class FFmpegAudioSource {
public:
    FFmpegAudioSource();
    ~FFmpegAudioSource();
    
    // Initialize with projectM handle and target sample rate
    bool init(projectm_handle pM, int sampleRate = 48000);
    
    // Load audio file
    bool loadFile(const std::string& path);
    
    // Playback control
    void play();
    void pause();
    void resume();
    void stop();
    
    // Status
    bool isPlaying() const { return isPlaying_; }
    bool isPaused() const { return isPaused_; }
    
private:
    struct Private;
    std::unique_ptr<Private> d;
    
    std::thread decodeThread_;
    void decodeLoop();
    void cleanup();
    
    bool isPlaying_ = false;
    bool isPaused_ = false;
};

} // namespace vc
