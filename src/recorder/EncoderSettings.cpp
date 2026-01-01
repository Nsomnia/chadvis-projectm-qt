#include "EncoderSettings.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc {

std::string VideoSettings::codecName() const {
    switch (codec) {
    case VideoCodec::H264:
        return "libx264";
    case VideoCodec::H265:
        return "libx265";
    case VideoCodec::VP9:
        return "libvpx-vp9";
    case VideoCodec::AV1:
        return "libaom-av1";
    case VideoCodec::ProRes:
        return "prores_ks";
    case VideoCodec::FFV1:
        return "ffv1";
    }
    return "libx264";
}

std::string VideoSettings::presetName() const {
    switch (preset) {
    case EncoderPreset::Ultrafast:
        return "ultrafast";
    case EncoderPreset::Superfast:
        return "superfast";
    case EncoderPreset::Veryfast:
        return "veryfast";
    case EncoderPreset::Faster:
        return "faster";
    case EncoderPreset::Fast:
        return "fast";
    case EncoderPreset::Medium:
        return "medium";
    case EncoderPreset::Slow:
        return "slow";
    case EncoderPreset::Slower:
        return "slower";
    case EncoderPreset::Veryslow:
        return "veryslow";
    case EncoderPreset::Placebo:
        return "placebo";
    }
    return "medium";
}

std::string VideoSettings::pixelFormatName() const {
    switch (pixelFormat) {
    case PixelFormat::YUV420P:
        return "yuv420p";
    case PixelFormat::YUV422P:
        return "yuv422p";
    case PixelFormat::YUV444P:
        return "yuv444p";
    case PixelFormat::RGB24:
        return "rgb24";
    }
    return "yuv420p";
}

std::string AudioSettings::codecName() const {
    switch (codec) {
    case AudioCodec::AAC:
        return "aac";
    case AudioCodec::Opus:
        return "libopus";
    case AudioCodec::FLAC:
        return "flac";
    case AudioCodec::MP3:
        return "libmp3lame";
    case AudioCodec::PCM:
        return "pcm_s16le";
    }
    return "aac";
}

std::string EncoderSettings::containerExtension() const {
    switch (container) {
    case Container::MP4:
        return ".mp4";
    case Container::MKV:
        return ".mkv";
    case Container::WebM:
        return ".webm";
    case Container::MOV:
        return ".mov";
    case Container::AVI:
        return ".avi";
    }
    return ".mp4";
}

Result<void> EncoderSettings::validate() const {
    // Check codec/container compatibility
    if (container == Container::WebM) {
        if (video.codec != VideoCodec::VP9 && video.codec != VideoCodec::AV1) {
            return Result<void>::err("WebM requires VP9 or AV1 video codec");
        }
        if (audio.codec != AudioCodec::Opus) {
            return Result<void>::err("WebM requires Opus audio codec");
        }
    }

    if (container == Container::MP4 || container == Container::MOV) {
        if (video.codec == VideoCodec::VP9 || video.codec == VideoCodec::FFV1) {
            return Result<void>::err("MP4/MOV doesn't support VP9 or FFV1");
        }
    }

    // Check dimensions
    if (video.width == 0 || video.height == 0) {
        return Result<void>::err("Invalid video dimensions");
    }

    if (video.width % 2 != 0 || video.height % 2 != 0) {
        return Result<void>::err("Video dimensions must be even numbers");
    }

    // Check CRF range
    if (video.crf > 51) {
        return Result<void>::err("CRF must be between 0 and 51");
    }

    return Result<void>::ok();
}

