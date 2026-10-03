// Version: 2.1.0 - 2026-04-14 14:25:00 MDT

#include "VideoRecorderFFmpeg.hpp"
#include <libavcodec/defs.h>
#include <libavcodec/version.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include "core/Logger.hpp"
#include "lyrics/LyricsData.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include <fmt/core.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 60
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace vc {

namespace {

// Exclusive-create + advisory-lock primitives for claiming an output file.
// The POSIX branches preserve the historical open/flock behavior exactly;
// the Win32 branches use the CRT _open family plus LockFileEx over
// _get_osfhandle for an equivalent byte-range lock.

#ifdef _WIN32

int openExclusive(const char* path) {
    return _open(path,
                 _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
}

bool lockExclusive(int fd) {
    OVERLAPPED overlapped{};
    const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    return LockFileEx(handle,
                      LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                      0, MAXDWORD, MAXDWORD, &overlapped) != FALSE;
}

void unlockExclusive(int fd) {
    OVERLAPPED overlapped{};
    LockFileEx(reinterpret_cast<HANDLE>(_get_osfhandle(fd)),
               LOCKFILE_FAIL_IMMEDIATELY,
               0, MAXDWORD, MAXDWORD, &overlapped);
}

void closeExclusive(int fd) {
    _close(fd);
}

#else

int openExclusive(const char* path) {
    return ::open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
}

bool lockExclusive(int fd) {
    return flock(fd, LOCK_EX | LOCK_NB) == 0;
}

void unlockExclusive(int fd) {
    flock(fd, LOCK_UN);
}

void closeExclusive(int fd) {
    ::close(fd);
}

#endif

} // namespace

// ── Subtitle stream state ────────────────────────────────────────────────────
//
// Everything the karaoke track needs, and nothing the caller has to keep alive.
// Two lifetimes, and both are ours:
//
//   * the script -- once avcodec_open2 has run, the *only* owner of the header
//     bytes is the codec: ff_ass_encoder's ass_encode_init av_malloc's an
//     extradata and memcpy's `subtitle_header` into it. The copy in
//     initSubtitleStream's local is dead on the next line, so the caller's
//     buffer has to outlive that call and nothing else.
//   * `events`  -- the cue bodies, held for the whole recording because a cue
//     at 3:12 is written when the video clock reaches 3:12. Each is copied into
//     its packet by av_strlcpy inside avcodec_encode_subtitle, so the vector
//     owns them outright and hands out no reference to a caller.
//
// `stream` is an AVStream owned by formatCtx_, not by us; it is dereferenced
// only between init and cleanup.
//
// That is the buffer-lifetime argument for this design, and it is why no AVIO
// context appears anywhere below. An `avio_alloc_context` read callback over an
// in-memory buffer is the *demuxer* idiom: it lets a muxer re-read a source it
// has already been handed. Muxing a subtitle track needs no such thing -- the
// cues are discrete packets, and the only way a use-after-free would arise here
// is a caller's buffer that outlived init, which the copying removes.
struct VideoRecorderFFmpeg::SubtitleTrack {
  AVCodecContextPtr codecCtx;
  AVStream* stream{nullptr};
  std::vector<LyricsExport::AssEvent> events;
  /// Index of the next cue to write. Advances even when a cue cannot be
  /// written, so one unencodable line cannot spin the flush loop forever.
  std::size_t next{0};
};

namespace {

/// The subtitle stream's time base: milliseconds.
///
/// This is the container's own resolution -- Matroska's Timestamp is 1 ms, and
/// its muxer derives `time_scale` from `st->time_base` and warns for anything
/// that is neither 1/1000 nor 1/1000000000. It is also exact for ASS, whose
/// timestamps are centiseconds, so no rescale rounding can move a cue.
/// AV_TIME_BASE_Q is the encoder-side base ffmpeg's own CLI uses for subtitle
/// encoders, and is required: ff_encode_preinit rejects an unset encoder time
/// base outright.
constexpr AVRational kSubtitleTimeBase{1, 1000};

/// ms per ASS centisecond.
constexpr std::int64_t kMillisecondsPerCentisecond = 10;

/// ASS timestamps are centiseconds; the subtitle stream's time base is
/// milliseconds, so the conversion is a multiplication by ten.
///
/// It is a named function rather than a bare `*` at each of the two call sites
/// because the original bug here was exactly this and nothing caught it for two
/// rounds: `cs / kMillisecondsPerCentisecond` is a plausible-looking line, it
/// compiles, it produces plausible-looking numbers, and it is wrong by a factor
/// of 100. A two-second cue became a 20 ms cue, and a three-minute song's whole
/// lyric sheet collapsed into its first 1.8 seconds. Only a test asserting the
/// *exact* millisecond value could see it -- an assertion of the form "the
/// duration is non-zero" or "roughly 2 s" would have passed throughout.
constexpr std::int64_t toMilliseconds(const std::int64_t centiseconds) {
  return centiseconds * kMillisecondsPerCentisecond;
}

/// Should a failure that can repeat forever be logged at this occurrence count?
///
/// Throttled logarithmically rather than per-occurrence or on a fixed interval.
/// At 48 kHz stereo AAC a persistent resampler failure is ~47 frames a second,
/// so `LOG_WARN` per occurrence is ~2800 lines a minute -- which makes the log
/// useless in exactly the recording where the diagnosis matters. A fixed
/// "every 1000th" is better but still unbounded over a long take. Logging the
/// first occurrence and then each power of two costs log2(N) lines forever, so
/// the first line says *what* failed, the later ones say *it is still happening
/// and here is how much*, and a three-hour failure is about 24 lines.
///
/// Shared by the two failures that can repeat: dropped resample frames, and
/// refused muxer writes. `flush()` is the second one's worst case, because the
/// drain is a loop over every packet the encoders were still holding.
bool shouldLogOccurrence(const u64 occurrence) {
  if (occurrence <= 1) {
    return true;
  }
  return (occurrence & (occurrence - 1)) == 0; // a power of two
}

/// The `ass` encoder is `ff_ass_encoder`, gated on libavcodec's
/// CONFIG_ASS_ENCODER. `ssa` is the same codec under its other name -- both map
/// to AV_CODEC_ID_ASS with one shared body -- so the fallback is free.
///
/// A passthrough text encoder: ass_encode_frame is `av_strlcpy(buf,
/// rects[0]->ass, bufsize)`. It is *not* libass, and nothing in libavcodec
/// links libass -- `--enable-libass` on the ffmpeg CLI gates the `ass`/
/// `subtitles` *filters* (burn-in), not this. The dependency set does not
/// change.
const AVCodec* findAssEncoder() {
  if (const AVCodec* codec = avcodec_find_encoder_by_name("ass")) {
    return codec;
  }
  return avcodec_find_encoder_by_name("ssa");
}

} // namespace

VideoRecorderFFmpeg::VideoRecorderFFmpeg() = default;

VideoRecorderFFmpeg::~VideoRecorderFFmpeg() {
  cleanup();
}

Result<void> VideoRecorderFFmpeg::init(const EncoderSettings& settings,
                                       std::string_view assSubtitle) {
  int ret;

  std::filesystem::path originalPath(settings.outputPath);
  std::filesystem::path finalPath = originalPath;
  std::string base = originalPath.stem().string();
  std::string ext = originalPath.extension().string();
  std::filesystem::path dir = originalPath.parent_path();
  
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }

  int counter = 1;
  int fd = -1;

  while (true) {
    // Exclusive create prevents racing another recorder instance; the lock
    // then claims the file for the lifetime of this recording.
    fd = openExclusive(finalPath.c_str());
    if (fd >= 0) {
      // Successfully created a unique file, now lock it
      if (!lockExclusive(fd)) {
        closeExclusive(fd);
        return Result<void>::err(fmt::format("Failed to lock file {}: {}", finalPath.string(), strerror(errno)));
      }
      break;
    }
    
    if (errno != EEXIST) {
      return Result<void>::err(fmt::format("Failed to resolve file conflict for {}: {}", finalPath.string(), strerror(errno)));
    }
    
    // File exists, try next suffix
    finalPath = dir / fmt::format("{}_{:02d}{}", base, counter, ext);
    counter++;
    if (counter > 999) {
      return Result<void>::err("Failed to find a non-colliding filename after 999 attempts.");
    }
  }
  
  fileLockFd_ = fd;
  currentOutputPath_ = finalPath.string();

  AVFormatContext* ctx = nullptr;
  ret = avformat_alloc_output_context2(
    &ctx, nullptr, nullptr, currentOutputPath_.c_str());
  formatCtx_.reset(ctx);

  if (ret < 0 || !formatCtx_) {
    return Result<void>::err("Failed to create output context: " +
      ffmpegError(ret));
  }

  if (settings.video.isHardwareAccelerated()) {
    if (auto result = initHWDevice(settings); !result) {
      LOG_WARN("HW accel init failed, falling back to software: {}", result.error().message);
    }
  }

  if (auto result = initVideoStream(settings); !result) {
    return result;
  }

  if (auto result = initAudioStream(settings); !result) {
    return result;
  }

  // Before avio_open and avformat_write_header, because a stream only exists to
  // the muxer if it was declared before the header went out. Returns void on
  // purpose: see the declaration -- no subtitle outcome may fail a recording.
  initSubtitleStream(assSubtitle);

  if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(
      &formatCtx_->pb, currentOutputPath_.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
      return Result<void>::err("Failed to open output file: " +
        ffmpegError(ret));
    }
  }

  AVDictionary* opts = nullptr;
  ret = avformat_write_header(formatCtx_.get(), &opts);
  av_dict_free(&opts);

  if (ret < 0) {
    return Result<void>::err("Failed to write header: " + ffmpegError(ret));
  }
  // The gate cleanup() uses before writing any late subtitle packet. Set here and
  // nowhere else, so "the muxer will accept a packet" has exactly one answer.
  headerWritten_ = true;

  packet_.reset(av_packet_alloc());
  if (!packet_) {
    return Result<void>::err("Failed to allocate packet");
  }

  LOG_DEBUG("FFmpeg initialized successfully (HW accel: {}, file: {})",
    settings.video.isHardwareAccelerated() ? "yes" : "no",
    currentOutputPath_);
  return Result<void>::ok();
}

