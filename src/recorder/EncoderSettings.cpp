#include "EncoderSettings.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vc {

namespace {

VideoCodec parseVideoCodec(std::string_view codec) {
    if (codec == "libx264" || codec == "h264")
        return VideoCodec::H264;
    if (codec == "libx265" || codec == "h265" || codec == "hevc")
        return VideoCodec::H265;
    if (codec == "libvpx-vp9" || codec == "vp9")
        return VideoCodec::VP9;
    if (codec == "libaom-av1" || codec == "av1")
        return VideoCodec::AV1;
    if (codec == "prores_ks" || codec == "prores")
        return VideoCodec::ProRes;
    if (codec == "ffv1")
        return VideoCodec::FFV1;
    if (codec == "h264_nvenc")
        return VideoCodec::H264_NVENC;
    if (codec == "hevc_nvenc" || codec == "h265_nvenc")
        return VideoCodec::H265_NVENC;
    if (codec == "h264_vaapi")
        return VideoCodec::H264_VAAPI;
    if (codec == "hevc_vaapi" || codec == "h265_vaapi")
        return VideoCodec::H265_VAAPI;
    return VideoCodec::H264;
}

AudioCodec parseAudioCodec(std::string_view codec) {
    if (codec == "aac")
        return AudioCodec::AAC;
    if (codec == "libopus" || codec == "opus")
        return AudioCodec::Opus;
    if (codec == "flac")
        return AudioCodec::FLAC;
    if (codec == "libmp3lame" || codec == "mp3")
        return AudioCodec::MP3;
    if (codec == "pcm_s16le" || codec == "pcm")
        return AudioCodec::PCM;
    return AudioCodec::AAC;
}

EncoderPreset parseEncoderPreset(std::string_view preset) {
    if (preset == "ultrafast") return EncoderPreset::Ultrafast;
    if (preset == "superfast") return EncoderPreset::Superfast;
    if (preset == "veryfast") return EncoderPreset::Veryfast;
    if (preset == "faster") return EncoderPreset::Faster;
    if (preset == "fast") return EncoderPreset::Fast;
    if (preset == "slow") return EncoderPreset::Slow;
    if (preset == "slower") return EncoderPreset::Slower;
    if (preset == "veryslow") return EncoderPreset::Veryslow;
    if (preset == "placebo") return EncoderPreset::Placebo;
    return EncoderPreset::Medium;
}

std::string formatDefaultFilename(std::string pattern) {
    if (pattern.empty())
        pattern = "chadvis-projectm-qt_{date}_{time}";

    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    std::ostringstream date;
    std::ostringstream clock;
    date << std::put_time(&local, "%Y-%m-%d");
    clock << std::put_time(&local, "%H-%M-%S");

    const auto replaceAll = [](std::string& value,
                              std::string_view token,
                              std::string_view replacement) {
        std::size_t pos = 0;
        while ((pos = value.find(token, pos)) != std::string::npos) {
            value.replace(pos, token.size(), replacement);
            pos += replacement.size();
        }
    };
    replaceAll(pattern, "{date}", date.str());
    replaceAll(pattern, "{time}", clock.str());
    return pattern;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string extensionFor(Container container) {
    switch (container) {
    case Container::MP4: return ".mp4";
    case Container::MKV: return ".mkv";
    case Container::WebM: return ".webm";
    case Container::MOV: return ".mov";
    case Container::AVI: return ".avi";
    }
    return ".mp4";
}

} // namespace

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
    case VideoCodec::H264_NVENC:
        return "h264_nvenc";
    case VideoCodec::H265_NVENC:
        return "hevc_nvenc";
    case VideoCodec::H264_VAAPI:
        return "h264_vaapi";
    case VideoCodec::H265_VAAPI:
        return "hevc_vaapi";
    }
    return "libx264";
}

bool VideoSettings::isHardwareAccelerated() const {
    return codec == VideoCodec::H264_NVENC ||
           codec == VideoCodec::H265_NVENC ||
           codec == VideoCodec::H264_VAAPI ||
           codec == VideoCodec::H265_VAAPI;
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
    case PixelFormat::NV12:
        return "nv12";
    case PixelFormat::P010LE:
        return "p010le";
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
    return extensionFor(container);
}

