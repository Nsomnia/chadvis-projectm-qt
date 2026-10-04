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
#include <atomic>
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

  /// Which kind of "no audio came out" an encodeAudio call hit. A bool could not
  /// carry this; see encodeAudio.
  enum class AudioEncodeOutcome {
    /// At least one frame's packets all reached the file.
    Encoded,
    /// Nothing reached the file and nothing is wrong: the batch was empty, every
    /// frame was dropped by the resampler, or the muxer had already refused a
    /// packet and encodeAudio short-circuited.
    NoProgress,
    /// The audio encoder itself refused a frame.
    Failed
  };

  /**
   * @brief Encode one batch of decoded audio, reporting *which kind* of nothing
   * happened.
   *
   * The two ways a batch can produce no audio are opposites. A resampler that
   * refused every frame left a recording whose file is intact and audibly
   * degraded -- which the drop path already reports, once, in its own words, at
   * stop() -- while an encoder that refused a frame is a genuine fault. Collapsing
   * them into one `false` made the encoding loop emit "Encoding error occurred"
   * up to a hundred times a second for a drop-only batch: it told the user the
   * recording had broken, and then, at stop(), told them the file was fine.
   */
  AudioEncodeOutcome encodeAudio(std::vector<f32>& buffer, u32 channels,
                                 u64& bytesWritten);
  void flush(u64& bytesWritten);

  /**
   * @brief Has the muxer refused a packet for a reason that damages the file?
   *
   * Sticky once set: the container cannot be repaired after a hole, so every
   * later packet is into a file nobody should trust. `VideoRecorder::stop`
   * reads this to refuse to report a plausible-looking success.
   */
  bool writeFailed() const { return writeFailed_; }

  /**
   * @brief True exactly once, on the first damaging write failure.
   *
   * The one-shot shape is what lets the encoding loop report a single error
   * instead of one per frame for the rest of the recording, and it matches the
   * `warnedMissingCaptureTime_` pattern already in this class.
   */
  bool reportWriteFailure();

  /**
   * @brief Test seam: make the muxer refuse, where the refusal happens.
   *
   * Injected as the *return code* handed to `writePacket`'s classifier, not as a
   * separate code path, so the production handling of a refusal -- the
   * `case WriteOutcome::Failed` arm, the sticky flag, the frame counter standing
   * still, the throttled log, and stop() refusing to call it complete -- is the
   * code under test rather than a rehearsal of it. The earlier version of this
   * seam assigned `writeFailed_` directly, which set the *consequence* and left
   * the site that produces it unexecuted by any test anywhere.
   *
   * `av_interleaved_write_frame`'s failure still cannot be provoked for real, and
   * the reasons are the three rejected mechanisms in the commit history: a full
   * filesystem, a descriptor closed behind AVIO's back, or an RLIMIT_FSIZE that
   * would also trip the logger inside the same process.
   *
   * @param writesBeforeFailure  Writes allowed through before one is refused.
   *                             Zero -- the default -- refuses the very next
   *                             write, which is the ENOSPC-at-zero-bytes case: a
   *                             header went out, the first packet did not.
   *
   * One refusal, then the seam disarms. What a full volume does *afterwards* is
   * writeFailed_'s job -- encodeVideo and encodeAudio short-circuit on it, flush()
   * is gated on it, and reportWriteFailure() is the one-shot -- so a seam that
   * refused every later write would add nothing but an unobservable: a refused
   * packet contributes no bytes, so a permanently refusing muxer looks exactly
   * like a write path that did not run at all, and the gate could not be tested.
   */
  void simulateWriteFailureForTesting(u64 writesBeforeFailure = 0) {
    writesBeforeWriteFailure_.store(writesBeforeFailure, std::memory_order_release);
    writeFailureArmed_.store(true, std::memory_order_release);
  }

  /**
   * @brief What the resampler cost this recording.
   *
   * Deliberately *not* folded into `writeFailed()`. A muxer failure damages the
   * file -- the container, the index, every stream -- and a resampler failure
   * damages one track's contents: the container still has a header, a trailer and
   * a valid audio stream, and it decodes. The user gets a recording that plays
   * with gaps in the sound, and the two conditions deserve different words.
   */
  struct AudioResampleReport {
    /// Audio frames swr_convert refused, and which were therefore left out.
    u64 droppedFrames{0};
    /// Did any audio frame reach the file? False together with a non-zero
    /// droppedFrames means the audio track is empty, which is a different severity
    /// from "there are gaps in it".
    ///
    /// This used to be `audioFrameCount_ > 0`, and that counter is the PTS
    /// seed -- it is incremented *before* the frame is encoded, and the encode
    /// can fail with the packet not in the file. So it answered "did the
    /// encoder accept a frame at all", and could report an essentially empty
    /// audio track as encoded; stop() then attributed every hole in the sound to
    /// the resampler when some of them were mux losses. The distinction is
    /// load-bearing, so it is counted separately (`audioFramesEncoded_`) rather
    /// than renamed.
    bool anyFrameWritten{false};
  };

  /// The above, read live by the encoding thread and once more at finalisation.
  /// See the note on the members about why the drop counter is atomic and why
  /// cleanup() clears them.
  AudioResampleReport audioResampleReport() const {
    return AudioResampleReport{audioFramesDropped_.load(std::memory_order_acquire),
                               audioFramesEncoded_ > 0};
  }

  /**
   * @brief Test seam: make the next `frames` resample attempts fail.
   *
   * Injected as the *return value* of the call to swr_convert, not as a
   * separate code path, so the production handling of a resampler failure --
   * the counter, the log throttle, the stop() message -- is the code under test
   * and not a rehearsal of it. For the same reason as the muxer seam, this
   * cannot be provoked for real: swr_convert fails on a resampler that was
   * configured with an impossible conversion, and round 5 made the alternative
   * (fixing the configuration) a hard error at init, so there is no reachable
   * state in which a healthy recording hits this.
   */
  void simulateResampleFailureForTesting(u32 frames) {
    injectedResampleFailures_.store(frames, std::memory_order_release);
  }

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

  /// What one avcodec_send_frame, plus the drain that follows it, actually did.
  ///
  /// A bool cannot carry this, and the case it could not carry was the one that
  /// lied. `avcodec_receive_packet` returning `AVERROR(EAGAIN)` on the first
  /// call means the encoder is still *holding* the frame, which every codec with
  /// `AV_CODEC_CAP_DELAY` does for its first frames: measured against this
  /// project's FFmpeg (libavcodec 63.1.102), the native `aac` encoder reports
  /// `initial_padding = 1024` and emits zero packets for send #0, one for each
  /// send after. "Every packet this frame produced is in the file" was therefore
  /// vacuously true for a frame that produced nothing at all, and
  /// `AudioResampleReport::anyFrameWritten` -- which is an *existential* claim,
  /// "did any audio frame reach the file" -- was told yes by a frame that
  /// reached nothing.
  enum class FrameEncodeOutcome {
    /// The encoder is still holding the frame: no packet came out, and nothing
    /// is wrong. Neither progress nor a fault, which is why it is a third state
    /// rather than a false.
    Deferred,
    /// At least one packet came out and every one of them is in the file.
    Written,
    /// Nothing this frame produced is known to be in the file: either a packet
    /// the muxer would not take, or the encoder refused the frame outright.
    Lost,
  };

  /// Send one frame and write everything the encoder gives back for it.
  ///
  /// One implementation for both streams. The two bodies were identical apart
  /// from which codec context and time base they named -- exactly the shape that
  /// lets two copies drift, and they had, on the return value.
  FrameEncodeOutcome encodeOneFrame(AVCodecContext* codecCtx, AVStream* stream,
                                    AVFrame* frame, u64& bytesWritten);

  /// One resample into audioFrame_.data, or a simulated failure. Everything
  /// downstream of the return value is the production path in both cases.
  int resampleIntoAudioFrame(AVFrame* frame, const u8* const* srcData,
                             int frameSize);
  /// Charge one write against the test seam's countdown. True means "refuse this
  /// one", i.e. the caller substitutes a muxer error for the real call. See
  /// simulateWriteFailureForTesting; atomic because it can be armed from the
  /// test's thread while the encoding thread is inside a write.
  bool consumeInjectedWriteFailure();
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
  ///
  /// The same gate now covers av_write_trailer and flush(), which both used to
  /// write into a context that had never had a header -- an init failure at
  /// avio_open or write_header left a context that either had no pb at all or a
  /// pb and no header, and cleanup() then asked the muxer to finalise it.
  bool headerWritten_{false};

  /// Sticky: see writeFailed(). Reset only by cleanup().
  ///
  /// Atomic because writeFailed() is read from the GUI thread (VideoRecorder::
  /// stop, after the join) and, under the test seam, written from the test's
  /// thread while the encoding thread is inside encodeVideo. The flag is the one
  /// piece of recorder state that legitimately crosses that boundary.
  std::atomic<bool> writeFailed_{false};
  /// Cleared by reportWriteFailure() so exactly one caller reports it. Also the
  /// one-shot for the refusal log inside writePacket, because `Failed` is
  /// reported per *occurrence* through a throttled count (writeFailures_) and
  /// latching this instead would suppress the log for the whole recording.
  bool writeFailureReported_{false};

  /// Audio frames the resampler refused. Atomic for the same reason
  /// writeFailed_ is: read by the GUI thread at finalisation, and under the test
  /// seam written from the test's thread while the encoding thread is inside
  /// encodeAudio. Reset by cleanup(), which is why the report is snapshotted
  /// before it runs. Also read live by the encoding thread's stats block, which
  /// is the point: a count only published at teardown cannot be told apart from
  /// "none dropped".
  std::atomic<u64> audioFramesDropped_{0};
  /// Audio frames that produced at least one packet *and* whose packets all
  /// reached the file, as opposed to audioFrameCount_ below, which counts every
  /// frame the encoder accepted -- including the ones a delaying encoder is
  /// still holding, which have written nothing. Read only on the encoding
  /// thread, like audioFrameCount_.
  u64 audioFramesEncoded_{0};
  /// Remaining resample failures the test seam will simulate. Zero in production
  /// and on every build that is not a test.
  std::atomic<u32> injectedResampleFailures_{0};
  /// Remaining writes the test seam will allow before refusing one, and whether
  /// it has been armed at all. Zero in production.
  std::atomic<u64> writesBeforeWriteFailure_{0};
  std::atomic<bool> writeFailureArmed_{false};
  /// Refused writes so far, for the throttled log. Distinct from
  /// writeFailureReported_, which is the one-shot *report* to the caller.
  u64 writeFailures_{0};

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

  /// Seed for `audioFrame_->pts`, so audio packets land on the audio clock rather
  /// than on a frame counter that would interleave them with the video. Advanced
  /// for every frame the *encoder accepted*, which is why it cannot answer
  /// AudioResampleReport::anyFrameWritten -- see audioFramesEncoded_.
  u64 audioFrameCount_{0};

  int fileLockFd_{-1};
  std::string currentOutputPath_;
};

} // namespace vc
