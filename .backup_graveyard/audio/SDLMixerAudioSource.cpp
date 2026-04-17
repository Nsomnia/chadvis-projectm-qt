#include "SDLMixerAudioSource.hpp"
#include "core/Logger.hpp"
#include <SDL2/SDL_mixer.h>
#include <vector>

namespace vc {

// Global callback that feeds to projectM
static void sdlAudioCallback(void* userdata, Uint8* stream, int len) {
    SDLMixerAudioSource* source = static_cast<SDLMixerAudioSource*>(userdata);
    if (source) {
        source->processAudio(stream, len);
    }
}

SDLMixerAudioSource::SDLMixerAudioSource() 
    : music_(nullptr), projectM_(nullptr), sampleRate_(44100) {
}

SDLMixerAudioSource::~SDLMixerAudioSource() {
    stop();
    if (music_) {
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }
    Mix_CloseAudio();
}

bool SDLMixerAudioSource::init(projectm_handle pM, int sampleRate) {
    if (!pM) {
        LOG_ERROR("SDLMixerAudioSource: Invalid projectM handle");
        return false;
    }
    
    projectM_ = pM;
    sampleRate_ = sampleRate;
    
    // Initialize SDL_mixer
    if (Mix_OpenAudio(sampleRate_, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        LOG_ERROR("SDL_mixer init failed: {}", Mix_GetError());
        return false;
    }
    
    // Set up post-mix callback to get raw PCM
    Mix_SetPostMix(sdlAudioCallback, this);
    
    LOG_INFO("SDLMixerAudioSource initialized with sample rate: {} Hz", sampleRate_);
    return true;
}

bool SDLMixerAudioSource::loadFile(const std::string& path) {
    if (music_) {
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }
    
    music_ = Mix_LoadMUS(path.c_str());
    if (!music_) {
        LOG_ERROR("Failed to load music file {}: {}", path, Mix_GetError());
        return false;
    }
    
    LOG_INFO("Loaded audio file: {}", path);
    return true;
}

void SDLMixerAudioSource::play() {
    if (!music_) {
        LOG_WARN("No music loaded");
        return;
    }
    
    if (Mix_PlayMusic(music_, 0) == -1) {  // 0 = loop forever
        LOG_ERROR("Failed to play music: {}", Mix_GetError());
    } else {
        LOG_INFO("Playback started");
    }
}

void SDLMixerAudioSource::pause() {
    Mix_PauseMusic();
    LOG_INFO("Playback paused");
}

void SDLMixerAudioSource::resume() {
    Mix_ResumeMusic();
    LOG_INFO("Playback resumed");
}

void SDLMixerAudioSource::stop() {
    Mix_HaltMusic();
    LOG_INFO("Playback stopped");
}

void SDLMixerAudioSource::setVolume(float volume) {
    Mix_VolumeMusic(static_cast<int>(volume * MIX_MAX_VOLUME));
}

void SDLMixerAudioSource::processAudio(Uint8* stream, int len) {
    if (!projectM_) return;
    
    // Convert to float and feed to projectM
    int samples = len / (2 * sizeof(int16_t));  // Stereo, 16-bit
    int16_t* pcm_stream = reinterpret_cast<int16_t*>(stream);
    
    // Convert to float [-1.0, 1.0]
    std::vector<float> float_samples(len / sizeof(int16_t));
    for (int i = 0; i < len / 2; ++i) {
        float_samples[i] = static_cast<float>(pcm_stream[i]) / 32768.0f;
    }
    
    // Feed to projectM
    projectm_pcm_add_float(projectM_, float_samples.data(), samples, PROJECTM_STEREO);
    
    LOG_DEBUG("Fed {} samples to projectM", samples);
}

bool SDLMixerAudioSource::isPlaying() const {
    return Mix_PlayingMusic() && !Mix_PausedMusic();
}

} // namespace vc
