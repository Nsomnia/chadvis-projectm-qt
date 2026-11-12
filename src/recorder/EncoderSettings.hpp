#pragma once
// EncoderSettings.hpp - Video/audio encoding configuration
// All the knobs you can turn to make your file smaller or prettier

#include "util/Types.hpp"
#include "util/Result.hpp"
#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace vc {

// Video codec options
enum class VideoCodec {
    H264,       // libx264 - most compatible
    H265,       // libx265 - better compression, slower
    VP9,        // libvpx-vp9 - good for web
    AV1,        // libaom-av1 - best compression, slowest
    ProRes,     // prores_ks - for editing
    FFV1,       // lossless
    H264_NVENC, // NVIDIA hardware encoding
    H265_NVENC, // NVIDIA HEVC hardware encoding
    H264_VAAPI, // Intel/AMD hardware encoding via VAAPI
    H265_VAAPI  // Intel/AMD HEVC hardware encoding via VAAPI
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

/// The canonical encoder-preset slugs, in EncoderPreset declaration order.
/// Single source of truth for presetName(), config parsing, render-job parsing
/// and the Settings QML model -- the four used to keep separate hand-written
/// lists (T0124), which is how `placebo` ended up missing from the UI while
/// C++ accepted it. Index with static_cast<usize>(preset).
inline constexpr std::array<std::string_view, 10> kEncoderPresetSlugs = {
    "ultrafast", "superfast", "veryfast", "faster", "fast",
    "medium",    "slow",      "slower",   "veryslow", "placebo"};

[[nodiscard]] constexpr std::string_view encoderPresetSlug(EncoderPreset preset) noexcept {
    const auto i = static_cast<usize>(preset);
    return i < kEncoderPresetSlugs.size() ? kEncoderPresetSlugs[i]
                                          : kEncoderPresetSlugs[static_cast<usize>(EncoderPreset::Medium)];
}

/// Unknown or empty slugs fall back to Medium, matching the historical default
/// of every parser this replaces.
[[nodiscard]] constexpr EncoderPreset encoderPresetFromSlug(std::string_view slug) noexcept {
    for (usize i = 0; i < kEncoderPresetSlugs.size(); ++i) {
        if (slug == kEncoderPresetSlugs[i]) return static_cast<EncoderPreset>(i);
    }
    return EncoderPreset::Medium;
}

// Pixel format
enum class PixelFormat {
    YUV420P,    // Most compatible
    YUV422P,    // Better color, larger
    YUV444P,    // Best color, largest
    RGB24,      // For lossless
    NV12,       // Hardware acceleration format
    P010LE      // 10-bit hardware format for HEVC
};

// Hardware acceleration device
enum class HWAccelDevice {
    None,       // Software encoding
    NVENC,      // NVIDIA GPU encoding
    VAAPI,      // Intel/AMD via VAAPI
    AMF,        // AMD Advanced Media Framework
    QuickSync   // Intel QuickSync
};

struct VideoSettings {
    VideoCodec codec{VideoCodec::H264};
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    u32 crf{18};                // Quality: 0-51, lower is better (software only)
    u32 bitrate{0};             // 0 = use CRF for software, auto for hardware
    EncoderPreset preset{EncoderPreset::Medium};
    PixelFormat pixelFormat{PixelFormat::YUV420P};
    u32 gopSize{0};             // 0 = auto (fps * 2)
    u32 bFrames{3};
    bool twoPass{false};
    
    // Hardware acceleration
    HWAccelDevice hwAccel{HWAccelDevice::None};
    u32 hwDevice{0};            // GPU device index (for multi-GPU systems)
    
    // Codec-specific options
    std::string extraOptions;
    
    // Get FFmpeg codec name
    std::string codecName() const;
    std::string presetName() const;
    std::string pixelFormatName() const;
    bool isHardwareAccelerated() const;
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
    
    // Create from config, including a sanitized default output filename.
    static EncoderSettings fromConfig();

    // Infer the container selected by a save dialog from its extension.
    static std::optional<Container> containerFromPath(const fs::path& path);

    // Keep the selected filename but force the extension for the chosen
    // container.  This is used for both dialog paths and generated paths.
    static fs::path outputPathForContainer(const fs::path& path,
                                            Container container);


    // Presets
    static EncoderSettings youtube1080p60();
    static EncoderSettings youtube4k60();
    static EncoderSettings twitter720p();
    static EncoderSettings discord8mb();
    static EncoderSettings lossless();
    static EncoderSettings editing();
    static EncoderSettings hardware1080p60();  // NVENC/VAAPI
    static EncoderSettings hardware4k60();     // NVENC HEVC
};

} // namespace vc