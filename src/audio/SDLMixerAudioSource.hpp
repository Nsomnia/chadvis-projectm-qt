#pragma once
// SDLMixerAudioSource.hpp - SDL_mixer based audio source for projectM
// Provides reliable PCM data feeding via SDL_mixer's post-mix callback

#include "util/Types.hpp"
#include <projectM-4/projectM.h>
#include <string>

// Forward declare SDL types
struct Mix_Music;

namespace vc {

class SDLMixerAudioSource {
public:
    SDLMixerAudioSource();
    ~SDLMixerAudioSource();
    
    // Initialize with projectM handle and sample rate
    bool init(projectm_handle pM, int sampleRate = 44100);
    
    // Load and control playback
    bool loadFile(const std::string& path);
    void play();
    void pause();
    void resume();
    void stop();
    void setVolume(float volume);  // 0.0 - 1.0
    
    // Status
    bool isPlaying() const;
    bool isInitialized() const { return projectM_ != nullptr; }
    
    // Called by SDL callback - public so static callback can access it
    void processAudio(Uint8* stream, int len);
    
private:
    Mix_Music* music_;
    projectm_handle projectM_;
    int sampleRate_;
};

} // namespace vc