std::optional<Container> EncoderSettings::containerFromPath(const fs::path& path) {
    const auto extension = lowercase(path.extension().string());
    if (extension == ".mp4") return Container::MP4;
    if (extension == ".mkv") return Container::MKV;
    if (extension == ".webm") return Container::WebM;
    if (extension == ".mov") return Container::MOV;
    if (extension == ".avi") return Container::AVI;
    return std::nullopt;
}

fs::path EncoderSettings::outputPathForContainer(const fs::path& path,
                                                 Container container) {
    if (path.empty())
        return path;

    auto result = path;
    result.replace_extension(extensionFor(container));
    return result;
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
    settings.video.codec = parseVideoCodec(recCfg.video.codec);
    settings.video.width = recCfg.video.width;
    settings.video.height = recCfg.video.height;
    settings.video.fps = recCfg.video.fps;
    settings.video.crf = recCfg.video.crf;
    settings.video.preset = parseEncoderPreset(recCfg.video.preset);

    // Audio
    settings.audio.codec = parseAudioCodec(recCfg.audio.codec);
    settings.audio.bitrate = recCfg.audio.bitrate;
    settings.audio.sampleRate = recCfg.audio.sampleRate;
    settings.audio.channels = recCfg.audio.channels;

    // Container
    const auto container = lowercase(recCfg.container);
    if (container == "mkv")
        settings.container = Container::MKV;
    else if (container == "webm")
        settings.container = Container::WebM;
    else if (container == "mov")
        settings.container = Container::MOV;
    else if (container == "avi")
        settings.container = Container::AVI;
    else
        settings.container = Container::MP4;

    // An empty path means "use the configured output directory".  Build the
    // name here (rather than only in the dialog) so the encoder receives the
    // same concrete path that the UI will display after start.  Keep filename
    // sanitization centralized in FileUtils.
    const auto outputDirectory = recCfg.outputDirectory.empty()
        ? file::dataDir() / "recordings"
        : recCfg.outputDirectory;
    auto safeName = file::sanitizeFilename(
        formatDefaultFilename(recCfg.defaultFilename));
    fs::path safePath(safeName);
    // Replace only a recognized existing container suffix.  Ordinary dots in
    // a configured basename are part of the sanitized filename and must not
    // be mistaken for an extension to discard.
    if (containerFromPath(safePath)) {
        safeName = safePath.replace_extension("").string();
    }
    if (safeName.empty())
        safeName = "_";
    settings.outputPath = outputDirectory / (safeName + settings.containerExtension());

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

EncoderSettings EncoderSettings::hardware1080p60() {
    // NVIDIA NVENC 1080p60 preset
    EncoderSettings s;
    s.video.codec = VideoCodec::H264_NVENC;
    s.video.width = 1920;
    s.video.height = 1080;
    s.video.fps = 60;
    s.video.bitrate = 12000; // 12 Mbps for NVENC
    s.video.hwAccel = HWAccelDevice::NVENC;
    s.audio.codec = AudioCodec::AAC;
    s.audio.bitrate = 320;
    s.container = Container::MP4;
    s.comment = "Recorded with ChadVis using NVENC - I use Arch btw";
    return s;
}

EncoderSettings EncoderSettings::hardware4k60() {
    // NVIDIA NVENC 4K60 preset
    EncoderSettings s;
    s.video.codec = VideoCodec::H265_NVENC;
    s.video.width = 3840;
    s.video.height = 2160;
    s.video.fps = 60;
    s.video.bitrate = 40000; // 40 Mbps for 4K NVENC
    s.video.hwAccel = HWAccelDevice::NVENC;
    s.audio.codec = AudioCodec::AAC;
    s.audio.bitrate = 384;
    s.container = Container::MP4;
    s.comment = "Recorded with ChadVis using NVENC 4K - I use Arch btw";
    return s;
}

} // namespace vc