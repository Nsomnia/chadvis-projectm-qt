#pragma once
// EncoderSettings.hpp - Video/audio encoding configuration
// All the knobs you can turn to make your file smaller or prettier

#include "util/Types.hpp"
#include "util/Result.hpp"
#include <vector>

namespace vc {

// Video codec options
enum class VideoCodec {
    H264,       // libx264 - most compatible
    H265,       // libx265 - better compression, slower
    VP9,        // libvpx-vp9 - good for web
    AV1,        // libaom-av1 - best compression, slowest
    ProRes,     // prores_ks - for editing
    FFV1        // lossless
};

// Audio codec options
enum class AudioCodec {
    AAC,        // Most compatible
    Opus,       // Best quality/size for web
    FLAC,       // Lossless
    MP3,        // Legacy compatibility
    PCM         // Uncompressed
};

// Container format
enum class Container {
    MP4,
    MKV,
    WebM,
    MOV,
    AVI
};

// Encoder speed preset
enum class EncoderPreset {
    Ultrafast,
    Superfast,
    Veryfast,
    Faster,
    Fast,
    Medium,
    Slow,
    Slower,
    Veryslow,
    Placebo     // For when you have a week to encode
};

// Pixel format
enum class PixelFormat {
    YUV420P,    // Most compatible
    YUV422P,    // Better color, larger
    YUV444P,    // Best color, largest
    RGB24       // For lossless
};

struct VideoSettings {
    VideoCodec codec{VideoCodec::H264};
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    u32 crf{18};                // Quality: 0-51, lower is better
    u32 bitrate{0};             // 0 = use CRF
    EncoderPreset preset{EncoderPreset::Medium};
    PixelFormat pixelFormat{PixelFormat::YUV420P};
    u32 gopSize{0};             // 0 = auto (fps * 2)
    u32 bFrames{3};
    bool twoPass{false};
    
    // Codec-specific options
    std::string extraOptions;
    
    // Get FFmpeg codec name
    std::string codecName() const;
    std::string presetName() const;
    std::string pixelFormatName() const;
};

struct AudioSettings {
    AudioCodec codec{AudioCodec::AAC};
    u32 sampleRate{48000};
    u32 channels{2};
    u32 bitrate{320};           // kbps
    
    // Get FFmpeg codec name
    std::string codecName() const;
};

struct EncoderSettings {
    VideoSettings video;
    AudioSettings audio;
    Container container{Container::MP4};
    fs::path outputPath;
    
    // Metadata
    std::string title;
    std::string artist;
    std::string comment{"Recorded with ChadVis - I use Arch btw"};
    
    // Get container extension
    std::string containerExtension() const;
    
    // Validate settings compatibility
    Result<void> validate() const;
    
    // Create from config
    static EncoderSettings fromConfig();
    
    // Presets
    static EncoderSettings youtube1080p60();
    static EncoderSettings youtube4k60();
    static EncoderSettings twitter720p();
    static EncoderSettings discord8mb();
    static EncoderSettings lossless();
    static EncoderSettings editing();
};

// Quality presets
struct QualityPreset {
    std::string name;
    std::string description;
    EncoderSettings settings;
};

std::vector<QualityPreset> getQualityPresets();

} // namespace vc