void VideoRecorderFFmpeg::cleanup() {
    std::lock_guard lock(mutex_);

    packet_.reset();
    videoFrame_.reset();
    audioFrame_.reset();
    swsCtx_.reset();
    swrCtx_.reset();
    videoCodecCtx_.reset();
    audioCodecCtx_.reset();

    // Captured before anything resets it, and used for both writes below. "The
    // muxer got a header" is the one fact that decides whether either is legal.
    const bool muxerIsWritable = headerWritten_ && formatCtx_ && formatCtx_->pb;

    // Any cue the video clock never reached. Written here rather than in flush()
    // because cleanup() is the only guaranteed-terminal path -- the destructor
    // calls it, and VideoRecorderThread::stop() reaches it even if the loop
    // exited on an error -- so this is where "the recording is over" can be
    // stated exactly once. Idempotent: subtitle_->next only moves forward.
    //
    // Gated on the same fact av_write_trailer is gated on: an init that failed at
    // avio_open leaves formatCtx_ with no pb, and an init that failed at
    // avformat_write_header leaves a pb but no header, and writing packets into
    // either produces a file that is not merely unfinished but structurally
    // invalid. subtitle_->codecCtx must outlive this, which is why the reset
    // below is not up with the other codec contexts.
    if (subtitle_ && muxerIsWritable) {
        u64 subtitleBytes = 0;
        writeSubtitlePacketsUpTo(std::numeric_limits<std::int64_t>::max(),
                                 subtitleBytes);
    }

    // Releases the subtitle codec context and the cue bodies. The AVStream is
    // not ours; it dies with formatCtx_ below, and nothing touches
    // subtitle_->stream between here and then.
    subtitle_.reset();
    headerWritten_ = false;

    // Checked. A refused trailer is the index of the file: without it a
    // Matroska file has no cues and cannot be seeked, and an MP4 has no moov
    // atom at all and will not open. Either way the caller must hear about it,
    // because the alternative is the exact failure this class is here to stop --
    // a file that looks like a recording and is not. It cannot fire in a healthy
    // recording: the same context accepted a header and every packet up to here,
    // and the only cause of a trailer failure is an I/O error on the final write.
    if (muxerIsWritable) {
        const int trailer = av_write_trailer(formatCtx_.get());
        if (trailer < 0) {
            writeFailed_ = true;
            LOG_ERROR("Failed to write the trailer; the file is incomplete: {}",
                      ffmpegError(trailer));
        }
    }
    formatCtx_.reset();

    // The encoder is reusable, so a failed instance must not leave the next
    // recording looking broken. VideoRecorderThread snapshots writeFailed() and
    // audioResampleReport() before calling cleanup() precisely so these resets
    // cannot swallow the reports.
    writeFailed_ = false;
    writeFailureReported_ = false;
    audioFramesDropped_.store(0, std::memory_order_release);
    injectedResampleFailures_.store(0, std::memory_order_release);
    writesBeforeWriteFailure_.store(0, std::memory_order_release);
    writeFailureArmed_.store(false, std::memory_order_release);
    writeFailures_ = 0;

    videoStream_ = nullptr;
    audioStream_ = nullptr;
    haveVideoOrigin_ = false;
    videoOriginUs_ = 0;
    havePreviousCaptureUs_ = false;
    previousCaptureUs_ = 0;
    lastVideoStepUs_ = 0;
    lastVideoPts_ = -1;
    warnedMissingCaptureTime_ = false;
    audioFrameCount_ = 0;
    audioFramesEncoded_ = 0;

    if (fileLockFd_ >= 0) {
        unlockExclusive(fileLockFd_);
        closeExclusive(fileLockFd_);
        fileLockFd_ = -1;
    }
}

