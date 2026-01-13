/**
 * @file VideoRecorderFFmpeg.hpp
 * @brief Low-level FFmpeg integration for video recording.
 *
 * This file defines the VideoRecorderFFmpeg class which handles the direct
 * interaction with FFmpeg libraries (libavcodec, libavformat, etc.).
 * It manages codecs, contexts, scaling, resampling, and file I/O.
 *
 * @section Dependencies
 * - FFmpeg (libavcodec, libavformat, libswscale, libswresample)
 *
 * @section Patterns
 * - Wrapper/Adapter: Wraps C-style FFmpeg API in a C++ class.
 * - RAII: Manages FFmpeg resources via smart pointers (AVFramePtr, etc.).
 */

#pragma once
#include <mutex>
#include <vector>
#include "EncoderSettings.hpp"
#include "FFmpegUtils.hpp"
#include "FrameGrabber.hpp"
#include "util/Result.hpp"

namespace vc {

class VideoRecorderFFmpeg {
public:
    VideoRecorderFFmpeg();
    ~VideoRecorderFFmpeg();

    Result<void> init(const EncoderSettings& settings);
    void cleanup();

    bool encodeVideo(const GrabbedFrame& frame, u64& bytesWritten);
    bool encodeAudio(std::vector<f32>& buffer, u32 channels, u64& bytesWritten);
    void flush(u64& bytesWritten);

private:
    Result<void> initVideoStream(const EncoderSettings& settings);
    Result<void> initAudioStream(const EncoderSettings& settings);

    bool encodeVideoFrame(AVFrame* frame, u64& bytesWritten);
    bool encodeAudioFrame(AVFrame* frame, u64& bytesWritten);
    bool writePacket(AVPacket* packet, AVStream* stream, u64& bytesWritten);

    AVFormatContextPtr formatCtx_;
    AVCodecContextPtr videoCodecCtx_;
    AVCodecContextPtr audioCodecCtx_;
    AVStream* videoStream_{nullptr};
    AVStream* audioStream_{nullptr};

    SwsContextPtr swsCtx_;
    SwrContextPtr swrCtx_;

    AVFramePtr videoFrame_;
    AVFramePtr audioFrame_;
    AVPacketPtr packet_;

    i64 videoFrameCount_{0};
    i64 audioFrameCount_{0};

    std::mutex mutex_;
};

} // namespace vc
