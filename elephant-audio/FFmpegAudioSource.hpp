// src/audio/FFmpegAudioSource.hpp
#pragma once

#include <string>
#include <memory>

namespace vc {

class FFmpegAudioSource {
public:
    FFmpegAudioSource();
    ~FFmpegAudioSource();
    
    bool open(const std::string& path);
    void close();
    
    bool isOpen() const;
    int getSampleRate() const;
    int getChannels() const;
    
    // Returns number of bytes read
    int read(void* buffer, int size);
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace vc