bool VideoRecorderFFmpeg::encodeVideo(const GrabbedFrame& frame,
  u64& bytesWritten) {
  if (frame.data.empty())
    return false;

  std::lock_guard lock(mutex_);
  if (!videoCodecCtx_ || !videoFrame_)
    return false;

  // Once the muxer has refused a packet for a damaging reason, do not keep
  // feeding it: every further frame lands in a file nobody should trust, and
  // lengthening the plausible-looking segment is worse than stopping.
  if (writeFailed_)
    return false;

  const u8* srcData[1] = {frame.data.data()};
  int srcLinesize[1] = {static_cast<int>(frame.width * 4)};

  AVPixelFormat targetFmt = swPixelFormat_;

  if (!swsCtx_) {
    swsCtx_.reset(sws_getContext(frame.width,
      frame.height,
      AV_PIX_FMT_RGBA,
      videoCodecCtx_->width,
      videoCodecCtx_->height,
      targetFmt,
      SWS_BILINEAR,
      nullptr,
      nullptr,
      nullptr));
  }

  sws_scale(swsCtx_.get(),
    srcData,
    srcLinesize,
    0,
    frame.height,
    videoFrame_->data,
    videoFrame_->linesize);

  videoFrame_->pts = presentationTimestampFor(frame.timestamp);

  AVFrame* encodeFrame = videoFrame_.get();

  if (hwFramesCtx_ && hwFrame_) {
    int ret = av_hwframe_transfer_data(hwFrame_.get(), videoFrame_.get(), 0);
    if (ret < 0) {
      LOG_WARN("HW frame transfer failed: {}", ffmpegError(ret));
    } else {
      hwFrame_->pts = videoFrame_->pts;
      encodeFrame = hwFrame_.get();
    }
  }

  return encodeVideoFrame(encodeFrame, bytesWritten);
}

i64 VideoRecorderFFmpeg::presentationTimestampFor(i64 captureTimestampUs) {
    // GrabbedFrame::timestamp is microseconds; the codec time base is
    // {1, video.fps}, so this is the elapsed wall time expressed in frame
    // ticks. av_rescale_q is the library's own nearest-integer rescale, which
    // is what we want: truncating would bias every frame one tick early and
    // accumulate.
    static constexpr AVRational microsecondTimeBase{1, 1000000};

    // A non-positive capture time carries no elapsed-time information at all --
    // GrabbedFrame defaults the field to 0 and VideoRecorder::submitVideoFrame
    // forwards its caller's argument without validating it, so 0 really does
    // arrive (the pre-existing frameSubmissionsReachEncoder test sends 0 for
    // its first frame). It is not a plausible steady_clock reading: a monotonic
    // clock since boot is never exactly 0 microseconds in a running process.
    if (captureTimestampUs <= 0) {
        if (!warnedMissingCaptureTime_) {
            warnedMissingCaptureTime_ = true;
            LOG_WARN("Encoder received a frame with no capture timestamp; "
                     "stepping the timeline by the last observed frame "
                     "interval until a timestamped frame arrives");
        }
        // Deliberately not a frame counter. It advances by the most recently
        // observed real capture interval, and the very next timestamped frame
        // is re-anchored absolutely against videoOriginUs_ rather than
        // incrementally, so an extrapolation error can never accumulate. With
        // no interval observed yet there is nothing to extrapolate from, so
        // this collapses to one tick -- the only available answer when the
        // producer supplied no timing information whatsoever.
        const i64 stepTicks =
            av_rescale_q(lastVideoStepUs_, microsecondTimeBase,
                         videoCodecCtx_->time_base);
        lastVideoPts_ += std::max<i64>(stepTicks, 1);
        return lastVideoPts_;
    }

    if (!haveVideoOrigin_) {
        // Normalise: the first real capture time becomes PTS 0 rather than an
        // arbitrary offset. A microsecond-since-boot value as a start time
        // lands in the container's edit list / first cluster timestamp, which
        // inflates the reported duration by the offset and makes seeking
        // behave as if the file started later than it does. Offset 0 keeps the
        // header small, keeps the fragment/AVIO story irrelevant (the muxer
        // writes a plain interleaved file), and makes the file's duration equal
        // the wall time it actually spans.
        videoOriginUs_ = captureTimestampUs;
        haveVideoOrigin_ = true;
        lastVideoPts_ = -1;
    }

    if (havePreviousCaptureUs_) {
        const i64 stepUs = captureTimestampUs - previousCaptureUs_;
        // A backwards clock tick must not poison the learned interval that the
        // un-timestamped fallback above relies on.
        if (stepUs > 0) {
            lastVideoStepUs_ = stepUs;
        }
    }
    previousCaptureUs_ = captureTimestampUs;
    havePreviousCaptureUs_ = true;

    const i64 elapsedUs = captureTimestampUs - videoOriginUs_;
    const i64 ticks = av_rescale_q(elapsedUs, microsecondTimeBase,
                                   videoCodecCtx_->time_base);

    // Strictly increasing, always. Two capture instants closer together than
    // one nominal frame interval rescale to the same tick, and a clock that
    // ticked backwards (or a producer whose origin moved) rescales to an
    // earlier one; libavcodec rejects a non-increasing pts, and the muxer
    // would emit a stream it cannot seek. The floor is applied to the
    // *rescaled* value, so a capture rate above the nominal record rate is
    // clamped to the nominal rate instead of corrupting the timeline. That
    // case is the fps mismatch VisualizerRenderer::startRecording already
    // warns about, and it is unchanged from the old counter's behaviour.
    lastVideoPts_ = std::max(ticks, lastVideoPts_ + 1);
    return lastVideoPts_;
}

