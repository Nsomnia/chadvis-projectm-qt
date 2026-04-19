/**
* @file VideoRecorderFFmpeg.hpp
* @brief Low-level FFmpeg integration for video recording with HW accel support.
* @version 2.1.0 - 2026-04-14 14:22:00 MDT
*
* This file defines the VideoRecorderFFmpeg class which handles the direct
* interaction with FFmpeg libraries (libavcodec, libavformat, etc.).
* It manages codecs, contexts, scaling, resampling, and file I/O.
*
* Supports hardware acceleration via:
* - NVIDIA NVENC (h264_nvenc, hevc_nvenc)
* - Intel/AMD VAAPI (h264_vaapi, hevc_vaapi)
* - Intel QuickSync (qsv)
* - AMD AMF (h264_amf, hevc_amf)
*
* @section Dependencies
* - FFmpeg 60+ (libavcodec, libavformat, libswscale, libswresample, libavutil)
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

extern "C" {
#include <libavutil/hwcontext.h>
}

namespace vc {

/**
 * @brief Hardware device context wrapper for RAII management.
 * Encapsulates AVBufferRef* for hardware device contexts (CUDA, VAAPI, etc.)
 */
struct HWDeviceContextDeleter {
  void operator()(AVBufferRef* ctx) const {
    if (ctx) av_buffer_unref(&ctx);
  }
};
using HWDeviceContextPtr = std::unique_ptr<AVBufferRef, HWDeviceContextDeleter>;

/**
 * @brief Hardware frames context wrapper for RAII management.
 * Encapsulates AVBufferRef* for hardware frames contexts.
 */
struct HWFramesContextDeleter {
  void operator()(AVBufferRef* ctx) const {
    if (ctx) av_buffer_unref(&ctx);
  }
};
using HWFramesContextPtr = std::unique_ptr<AVBufferRef, HWFramesContextDeleter>;

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

  // Hardware acceleration initialization
  Result<void> initHWDevice(const EncoderSettings& settings);
  Result<void> initHWFrames(const EncoderSettings& settings);
  AVPixelFormat getHWPixelFormat(const EncoderSettings& settings) const;

  bool encodeVideoFrame(AVFrame* frame, u64& bytesWritten);
  bool encodeAudioFrame(AVFrame* frame, u64& bytesWritten);
  bool writePacket(AVPacket* packet, AVStream* stream, u64& bytesWritten);

  AVFormatContextPtr formatCtx_;
  AVCodecContextPtr videoCodecCtx_;
  AVCodecContextPtr audioCodecCtx_;
  AVStream* videoStream_{nullptr};
  AVStream* audioStream_{nullptr};

  // Hardware acceleration contexts
  HWDeviceContextPtr hwDeviceCtx_;
  HWFramesContextPtr hwFramesCtx_;
  AVPixelFormat swPixelFormat_{AV_PIX_FMT_YUV420P};

  SwsContextPtr swsCtx_;
  SwrContextPtr swrCtx_;

  AVFramePtr videoFrame_;
  AVFramePtr hwFrame_;         // Hardware frame for encoding
  AVFramePtr audioFrame_;
  AVPacketPtr packet_;

  std::mutex mutex_;
  u64 videoFrameCount_{0};
  u64 audioFrameCount_{0};

  int fileLockFd_{-1};
  std::string currentOutputPath_;
};

} // namespace vc
