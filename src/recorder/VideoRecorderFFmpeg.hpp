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
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
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

  /**
   * @brief Open an output file and write its header.
   *
   * @param assSubtitle  A complete Advanced SubStation Alpha document, UTF-8,
   *                     as built by `vc::LyricsExport::toAssDocument` -- the
   *                     same bytes `LyricsBridge::assDocument` hands to QML. The
   *                     recorder receives the *document* and nothing else: it
   *                     has no LyricsData, and every decision about the ASS
   *                     format belongs to src/lyrics, which is a layer below
   *                     this one. Empty means "no lyrics".
   *
   * Attaching the track is best effort and can never fail the recording. It
   * needs a container that carries AV_CODEC_ID_ASS, a libavcodec built with the
   * `ass` encoder, and a document with at least one cue; when any of those is
   * missing the log says which, and the file is written without a subtitle
   * stream. Adding the stream unconditionally instead would make an MP4
   * recording fail outright -- libavformat's `init_muxer` refuses a codec the
   * container has no tag for -- which is why the capability is probed first.
   *
   * The view is not retained. The script becomes the subtitle codec's own
   * extradata copy and every cue body is copied into `subtitle_` before this
   * returns, so the caller's buffer only has to outlive the call.
   */
  Result<void> init(const EncoderSettings& settings,
                    std::string_view assSubtitle = {});
  void cleanup();

  std::string getOutputPath() const { return currentOutputPath_; }

  bool encodeVideo(const GrabbedFrame& frame, u64& bytesWritten);
  bool encodeAudio(std::vector<f32>& buffer, u32 channels, u64& bytesWritten);
  void flush(u64& bytesWritten);

private:
  Result<void> initVideoStream(const EncoderSettings& settings);
  Result<void> initAudioStream(const EncoderSettings& settings);

  /// Karaoke subtitle state.
  ///
  /// Opaque here on purpose. It is `std::vector<LyricsExport::AssEvent>` and
  /// friends, and naming that in this header would make src/recorder depend on
  /// the lyrics layer's types at all. The recorder's whole relationship to ASS
  /// is "here are some bytes", and the indirection is what keeps it that way.
  /// The definition lives in the .cpp, where the destructor already is, so the
  /// incomplete type is complete before it is ever released.
  struct SubtitleTrack;
  void initSubtitleStream(std::string_view assDocument);
  void writeSubtitlePacketsUpTo(std::int64_t millisecond, u64& bytesWritten);
  bool writeSubtitlePacket(u64& bytesWritten);

  // Hardware acceleration initialization
  Result<void> initHWDevice(const EncoderSettings& settings);
  Result<void> initHWFrames(const EncoderSettings& settings);
  AVPixelFormat getHWPixelFormat(const EncoderSettings& settings) const;

  bool encodeVideoFrame(AVFrame* frame, u64& bytesWritten);
  bool encodeAudioFrame(AVFrame* frame, u64& bytesWritten);
  /// Rescale a packet out of `sourceTimeBase` and hand it to the muxer.
  ///
  /// The source base is a parameter rather than being looked up from the stream
  /// because the stream is not the thing that knows it: it used to be inferred
  /// as "video if this is the video stream, otherwise audio", which is exactly
  /// the kind of ternary that quietly sends a third stream's timestamps through
  /// the wrong base the first time a third stream exists.
  bool writePacket(AVPacket* packet, AVStream* stream, AVRational sourceTimeBase,
                   u64& bytesWritten);

  /// Place a frame on the presentation timeline from its own capture time.
  ///
  /// GrabbedFrame::timestamp is microseconds off an unspecified monotonic
  /// origin, and the codec time base is {1, fps}, so the presentation time is
  /// the elapsed microseconds rescaled into frame ticks. A frame that
  /// FrameGrabber dropped never reaches this function, but its *successor*
  /// still carries the real capture time, so the missing frame interval shows
  /// up as a gap in the output instead of being renumbered away. Returns a
  /// strictly increasing tick count; see the member comments for the origin
  /// and monotonicity rules.
  i64 presentationTimestampFor(i64 captureTimestampUs);

  // Muxing only: allocated by avformat_alloc_output_context2, so the output
  // alias is the one that closes pb and frees. A demuxer context must use
  // AVFormatContextInPtr instead.
  AVFormatContextOutPtr formatCtx_;
  AVCodecContextPtr videoCodecCtx_;
  AVCodecContextPtr audioCodecCtx_;
  AVStream* videoStream_{nullptr};
  AVStream* audioStream_{nullptr};

  /// Null unless a subtitle track was requested *and* attached. The AVStream
  /// it points at belongs to formatCtx_, not to it -- only the codec context
  /// and the cue bodies are ours to release.
  std::unique_ptr<SubtitleTrack> subtitle_;

  /// True between a successful avformat_write_header and cleanup(). The gate
  /// for writing a late subtitle packet: a context that failed at avio_open has
  /// no pb to write to, and one that failed at write_header has a pb but no
  /// header, and neither is a muxer that will accept a packet.
  bool headerWritten_{false};

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

  // ── Presentation timeline ─────────────────────────────────────────────────
  // The first frame with a real capture time anchors the origin and is
  // normalised to PTS 0, so the file never starts at an arbitrary offset
  // (a steady_clock reading is microseconds since boot, which is a huge and
  // machine-specific number to hand a muxer as a start time).
  bool haveVideoOrigin_{false};
  i64 videoOriginUs_{0};
  // Previous *timestamped* frame, so an un-timestamped arrival can be stepped
  // by the most recently observed real cadence instead of by a hardcoded tick.
  bool havePreviousCaptureUs_{false};
  i64 previousCaptureUs_{0};
  i64 lastVideoStepUs_{0};
  // Last PTS handed to the encoder, as the strict floor for the next one.
  i64 lastVideoPts_{-1};
  // One-shot diagnostic: a frame reached the encoder with no capture time.
  bool warnedMissingCaptureTime_{false};

  u64 audioFrameCount_{0};

  int fileLockFd_{-1};
  std::string currentOutputPath_;
};

} // namespace vc