VideoRecorderFFmpeg::AudioEncodeOutcome VideoRecorderFFmpeg::encodeAudio(
    std::vector<f32>& buffer, const u32 channels, u64& bytesWritten) {
    std::lock_guard lock(mutex_);
    // swrCtx_ is in the list because initAudioStream can now legitimately return
    // without one -- it returns ok() with no audio stream at all when the codec
    // is missing or needs a variable frame size -- and the old code called
    // swr_convert on swrCtx_.get() with only audioCodecCtx_ and audioFrame_
    // checked.
    //
    // Every one of these is NoProgress, not Failed: nothing about the encoder is
    // broken, there was simply nothing for it to do. See AudioEncodeOutcome for
    // why that distinction is worth a return value.
    if (!audioCodecCtx_ || !audioFrame_ || !swrCtx_ || buffer.empty())
        return AudioEncodeOutcome::NoProgress;

    // Same short-circuit encodeVideo has, for the same reason: once the muxer
    // has refused a packet for a damaging reason the container is untrustworthy,
    // and encoding more audio into it only lengthens a broken file. This is *not*
    // applied to a resampler failure, which is a track problem and does not stop
    // the recording -- see the note on resampleIntoAudioFrame.
    if (writeFailed_)
        return AudioEncodeOutcome::NoProgress;

    int frameSize = audioCodecCtx_->frame_size;
    if (frameSize <= 0)
        return AudioEncodeOutcome::NoProgress;

    bool encodedAny = false;
    // The encoder refusing a frame, as opposed to a packet not reaching the
    // file. encodeAudioFrame reports both with one bool, so the muxer case is
    // excluded by its own sticky flag: that fault has a one-shot report
    // (reportWriteFailure) and must not also arrive here as a generic "the
    // encoder failed", which is how a full volume used to be reported twice.
    bool encoderRefused = false;

    while (buffer.size() >= static_cast<usize>(frameSize * channels)) {
        std::vector<f32> samples(buffer.begin(),
                                 buffer.begin() + frameSize * channels);
        buffer.erase(buffer.begin(), buffer.begin() + frameSize * channels);

        const u8* srcData[1] = {reinterpret_cast<const u8*>(samples.data())};

        int ret = resampleIntoAudioFrame(audioFrame_.get(), srcData, frameSize);
        if (ret < 0) {
            // The frame is left out. That is not corruption -- the container, the
            // index and the audio stream all stay valid, and the result decodes
            // with a gap -- which is precisely why it used to be invisible. So it
            // is counted, and the count is what stop() reports.
            const u64 dropped = audioFramesDropped_.fetch_add(
                                    1, std::memory_order_acq_rel) +
                                1;
            if (shouldLogOccurrence(dropped)) {
                LOG_WARN("Audio resample error ({} dropped so far): {}",
                         dropped, ffmpegError(ret));
            }
            continue;
        }

        audioFrame_->pts = audioFrameCount_;
        audioFrameCount_ += frameSize;

        if (encodeAudioFrame(audioFrame_.get(), bytesWritten)) {
            encodedAny = true;
            ++audioFramesEncoded_;
        } else if (!writeFailed_) {
            encoderRefused = true;
        }
    }

    if (encoderRefused)
        return AudioEncodeOutcome::Failed;
    return encodedAny ? AudioEncodeOutcome::Encoded
                      : AudioEncodeOutcome::NoProgress;
}

void VideoRecorderFFmpeg::flush(u64& bytesWritten) {
    std::lock_guard lock(mutex_);

    // Nothing may be written to a muxer that never had a header. VideoRecorder's
    // destructor reaches flush() through VideoRecorderThread::stop() even when
    // init() failed at avio_open or avformat_write_header, and the encoders are
    // open by then, so draining them would hand packets to a context with no pb
    // at all or with a pb and no header. Same gate cleanup() uses.
    if (!headerWritten_ || !formatCtx_) {
        return;
    }

    // Nor to one that has already refused a packet. flush() is the only write
    // path encodeVideo and encodeAudio do not short-circuit on writeFailed_, and
    // the two are the same decision: those two refuse to lengthen a file nobody
    // should trust, and the drain was lengthening it with every packet the
    // encoders were still holding -- a full volume turns that into a burst of
    // writes, each one a LOG_ERROR, into a container already damaged. It also
    // cost the log: writeFailureReported_ guards only the Backpressure line, so
    // the drain was the loudest thing in an otherwise quiet failure.
    //
    // av_write_trailer in cleanup() deliberately still runs. That is not part of
    // this: a trailer is what makes a truncated file openable at all -- without
    // it an MP4 has no moov atom and a Matroska file has no cues -- so refusing
    // it would convert "plays to the break" into "does not open", and stop() has
    // already told the user the file is incomplete by then.
    if (writeFailed_) {
        return;
    }

    if (videoCodecCtx_) {
        avcodec_send_frame(videoCodecCtx_.get(), nullptr);
        while (true) {
            int ret =
                    avcodec_receive_packet(videoCodecCtx_.get(), packet_.get());
            if (ret < 0)
                break;
            writePacket(packet_.get(), videoStream_, videoCodecCtx_->time_base,
                        bytesWritten);
        }
    }

    if (audioCodecCtx_) {
        avcodec_send_frame(audioCodecCtx_.get(), nullptr);
        while (true) {
            int ret =
                    avcodec_receive_packet(audioCodecCtx_.get(), packet_.get());
            if (ret < 0)
                break;
            writePacket(packet_.get(), audioStream_, audioCodecCtx_->time_base,
                        bytesWritten);
        }
    }
}