EncoderSettings EncoderSettings::fromConfig() {
    EncoderSettings settings;
    const auto& recCfg = CONFIG.recording();

    // Video
    if (recCfg.video.codec == "libx264" || recCfg.video.codec == "h264") {
        settings.video.codec = VideoCodec::H264;
    } else if (recCfg.video.codec == "libx265" ||
               recCfg.video.codec == "h265") {
        settings.video.codec = VideoCodec::H265;
    } else if (recCfg.video.codec == "libvpx-vp9" ||
               recCfg.video.codec == "vp9") {
        settings.video.codec = VideoCodec::VP9;
    }

    settings.video.width = recCfg.video.width;
    settings.video.height = recCfg.video.height;
    settings.video.fps = recCfg.video.fps;
    settings.video.crf = 23; // Default to 23 for better performance on N4500

    // Parse preset
    std::string preset = recCfg.video.preset;
    if (preset == "ultrafast")
        settings.video.preset = EncoderPreset::Ultrafast;
    else if (preset == "superfast")
        settings.video.preset = EncoderPreset::Superfast;
    else if (preset == "veryfast")
        settings.video.preset = EncoderPreset::Veryfast;
    else if (preset == "faster")
        settings.video.preset = EncoderPreset::Faster;
    else if (preset == "fast")
        settings.video.preset = EncoderPreset::Fast;
    else if (preset == "medium")
        settings.video.preset = EncoderPreset::Medium;
    else if (preset == "slow")
        settings.video.preset = EncoderPreset::Slow;
    else if (preset == "slower")
        settings.video.preset = EncoderPreset::Slower;
    else if (preset == "veryslow")
        settings.video.preset = EncoderPreset::Veryslow;

    // Audio
    settings.audio.codec = AudioCodec::AAC;
    settings.audio.bitrate = recCfg.audio.bitrate;

    // Container
    if (recCfg.container == "mp4")
        settings.container = Container::MP4;
    else if (recCfg.container == "mkv")
        settings.container = Container::MKV;
    else if (recCfg.container == "webm")
        settings.container = Container::WebM;
    else if (recCfg.container == "mov")
        settings.container = Container::MOV;

    return settings;
}

EncoderSettings EncoderSettings::youtube1080p60() {
    EncoderSettings s;
    s.video.codec = VideoCodec::H264;
    s.video.width = 1920;
    s.video.height = 1080;
    s.video.fps = 60;
    s.video.crf = 18;
    s.video.preset = EncoderPreset::Slow;
    s.video.bFrames = 2;
    s.audio.codec = AudioCodec::AAC;
    s.audio.bitrate = 320;
    s.container = Container::MP4;
    return s;
}

EncoderSettings EncoderSettings::youtube4k60() {
    EncoderSettings s;
    s.video.codec = VideoCodec::H264;
    s.video.width = 3840;
    s.video.height = 2160;
    s.video.fps = 60;
    s.video.crf = 18;
    s.video.preset = EncoderPreset::Medium;
    s.audio.codec = AudioCodec::AAC;
    s.audio.bitrate = 384;
    s.container = Container::MP4;
    return s;
}

EncoderSettings EncoderSettings::twitter720p() {
    EncoderSettings s;
    s.video.codec = VideoCodec::H264;
    s.video.width = 1280;
    s.video.height = 720;
    s.video.fps = 30;
    s.video.crf = 23;
    s.video.preset = EncoderPreset::Fast;
    s.audio.codec = AudioCodec::AAC;
    s.audio.bitrate = 192;
    s.container = Container::MP4;
    return s;
}

EncoderSettings EncoderSettings::discord8mb() {
    // Aim for 8MB file for Discord free tier
    EncoderSettings s;
    s.video.codec = VideoCodec::H264;
    s.video.width = 1280;
    s.video.height = 720;
    s.video.fps = 30;
    s.video.crf = 28; // Lower quality for size
    s.video.preset = EncoderPreset::Veryfast;
    s.audio.codec = AudioCodec::AAC;
    s.audio.bitrate = 128;
    s.container = Container::MP4;
    return s;
}

EncoderSettings EncoderSettings::lossless() {
    EncoderSettings s;
    s.video.codec = VideoCodec::FFV1;
    s.video.width = 1920;
    s.video.height = 1080;
    s.video.fps = 60;
    s.video.pixelFormat = PixelFormat::RGB24;
    s.audio.codec = AudioCodec::FLAC;
    s.container = Container::MKV;
    return s;
}

EncoderSettings EncoderSettings::editing() {
    // ProRes for video editing
    EncoderSettings s;
    s.video.codec = VideoCodec::ProRes;
    s.video.width = 1920;
    s.video.height = 1080;
    s.video.fps = 60;
    s.video.pixelFormat = PixelFormat::YUV422P;
    s.audio.codec = AudioCodec::PCM;
    s.container = Container::MOV;
    return s;
}

std::vector<QualityPreset> getQualityPresets() {
    return {
            {"YouTube 1080p60",
             "High quality for YouTube uploads",
             EncoderSettings::youtube1080p60()},
            {"YouTube 4K60",
             "Maximum quality for 4K displays",
             EncoderSettings::youtube4k60()},
            {"Twitter/X 720p",
             "Optimized for Twitter video",
             EncoderSettings::twitter720p()},
            {"Discord 8MB",
             "Compressed for Discord free tier",
             EncoderSettings::discord8mb()},
            {"Lossless",
             "No quality loss, huge files",
             EncoderSettings::lossless()},
            {"Editing (ProRes)",
             "For video editing software",
             EncoderSettings::editing()},
    };
}

} // namespace vc