Result<void> VideoRecorderFFmpeg::initVideoStream(
  const EncoderSettings& settings) {
  if (settings.video.fps == 0) {
    // {1, 0} is not a time base: every rescale against it divides by zero,
    // both the per-frame capture-time conversion and the packet rescale in
    // writePacket. ConfigParsers clamps the configured value into range, so
    // this only guards a directly-constructed EncoderSettings -- but it is a
    // hard arithmetic failure, not a degraded result, so it fails closed.
    return Result<void>::err("Invalid video frame rate: 0");
  }

  const AVCodec* codec =
    avcodec_find_encoder_by_name(settings.video.codecName().c_str());
  if (!codec) {
    return Result<void>::err("Video codec not found: " +
      settings.video.codecName());
  }

  videoStream_ = avformat_new_stream(formatCtx_.get(), nullptr);
  if (!videoStream_)
    return Result<void>::err("Failed to create video stream");

  videoCodecCtx_.reset(avcodec_alloc_context3(codec));
  if (!videoCodecCtx_)
    return Result<void>::err("Failed to allocate video codec context");

  videoCodecCtx_->width = settings.video.width;
  videoCodecCtx_->height = settings.video.height;
  videoCodecCtx_->time_base =
    AVRational{1, static_cast<int>(settings.video.fps)};
  videoCodecCtx_->framerate =
    AVRational{static_cast<int>(settings.video.fps), 1};
  videoCodecCtx_->gop_size = settings.video.gopSize > 0
    ? settings.video.gopSize
    : settings.video.fps * 2;
  videoCodecCtx_->max_b_frames = settings.video.bFrames;

  if (settings.video.isHardwareAccelerated() && hwDeviceCtx_) {
    auto result = initHWFrames(settings);
    if (!result) {
      LOG_WARN("HW frames init failed: {}", result.error().message);
    }
    videoCodecCtx_->pix_fmt = getHWPixelFormat(settings);
  } else {
    videoCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
  }

  if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
    videoCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  AVDictionary* opts = nullptr;
  if (settings.video.isHardwareAccelerated()) {
    if (settings.video.bitrate > 0) {
      videoCodecCtx_->bit_rate = settings.video.bitrate * 1000;
      videoCodecCtx_->rc_max_rate = videoCodecCtx_->bit_rate;
      videoCodecCtx_->rc_buffer_size = videoCodecCtx_->bit_rate;
    }
    av_dict_set(&opts, "preset", "p4", 0);
    av_dict_set(&opts, "rc", "vbr", 0);
  } else if (settings.video.codec == VideoCodec::H264 ||
       settings.video.codec == VideoCodec::H265) {
    av_dict_set(&opts, "preset", settings.video.presetName().c_str(), 0);
    av_dict_set(
      &opts, "crf", std::to_string(settings.video.crf).c_str(), 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
  }

  int ret = avcodec_open2(videoCodecCtx_.get(), codec, &opts);
  av_dict_free(&opts);

  if (ret < 0) {
    return Result<void>::err("Failed to open video codec: " +
      ffmpegError(ret));
  }

  avcodec_parameters_from_context(videoStream_->codecpar,
    videoCodecCtx_.get());
  videoStream_->time_base = videoCodecCtx_->time_base;

  videoFrame_.reset(av_frame_alloc());
  if (!videoFrame_)
    return Result<void>::err("Failed to allocate video frame");
  videoFrame_->format = AV_PIX_FMT_YUV420P;
  videoFrame_->width = videoCodecCtx_->width;
  videoFrame_->height = videoCodecCtx_->height;
  // Checked. av_frame_get_buffer is the only thing that allocates videoFrame_'s
  // plane pointers, and sws_scale writes through data[0] unconditionally on the
  // next call -- an unchecked failure here is a null write, not a soft error.
  // The condition it catches is a 32-bit malloc failing on a
  // width*height*1.5-byte allocation (a 4K frame is ~12 MB, and a machine under
  // memory pressure can refuse it). It cannot fire in a healthy recording: the
  // same allocation already succeeded for the codec context and the muxer, and
  // the frame is only this size because settings.video validated non-zero.
  ret = av_frame_get_buffer(videoFrame_.get(), 0);
  if (ret < 0) {
    return Result<void>::err("Failed to allocate video frame buffer: " +
      ffmpegError(ret));
  }

  if (hwFramesCtx_) {
    hwFrame_.reset(av_frame_alloc());
    hwFrame_->format = videoCodecCtx_->pix_fmt;
    hwFrame_->width = videoCodecCtx_->width;
    hwFrame_->height = videoCodecCtx_->height;
    ret = av_hwframe_get_buffer(hwFramesCtx_.get(), hwFrame_.get(), 0);
    if (ret < 0) {
      LOG_WARN("Failed to allocate HW frame: {}", ffmpegError(ret));
    }
  }

  return Result<void>::ok();
}

Result<void> VideoRecorderFFmpeg::initAudioStream(
        const EncoderSettings& settings) {
    const AVCodec* codec =
            avcodec_find_encoder_by_name(settings.audio.codecName().c_str());
    if (!codec) {
        LOG_WARN("Audio codec not found, skipping audio");
        return Result<void>::ok();
    }

    // The codec context is opened before the stream is declared, and the stream
    // is only created once this codec has proved it can be driven. That ordering
    // is not cosmetic: avformat_new_stream cannot be undone -- the AVStream stays
    // in formatCtx_ -- so declaring it first and then deciding the codec is
    // unusable leaves avformat_write_header looking at a stream with no codecpar.
    // Same discipline initSubtitleStream uses, for the same reason.
    audioCodecCtx_.reset(avcodec_alloc_context3(codec));
    if (!audioCodecCtx_)
        return Result<void>::err("Failed to allocate audio codec context");

    audioCodecCtx_->sample_rate = settings.audio.sampleRate;
    audioCodecCtx_->bit_rate = settings.audio.bitrate * 1000;

    AVChannelLayout layout;
    av_channel_layout_default(&layout, settings.audio.channels);
    av_channel_layout_copy(&audioCodecCtx_->ch_layout, &layout);

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    // FFmpeg >= 7.1: AVCodec::sample_fmts was removed; query supported
    // formats through avcodec_get_supported_config() instead.
    const AVSampleFormat* supportedFmts = nullptr;
    int numSupportedFmts = 0;
    avcodec_get_supported_config(nullptr, codec,
                                 AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                 reinterpret_cast<const void**>(&supportedFmts),
                                 &numSupportedFmts);
    audioCodecCtx_->sample_fmt =
            (supportedFmts && numSupportedFmts > 0)
                    ? supportedFmts[0]
                    : AV_SAMPLE_FMT_FLTP;
#else
    audioCodecCtx_->sample_fmt =
            codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#endif
    audioCodecCtx_->time_base =
            AVRational{1, static_cast<int>(settings.audio.sampleRate)};

    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        audioCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    const int opened = avcodec_open2(audioCodecCtx_.get(), codec, nullptr);
    if (opened < 0) {
        return Result<void>::err("Failed to open audio codec: " +
                                 ffmpegError(opened));
    }

    // A variable-frame-size codec reports frame_size <= 0. The old code set
    // nb_samples from it, skipped the buffer allocation because nb_samples was
    // 0, and then let swr_convert write through audioFrame_'s null data pointers;
    // encodeAudio's own `frameSize <= 0` guard reads a different local and did
    // not stop it. Decided here, before any stream exists, so the answer is
    // "record without audio" rather than "declare an audio stream and then never
    // fill it".
    //
    // It cannot fire for any AudioCodec this project offers: AAC, Opus, FLAC, MP3
    // and PCM all report a fixed frame_size once open. It is not in the
    // project's output set, so this is a guard against a future codec rather
    // than a condition a healthy recording hits -- which is why it skips the
    // stream (the same choice the missing-codec branch above makes) instead of
    // failing the recording.
    if (audioCodecCtx_->frame_size <= 0) {
        LOG_WARN("Audio codec {} needs {} samples per frame; recording without audio",
                 settings.audio.codecName(), audioCodecCtx_->frame_size);
        return Result<void>::ok();
    }

    audioStream_ = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!audioStream_)
        return Result<void>::err("Failed to create audio stream");

    avcodec_parameters_from_context(audioStream_->codecpar,
                                    audioCodecCtx_.get());
    audioStream_->time_base = audioCodecCtx_->time_base;

    audioFrame_.reset(av_frame_alloc());
    if (!audioFrame_) {
        return Result<void>::err("Failed to allocate audio frame");
    }
    audioFrame_->format = audioCodecCtx_->sample_fmt;
    av_channel_layout_copy(&audioFrame_->ch_layout, &audioCodecCtx_->ch_layout);
    audioFrame_->sample_rate = audioCodecCtx_->sample_rate;
    audioFrame_->nb_samples = audioCodecCtx_->frame_size;

    // Non-zero by now: the frame_size guard above ran before this stream existed.
    // Checked for the same reason as the video frame: swr_convert writes into
    // audioFrame_->data. frame_size * channels * bytes-per-sample is small --
    // 1024 * 2 * 4 for AAC at 48 kHz -- so this only catches genuine
    // out-of-memory, and the allocation it guards is the only one for these
    // pointers.
    const int buffered = av_frame_get_buffer(audioFrame_.get(), 0);
    if (buffered < 0) {
        return Result<void>::err("Failed to allocate audio frame buffer: " +
                                  ffmpegError(buffered));
    }

    SwrContext* s = nullptr;
    const int allocated = swr_alloc_set_opts2(&s,
                        &audioCodecCtx_->ch_layout,
                        audioCodecCtx_->sample_fmt,
                        audioCodecCtx_->sample_rate,
                        &layout,
                        AV_SAMPLE_FMT_FLT,
                        settings.audio.sampleRate,
                        0,
                        nullptr);
    // Checked. Both of these were discarded while swrCtx_.get() was
    // dereferenced by the very next call, so a resampler that could not be
    // configured -- an unsupported sample-format conversion, or an out-of-memory
    // -- became a null dereference inside swr_convert rather than an error.
    // Neither can fire for the project's settings: the layouts are the defaults
    // for the configured channel count, both rates are the same value, and the
    // only conversion is float to whatever the opened encoder advertises.
    if (allocated < 0 || !s) {
        return Result<void>::err("Failed to allocate the audio resampler: " +
                                  ffmpegError(allocated));
    }
    const int inited = swr_init(s);
    if (inited < 0) {
        swr_free(&s);
        return Result<void>::err("Failed to initialise the audio resampler: " +
                                  ffmpegError(inited));
    }
    swrCtx_.reset(s);

    return Result<void>::ok();
}

bool VideoRecorderFFmpeg::encodeVideoFrame(AVFrame* frame, u64& bytesWritten) {
    int ret = avcodec_send_frame(videoCodecCtx_.get(), frame);
    if (ret < 0)
        return false;

    // "Every packet this frame produced reached the file". The old code
    // discarded writePacket's answer and returned true, and VideoRecorderThread
    // incremented framesWritten on that true -- so a muxer that had been refusing
    // packets for a minute still reported a climbing count.
    bool allWritten = true;

    while (ret >= 0) {
        ret = avcodec_receive_packet(videoCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        if (!writePacket(packet_.get(), videoStream_, videoCodecCtx_->time_base,
                         bytesWritten)) {
            allWritten = false;
        }
    }
    return allWritten;
}

int VideoRecorderFFmpeg::resampleIntoAudioFrame(AVFrame* frame,
                                                 const u8* const* srcData,
                                                 const int frameSize) {
  // The seam injects the *error code*, not a branch, so everything the caller
  // does with a failure is the production path. Zero in production, and
  // fetch_sub on zero would wrap a u32 to 4 billion, hence the guard.
  if (injectedResampleFailures_.load(std::memory_order_acquire) > 0) {
    injectedResampleFailures_.fetch_sub(1, std::memory_order_acq_rel);
    return AVERROR(EINVAL);
  }
  return swr_convert(swrCtx_.get(), frame->data, frameSize, srcData, frameSize);
}

bool VideoRecorderFFmpeg::consumeInjectedWriteFailure() {
  // Armed is a separate flag rather than a sentinel in the countdown, because
  // "0 writes remaining" is both the immediate case and the exhausted one.
  if (!writeFailureArmed_.load(std::memory_order_acquire)) {
    return false;
  }
  auto remaining = writesBeforeWriteFailure_.load(std::memory_order_acquire);
  while (remaining > 0) {
    if (writesBeforeWriteFailure_.compare_exchange_weak(
            remaining, remaining - 1, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return false; // this write is allowed through
    }
  }
  // The countdown is spent: refuse this write and disarm. See the note on
  // simulateWriteFailureForTesting -- production's own persistence is
  // writeFailed_, and a seam that refused forever would make a gated write path
  // indistinguishable from one that never ran, because a refused packet
  // contributes no bytes. Only the encoding thread writes, so there is no race
  // here worth an atomic exchange.
  writeFailureArmed_.store(false, std::memory_order_release);
  return true;
}

bool VideoRecorderFFmpeg::encodeAudioFrame(AVFrame* frame, u64& bytesWritten) {
    int ret = avcodec_send_frame(audioCodecCtx_.get(), frame);
    if (ret < 0)
        return false;

    // Same contract as encodeVideoFrame: true only if every packet this frame
    // produced is in the file.
    bool allWritten = true;

    while (ret >= 0) {
        ret = avcodec_receive_packet(audioCodecCtx_.get(), packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        if (!writePacket(packet_.get(), audioStream_, audioCodecCtx_->time_base,
                         bytesWritten)) {
            allWritten = false;
        }
    }
    return allWritten;
}

bool VideoRecorderFFmpeg::writePacket(AVPacket* packet,
  AVStream* stream,
  AVRational sourceTimeBase,
  u64& bytesWritten) {
  if (stream == videoStream_ && subtitle_) {
    // Interleave on the video clock: emit every cue that begins at or before
    // this frame. Writing the whole lyric sheet up front would be simpler and
    // wrong -- av_interleaved_write_frame would have to hold every one of those
    // packets in its interleave buffer until the video caught up, and past its
    // 10000-packet default it returns EAGAIN, which is a silently dropped cue.
    // Doing it here keeps the buffer at a couple of packets, and it is the same
    // order a player sees either way: the cue's own pts is in the packet.
    const std::int64_t frameMilliseconds =
        av_rescale_q(packet->pts, sourceTimeBase, kSubtitleTimeBase);
    writeSubtitlePacketsUpTo(frameMilliseconds, bytesWritten);
  }

  av_packet_rescale_ts(packet, sourceTimeBase, stream->time_base);
  packet->stream_index = stream->index;

  // The seam substitutes the return value, not a branch above it, so the
  // classification below is the production path. AVERROR(ENOSPC) because that is
  // the real cause the message names, and because a code the classifier maps to
  // Failed is what the surrounding switch exists for.
  const int written =
      consumeInjectedWriteFailure()
          ? AVERROR(ENOSPC)
          : av_interleaved_write_frame(formatCtx_.get(), packet);

  // The return value used to be discarded at every call site, which is how a
  // disk that filled at 90% produced a truncated file that opens, plays to the
  // break, and is reported Completed.
  switch (classifyWriteResult(written)) {
    case WriteOutcome::Written:
      bytesWritten += packet->size;
      return true;

    case WriteOutcome::Backpressure:
      // Not a failure, and deliberately not counted as one. The packet is not in
      // the file, so the frame must not be counted -- but nothing is damaged, and
      // calling this a failure would mark a merely-slow recording as broken. One
      // log line, not one per frame.
      if (!writeFailureReported_) {
        writeFailureReported_ = true;
        LOG_WARN("Muxer asked for backpressure; a packet was deferred");
      }
      return false;

    case WriteOutcome::Failed:
      writeFailed_ = true;
      // Throttled by occurrence rather than latched, for the same reason the
      // resampler log is: flush() drains every packet the encoders were still
      // holding, and a full volume turns that loop into one LOG_ERROR per packet.
      // Deliberately *not* writeFailureReported_, which is the one-shot *report*
      // to the encoding loop -- latching it here would silence the user's error.
      if (shouldLogOccurrence(++writeFailures_)) {
        LOG_ERROR("Muxer refused a packet for stream {} ({} so far): {}",
                  stream->index, writeFailures_, ffmpegError(written));
      }
      return false;
  }

  return false;
}

bool VideoRecorderFFmpeg::reportWriteFailure() {
  if (!writeFailed_ || writeFailureReported_) {
    return false;
  }
  writeFailureReported_ = true;
  return true;
}

void VideoRecorderFFmpeg::writeSubtitlePacketsUpTo(std::int64_t millisecond,
                                                   u64& bytesWritten) {
  if (!subtitle_) {
    return;
  }
  while (subtitle_->next < subtitle_->events.size()) {
    const auto& event = subtitle_->events[subtitle_->next];
    // Same centiseconds-to-milliseconds conversion as writeSubtitlePacket, and
    // it has to be the same one: when this divided while that one divided the
    // two agreed with each other and disagreed with the file, so every cue was
    // flushed far too early and then written with a timestamp the video clock
    // had already passed.
    if (toMilliseconds(event.startCentiseconds) > millisecond) {
      break;
    }
    writeSubtitlePacket(bytesWritten);
  }
}

bool VideoRecorderFFmpeg::writeSubtitlePacket(u64& bytesWritten) {
  if (!subtitle_ || subtitle_->next >= subtitle_->events.size()) {
    return false;
  }
  // Advance first. A cue that cannot be written is still consumed: leaving it in
  // place would make writeSubtitlePacketsUpTo retry it for every subsequent
  // frame, forever.
  const auto& event = subtitle_->events[subtitle_->next++];

  AVPacketPtr packet(av_packet_alloc());
  if (!packet) {
    LOG_WARN("Karaoke track: could not allocate a subtitle packet; the cue is lost");
    return false;
  }

  // One byte over the payload. ass_encode_frame is
  // `av_strlcpy(buf, rects[0]->ass, bufsize)` and rejects `len >= bufsize`, so
  // an exactly-sized buffer would come back AVERROR_BUFFER_TOO_SMALL. The spare
  // byte is also what makes the packet payload a C string, which the matching
  // decoder relies on -- ass_decode_frame copies it with av_strdup, and a
  // payload without a terminator would be read past its end.
  const auto capacity = static_cast<int>(event.body.size()) + 1;
  if (av_new_packet(packet.get(), capacity) < 0) {
    LOG_WARN("Karaoke track: could not allocate {} bytes for a cue", capacity);
    return false;
  }

  std::string text = event.body;
  text.push_back('\0');
  AVSubtitleRect rect{};
  rect.type = SUBTITLE_ASS;
  rect.ass = text.data();
  AVSubtitleRect* rects[1] = {&rect};

  const std::int64_t startMilliseconds =
      toMilliseconds(event.startCentiseconds);
  // A cue is never zero-length: toAssDocument floors the line at one
  // centisecond, and a zero-duration packet is one that compute_pkt_fields()
  // would go on to guess a duration for.
  const std::int64_t durationMilliseconds = std::max<std::int64_t>(
      1, toMilliseconds(event.endCentiseconds - event.startCentiseconds));

  AVSubtitle subtitle{};
  subtitle.pts = 0; // Unused by the ASS encoder; set rather than left undefined.
  // Not optional: avcodec_encode_subtitle refuses anything else with a bare
  // `return -1` before it even reaches the codec.
  subtitle.start_display_time = 0;
  subtitle.end_display_time = static_cast<std::uint32_t>(
      std::min<std::int64_t>(durationMilliseconds,
                             std::numeric_limits<std::uint32_t>::max()));
  subtitle.num_rects = 1;
  subtitle.rects = rects;

  const int written = avcodec_encode_subtitle(subtitle_->codecCtx.get(),
                                              packet->data, packet->size,
                                              &subtitle);
  if (written < 0) {
    LOG_WARN("Karaoke track: the ass encoder refused a cue: {}",
             ffmpegError(written));
    return false;
  }
  av_shrink_packet(packet.get(), written);

  packet->stream_index = subtitle_->stream->index;
  packet->pts = startMilliseconds;
  packet->dts = startMilliseconds;
  packet->duration = durationMilliseconds;
  packet->time_base = kSubtitleTimeBase;

  // Source and stream base are the same, so the rescale inside writePacket is
  // the identity. It is still the path taken, so a subtitle packet can never
  // pick up a base belonging to another stream by accident.
  return writePacket(packet.get(), subtitle_->stream, kSubtitleTimeBase,
                     bytesWritten);
}

void VideoRecorderFFmpeg::initSubtitleStream(std::string_view assDocument) {
  if (assDocument.empty()) {
    return; // No lyrics is the common case, and it is not a failure.
  }

  // 1. Can this container carry the track at all?
  //
  // avformat_query_codec consults the muxer's own codec-tag table (for Matroska,
  // `ff_mkv_codec_tags`, which has S_TEXT/ASS and S_ASS mapping to
  // AV_CODEC_ID_ASS) or the muxer's query_codec hook. Anything not greater than
  // zero means no, including the AVERROR_PATCHWELCOME it returns for a container
  // with no tag table -- so this fails closed on anything unrecognised rather
  // than optimistically adding a stream.
  //
  // This is not a defensive check; without it an MP4 recording *fails*.
  // libavformat's init_muxer looks the tag up, finds none, and returns EINVAL
  // from avformat_write_header ("Could not find tag for codec ass in stream #2,
  // codec not currently supported in container" -- measured against this
  // project's own FFmpeg 9.0.1). Of the five containers EncoderSettings offers,
  // only Matroska carries ASS: mov/mp4 have mov_text, which is 3GPP timed text
  // with no override tags at all, so a `\kf` karaoke cue has nowhere to live;
  // WebM's table is WebVTT only, and AVI has no subtitle support.
  if (avformat_query_codec(formatCtx_->oformat, AV_CODEC_ID_ASS,
                           FF_COMPLIANCE_NORMAL) <= 0) {
    LOG_INFO("Karaoke track: {} cannot carry an ASS subtitle stream; recording "
             "without one", formatCtx_->oformat->name);
    return;
  }

  // 2. Does this libavcodec have the encoder? A build configured
  //    --disable-encoder=ass has none, and AV_CODEC_ID_ASS would still be a
  //    legal stream for a container that supports it.
  const AVCodec* codec = findAssEncoder();
  if (!codec) {
    LOG_INFO("Karaoke track: this libavcodec has no ASS encoder; recording "
             "without one");
    return;
  }

  // 3. Split the document. Doing this before touching formatCtx_ is deliberate:
  //    every failure from here on must leave the output context exactly as it
  //    was, or the recording dies for want of a subtitle.
  auto stream = LyricsExport::splitAssStream(std::string(assDocument));
  if (stream.header.empty() || stream.events.empty()) {
    LOG_WARN("Karaoke track: the document has no script header or no cues "
             "({} header bytes, {} cues); recording without one",
             stream.header.size(), stream.events.size());
    return;
  }

  // 4. Open the codec *before* declaring the stream, so an avcodec_open2
  //    failure cannot leave a registered AVStream with an unusable codecpar --
  //    which is precisely what would make avformat_write_header fail.
  auto track = std::make_unique<SubtitleTrack>();
  std::string script = std::move(stream.header);
  track->events = std::move(stream.events);

  track->codecCtx.reset(avcodec_alloc_context3(codec));
  if (!track->codecCtx) {
    LOG_WARN("Karaoke track: could not allocate the subtitle codec context");
    return;
  }
  track->codecCtx->codec_type = AVMEDIA_TYPE_SUBTITLE;
  track->codecCtx->codec_id = AV_CODEC_ID_ASS;
  track->codecCtx->time_base = AV_TIME_BASE_Q;
  // The script, handed to the codec which copies it into its own extradata
  // during avcodec_open2. That copy is why the caller's document only has to
  // outlive this function and not the recording.
  // int, not size_t: AVCodecContext::subtitle_header_size is declared `int`
  // (avcodec.h). The cast is the narrowing made explicit rather than left to an
  // implicit conversion; a script over 2 GiB would need a different field type
  // upstream, not a silent wrap here.
  track->codecCtx->subtitle_header_size = static_cast<int>(script.size());
  // The one raw allocation this file makes that has no local owner, and the
  // static_cast is the price of it: FFmpeg's headers are C, where `void *`
  // converts to `uint8_t *` implicitly and C++ does not. Ownership is not
  // ambiguous -- avcodec_free_context calls av_freep(&avctx->subtitle_header), so
  // AVCodecContextDeleter releases it whether or not avcodec_open2 ever ran.
  // This is also the pattern ffmpeg's own enc_open uses.
  track->codecCtx->subtitle_header = static_cast<u8*>(av_mallocz(script.size() + 1));
  if (!track->codecCtx->subtitle_header) {
    LOG_WARN("Karaoke track: could not allocate the script header");
    return;
  }
  memcpy(track->codecCtx->subtitle_header, script.data(), script.size());

  const int opened = avcodec_open2(track->codecCtx.get(), codec, nullptr);
  if (opened < 0) {
    LOG_WARN("Karaoke track: could not open the ASS encoder: {}",
             ffmpegError(opened));
    return;
  }

  track->stream = avformat_new_stream(formatCtx_.get(), nullptr);
  if (!track->stream) {
    LOG_WARN("Karaoke track: the muxer refused a third stream");
    return;
  }
  avcodec_parameters_from_context(track->stream->codecpar,
                                 track->codecCtx.get());
  track->stream->time_base = kSubtitleTimeBase;
  // Karaoke is the point of the track, so it has to be on without the user
  // hunting for it in a track menu. Without this it would be *off*: Matroska
  // infers FlagDefault from the codec type, and mkv_default_mode returns "not
  // default" for everything that is not audio unless the muxer is told to
  // infer it, so a subtitle with no disposition is a subtitle nobody sees.
  track->stream->disposition |= AV_DISPOSITION_DEFAULT;
  // No language tag, deliberately: the video and audio streams above set none
  // either, and a single-tagged track out of three produces a track picker
  // listing one language beside two blanks. Matching the existing streams is
  // the consistent answer, and a language is a product decision rather than
  // something to invent here.

  subtitle_ = std::move(track);
  LOG_INFO("Karaoke track: {} cues muxed as a {} subtitle stream into {}",
           subtitle_->events.size(), formatCtx_->oformat->name,
           currentOutputPath_);
}

AVPixelFormat VideoRecorderFFmpeg::getHWPixelFormat(const EncoderSettings& settings) const {
  switch (settings.video.hwAccel) {
    case HWAccelDevice::NVENC:
      return AV_PIX_FMT_CUDA;
    case HWAccelDevice::VAAPI:
      return AV_PIX_FMT_VAAPI;
    case HWAccelDevice::QuickSync:
      return AV_PIX_FMT_QSV;
    case HWAccelDevice::AMF:
      return AV_PIX_FMT_D3D11;
    default:
      return AV_PIX_FMT_YUV420P;
  }
}

Result<void> VideoRecorderFFmpeg::initHWDevice(const EncoderSettings& settings) {
  AVHWDeviceType devType = AV_HWDEVICE_TYPE_NONE;
  const char* devName = nullptr;

  switch (settings.video.hwAccel) {
    case HWAccelDevice::NVENC:
      devType = AV_HWDEVICE_TYPE_CUDA;
      devName = "CUDA";
      break;
    case HWAccelDevice::VAAPI:
      devType = AV_HWDEVICE_TYPE_VAAPI;
      devName = "VAAPI";
      break;
    case HWAccelDevice::QuickSync:
      devType = AV_HWDEVICE_TYPE_QSV;
      devName = "QSV";
      break;
    case HWAccelDevice::AMF:
      devType = AV_HWDEVICE_TYPE_D3D11VA;
      devName = "D3D11VA";
      break;
    case HWAccelDevice::None:
      return Result<void>::ok();
  }

  AVBufferRef* devCtx = nullptr;
  char devPath[64] = {0};

  if (devType == AV_HWDEVICE_TYPE_VAAPI) {
    snprintf(devPath, sizeof(devPath), "/dev/dri/renderD%d", 128 + settings.video.hwDevice);
  } else if (devType == AV_HWDEVICE_TYPE_CUDA) {
    snprintf(devPath, sizeof(devPath), "%d", settings.video.hwDevice);
  }

  int ret = av_hwdevice_ctx_create(&devCtx, devType,
    devPath[0] ? devPath : nullptr, nullptr, 0);

  if (ret < 0) {
    return Result<void>::err(std::string("Failed to create ") + devName +
      " device context: " + ffmpegError(ret));
  }

  hwDeviceCtx_.reset(devCtx);
  swPixelFormat_ = AV_PIX_FMT_YUV420P;

  LOG_INFO("HW device context created: {} (device {})", devName, settings.video.hwDevice);
  return Result<void>::ok();
}

Result<void> VideoRecorderFFmpeg::initHWFrames(const EncoderSettings& settings) {
  if (!hwDeviceCtx_) {
    return Result<void>::err("HW device context not initialized");
  }

  AVBufferRef* framesRef = av_hwframe_ctx_alloc(hwDeviceCtx_.get());
  if (!framesRef) {
    return Result<void>::err("Failed to allocate HW frames context");
  }

  AVHWFramesContext* framesCtx = reinterpret_cast<AVHWFramesContext*>(framesRef->data);
  framesCtx->format = getHWPixelFormat(settings);
  framesCtx->sw_format = swPixelFormat_;
  framesCtx->width = settings.video.width;
  framesCtx->height = settings.video.height;
  framesCtx->initial_pool_size = 20;

  int ret = av_hwframe_ctx_init(framesRef);
  if (ret < 0) {
    av_buffer_unref(&framesRef);
    return Result<void>::err("Failed to init HW frames: " + ffmpegError(ret));
  }

  hwFramesCtx_.reset(framesRef);
  videoCodecCtx_->hw_frames_ctx = av_buffer_ref(framesRef);

  return Result<void>::ok();
}

} // namespace vc

#if LIBAVCODEC_VERSION_MAJOR >= 60
#pragma GCC diagnostic pop
#endif
