#pragma once
/**
 * @file AudioFileDecoder.hpp
 * @file Purpose: headless media file -> interleaved f32 PCM, for offline render.
 *
 * @section Why this exists
 * The product's end goal is rendering a music video offline, from a file, with no
 * window, no audio device and no Suno session. Everything on that path already
 * exists except this class: `VisualizerRenderer` is a plain class whose
 * `isExposed` is a *parameter* (`VisualizerRenderer.hpp::render`), so an offscreen
 * worker just passes true; `AudioQueue::pushAll` is a producer seam that knows
 * nothing about QMediaPlayer (`AudioQueue.hpp:84`); swresample and swscale are
 * already linked. What was missing was the file->PCM half of the bridge. Two
 * production demuxers already existed and neither decodes *audio* into a queue:
 * `SubtitleBurnIn.cpp:225` opens a video file to transcode it, and
 * `VideoRecorderFFmpeg` opens nothing. So this is the first audio demuxer in the
 * tree, not the first demuxer.
 *
 * This class therefore needs no GUI, no audio device and no network. It never
 * logs: every failure is a returned value, and choosing a log level is the
 * caller's business.
 *
 * @section Why it is a pull API (chunk handout), not a producer
 * Measured and read out of the queue rather than assumed:
 *
 *  - `AudioQueue::pushAll` writes to **both** vizQueue_ and recQueue_ and returns
 *    `a && b` (`AudioQueue.hpp:77-81`). A decoder that pushed internally would
 *    have to know a consumer topology that does not exist yet -- the render
 *    worker's visualizer side drains `viz`, and nothing at all drains `rec` --
 *    so every push would fail for a reason the decoder cannot see or fix.
 *  - `pushInternal` stops at the **first** refused AudioFrame and counts the
 *    remainder as dropped (`AudioQueue.hpp:221-224`). For a renderer a dropped
 *    frame is a permanent audio/video desync, so "the queue is full" has to be
 *    the *caller's* decision -- it is the only party that can drain.
 *  - Backpressure then needs no sleep and no timeout anywhere in here.
 *    `nextChunk()` does one unit of real demux/decode work or returns
 *    `EndOfStream`; there is no loop inside this class that can spin on a full
 *    queue or an absent consumer. That property is structural rather than a
 *    tuned timeout, which is what a worker on someone else's spare core needs.
 *
 * `pushChunkTo()` is the convenience for the common case, and it reports exact
 * accepted/dropped frame counts rather than a bool, because a bool cannot say
 * *which* queue lost them.
 *
 * @section Exactness -- why the output can be asserted exactly
 * The fixture for the tests is a hand-written s16 WAV, and every claim below was
 * measured against this project's own FFmpeg (9.0.2; libavcodec 63.1.102,
 * libavformat 63.1.102, libswresample 7.1.102) rather than assumed:
 *
 *  1. A DC s16 input survives a 1:1 s16->flt conversion **bit-exactly**:
 *     16384 -> exactly `0.5f`, -32768 -> exactly `-1.0f`, 32767 -> exactly
 *     `0.999969482421875f`. Both steps are power-of-two scalings, so
 *     `static_cast<f32>(i16) * (1.0f / 32768.0f)` is exact for all 65536
 *     inputs -- the same argument `PcmFormat.hpp` makes for `kInt16Scale`. That
 *     is what makes an exact-value assertion meaningful here: a wrong-but-nonzero
 *     result, the failure mode this repo has been bitten by twice, cannot pass.
 *
 *  2. **libswresample's default mono->stereo rematrix is -3 dB, not unity.**
 *     Measured: a mono DC of `0.5f` arrives as exactly `0.353553385f`,
 *     bit-identical to `0.5f * 0.70710678118654752440f` (`0x3eb504f3`) -- i.e.
 *     multiplied by 1/sqrt(2), which is what spreading a *centre* channel to a
 *     stereo pair should do. That is not what this project wants:
 *     `AudioQueue::pushInternal` duplicates a mono source at unity
 *     (`AudioQueue.hpp:215-218`), so every mono input would come out 3 dB
 *     quieter through the decoder than through live playback. The resampler is
 *     therefore given an explicit unity matrix for mono->stereo; measured, that
 *     restores bit-exact `0.5f` with `L == R` for every frame.
 *
 *  3. **A fully drained resample of a whole file yields exactly
 *     `av_rescale(inFrames, outRate, inRate)` output frames.** Measured on 4000
 *     input frames: 8k->48k gives 24000, 8k->44.1k gives 22050, 16k->48k gives
 *     12000. There is no residual resampler-latency deficit once swr is drained,
 *     so a caller can predict the output length from the input length and two
 *     rates -- which is what a worker needs to size buffers and what a render
 *     receipt needs to record.
 *
 * @section The variable-frame-size case
 * `AVCodecContext::frame_size` is **0** for `pcm_s16le` (measured), which is
 * legal and means "the codec decides" -- and it is 0 for several of the codecs
 * this project would decode. Nothing in this class reads it. The decode loop is
 * driven entirely by `AVFrame::nb_samples` (as reported by
 * `avcodec_receive_frame`, which is the only truth about how much audio a frame
 * actually carries) and every buffer is sized from
 * `swr_get_out_samples()` rather than from any declared frame size.
 *
 * This is the same trap `VideoRecorderFFmpeg::initAudioStream` documents at
 * `:854-872`, where it had to *refuse* a codec reporting `frame_size <= 0` --
 * an encoder must choose a fixed frame size to encode into. A decoder has no such
 * constraint: a variable-size frame is simply a frame of whatever size arrived,
 * so refusing it here would throw away a working input.
 *
 * The related trap, also measured: the frame's channel layout must be read from
 * `extended_data`, never from `data[0]` on its own, because a planar codec
 * (FLAC, most AAC) has one pointer per plane and `data[0]` is only the first.
 *
 * @section The ch_layout finding, recorded so nobody "fixes" it
 * For a WAV `pcm_s16le` stream, `AVCodecParameters::ch_layout` -- and therefore
 * the opened `AVCodecContext::ch_layout` -- carries
 * `order == AV_CHANNEL_ORDER_UNSPEC` with `u.mask == 0` while `nb_channels` is
 * correct (measured: nb_channels 2, mask 0, order 0). libswresample handles that
 * correctly, working from `nb_channels`; the stereo DC round-trips bit-exactly
 * with the layout left alone. So **no repair is applied**, deliberately: a
 * rewrite here would be a fix for a bug that does not exist, and a comment
 * claiming otherwise would outlive the reason for it.
 *
 * @section Truncation is not reliably detectable, and this says so
 * Measured on a WAV cut mid-`data`: `av_read_frame` returns a **clean**
 * `AVERROR_EOF`, libavformat logs `Packet corrupt (stream = 0, dts = NOPTS)` at
 * ERROR, **and it rewrites the duration from the bytes it actually read** -- on
 * both `AVFormatContext` and `AVStream` -- to 61125 us for a file whose header
 * declares 4000 frames at 8 kHz (0.5 s), i.e. exactly the 489 frames it decoded.
 * So neither the read result nor the duration distinguishes a truncated WAV from
 * a short one, and a decoder that claimed otherwise would be guessing. Every
 * corruption tried -- cut mid-payload, cut into the header, a `data` chunk size
 * of 0x7FFFFFFF, no payload at all -- produced the same clean EOF.
 *
 * `outcome()` therefore reports `Truncated` only on evidence: a non-EOF demuxer
 * stop code, or a duration the decoded audio falls short of by more than
 * `kDurationSlackSeconds`. The raw signals (`demuxerStopCode()`,
 * `declaredDurationSeconds()`, `positionSeconds()`) are public so a caller that
 * holds a trustworthy duration -- a render job knows the duration it asked for --
 * can make its own call instead of inheriting this class's guess.
 */

#include "util/Types.hpp"

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc {

class AudioQueue;

/// Why a decode could not start, or stopped.
///
/// Named rather than a bare string so a caller can branch on the class of
/// failure and a test can assert the *kind*, which is what makes "fail closed and
/// say why" checkable rather than a message-matching exercise.
enum class DecodeErrorKind : u8 {
    /// `nextChunk()`/`pushChunkTo()` before a successful `open()`. Not a decode
    /// failure, so it does not move `outcome()`.
    NotOpen,
    /// The requested output rate/channel count/chunk size is not usable. Checked
    /// before any file is touched, so it cannot be blamed on the input.
    InvalidOutputSpec,
    /// `avformat_open_input` refused the path: missing, unreadable, or not media.
    OpenFailed,
    /// `avformat_find_stream_info` could not identify the container.
    StreamInfoFailed,
    /// The container has no audio stream at all (a video file, an image, a
    /// subtitle track).
    NoAudioStream,
    /// The stream's codec id has no decoder in this build.
    NoDecoder,
    /// The stream declared no channel count or no sample rate.
    UnusableStreamParameters,
    /// The audio stream opened and then decoded to **zero** frames.
    ///
    /// Its own kind because the alternative is the failure this repo keeps being
    /// bitten by: `EndOfStream` on the first call, `outcome() == Complete`, and a
    /// caller that checks neither renders a zero-length video and calls it a
    /// success. Measured on a WAV carrying a valid header and no payload.
    EmptyAudioStream,
    /// `avcodec_open2` refused the decoder.
    DecoderOpenFailed,
    /// The resampler could not be configured or initialised, or `swr_convert`
    /// failed. Checked: `VideoRecorderFFmpeg` discarded these returns while
    /// dereferencing the context on the next line.
    ResamplerFailed,
    /// `avcodec_send_packet`/`avcodec_receive_frame` failed, or the demuxer
    /// stopped for a reason other than end-of-file.
    DecodeFailed,
};

/// Stable identifier for an error kind, for logs and test assertions.
[[nodiscard]] std::string_view decodeErrorKindName(DecodeErrorKind kind) noexcept;

/// A decode failure, with the reason and libav's own words.
struct DecodeError {
    DecodeErrorKind kind{DecodeErrorKind::NotOpen};
    /// Never empty. Carries `av_strerror`'s text when `avCode` is non-zero,
    /// because "DecodeFailed" alone is not a diagnosis.
    std::string message;
    /// The raw AVERROR, or 0 when the failure did not come from libav.
    int avCode{0};

    [[nodiscard]] std::string describe() const;
};

/// How the stream ended.
enum class DecodeOutcome : u8 {
    /// Nothing opened yet, or `close()` was called.
    NotStarted,
    /// More audio may come.
    Streaming,
    /// The demuxer reached a clean end-of-file and nothing contradicted the
    /// container's declared duration.
    Complete,
    /// Ended early on evidence: a non-EOF demuxer stop code, or a declared
    /// duration the decoded audio falls short of by more than
    /// `kDurationSlackSeconds`. See the header note on why a truncated WAV is
    /// *not* in this bucket -- it reports `Complete`, honestly, because
    /// libavformat re-estimated the duration to match what it read.
    Truncated,
    /// A hard failure; `lastError()` says which.
    Failed,
};

[[nodiscard]] std::string_view decodeOutcomeName(DecodeOutcome outcome) noexcept;

/// What one `nextChunk()` produced. An enum rather than a bool because "no chunk
/// because the stream ended" and "no chunk because it failed" must not share a
/// return value, and neither may share one with "a chunk of zero frames".
enum class ChunkStatus : u8 {
    /// `out` was filled.
    Ready,
    /// The stream ended. `outcome()` says how.
    EndOfStream,
};

/// One block of decoded, resampled, interleaved f32 PCM.
///
/// Owned, and reused across calls: `nextChunk()` overwrites it, so a caller that
/// needs the samples afterwards must copy them. That is deliberate -- the decoder
/// keeps one scratch buffer for resampled output and copying out of it per chunk
/// would double the traffic for no benefit.
struct PcmChunk {
    /// Interleaved, `channels` samples per frame.
    std::vector<f32> samples;
    u32 channels{0};
    u32 sampleRate{0};

    [[nodiscard]] u32 frameCount() const noexcept {
        return channels > 0 ? static_cast<u32>(samples.size()) / channels : 0u;
    }
    [[nodiscard]] std::span<const f32> interleaved() const noexcept {
        return {samples.data(), samples.size()};
    }
    void clear() noexcept {
        samples.clear();
        channels = 0;
        sampleRate = 0;
    }
};

/// Exact frame accounting for one `AudioQueue` hand-off.
///
/// Both queues are reported separately because `AudioQueue::pushAll` writes to
/// both and returns `a && b`: a bool cannot distinguish "the visualizer queue
/// took it all" from "the recorder queue took it all" from "both did", and a
/// render worker cares which. The counts are read from the queue's own drop
/// counters rather than inferred, so they are exact even though `pushAll` only
/// returns a bool.
struct QueuePush {
    u32 frames{0};
    u32 framesDroppedByVisualizer{0};
    u32 framesDroppedByRecorder{0};
    /// `pushAll`'s own verdict: true only when *both* queues took every frame.
    bool acceptedByBothQueues{false};

    [[nodiscard]] u32 framesAcceptedByVisualizer() const noexcept {
        return frames - framesDroppedByVisualizer;
    }
    [[nodiscard]] u32 framesAcceptedByRecorder() const noexcept {
        return frames - framesDroppedByRecorder;
    }
    [[nodiscard]] bool lossless() const noexcept {
        return framesDroppedByVisualizer == 0 && framesDroppedByRecorder == 0;
    }
};

/// Blocking file -> PCM decoder. See the header note for the measurements behind
/// its behaviour.
///
/// Typical use, and the shape the render worker should take:
///
/// @code
/// AudioFileDecoder decoder({.sampleRate = 48000, .channels = 2});
/// if (auto opened = decoder.open(path); !opened) return opened.error();
/// AudioQueue queue;
/// PcmChunk chunk;
/// while (true) {
///     while (queue.vizDepth() >= kHighWaterFrames) drainSomeFrames(queue);
///     auto next = decoder.nextChunk(chunk);
///     if (!next) return next.error();
///     if (*next == ChunkStatus::EndOfStream) break;
///     (void)decoder... // chunk is already decoded; push it
///     queue.pushAll(chunk.samples.data(), chunk.frameCount(), 2, 48000);
/// }
/// @endcode
///
/// Not thread-safe: one decoder belongs to one thread, which is the same
/// contract `VideoRecorderFFmpeg` has and the same reason (its mutex exists to
/// serve a QThread boundary, not to make a class reentrant).
class AudioFileDecoder {
public:
    /// The PCM shape handed out. Channels are capped at 2 because `AudioFrame`
    /// is stereo-only (`AudioQueue.hpp:31`), so a wider sink could not reach a
    /// consumer intact.
    struct OutputSpec {
        u32 sampleRate{48000};
        u32 channels{2};
        /// Upper bound on the frames in one chunk. Rate conversion makes the
        /// resampler's output an arbitrary length, so leftovers are carried
        /// internally and this is a *cap*, not a size. 4096 frames is 85 ms at
        /// 48 kHz: small enough that backpressure is responsive, large enough
        /// that the per-chunk overhead is noise.
        u32 chunkFrames{4096};
    };

    static constexpr u32 kMaxChannels = 2;
    static constexpr u32 kMinSampleRate = 1000;
    static constexpr u32 kMaxSampleRate = 384000;
    static constexpr u32 kMaxChunkFrames = 1u << 20;

    /// How far the decoded duration may fall short of a container's declared
    /// duration before `outcome()` reports `Truncated`.
    ///
    /// A lossy codec's declared duration is only approximate: AAC carries
    /// `initial_padding` (measured at 1024 samples for the native encoder, ~21 ms
    /// at 48 kHz) and MP3 carries its own encoder delay, so a zero tolerance
    /// would flag every MP3 and AAC render as truncated. 50 ms is comfortably
    /// above those and comfortably below any truncation a user would notice.
    static constexpr f64 kDurationSlackSeconds = 0.05;

    /// Deliberately NOT `= {}`: a default argument cannot read the enclosing
    /// class's default member initialisers, so `OutputSpec{}` here is a hard
    /// error ([class.mem] -- "default member initializer required within
    /// definition of enclosing class"). Two overloads, one of which delegates.
    AudioFileDecoder();
    explicit AudioFileDecoder(OutputSpec spec);
    ~AudioFileDecoder();

    AudioFileDecoder(const AudioFileDecoder&) = delete;
    AudioFileDecoder& operator=(const AudioFileDecoder&) = delete;
    AudioFileDecoder(AudioFileDecoder&&) noexcept;
    AudioFileDecoder& operator=(AudioFileDecoder&&) noexcept;

    /// Open `path` and prepare to decode its first audio stream. Re-opening an
    /// already-open decoder closes the previous file first, so there is no state
    /// in which two demuxers are live on one object.
    [[nodiscard]] std::expected<void, DecodeError> open(const fs::path& path);

    /// Release every libav resource and reset all counters. Idempotent, and safe
    /// on a decoder that was never opened.
    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    /// Decode the next chunk into `out`.
    ///
    /// Returns `EndOfStream` once the stream is exhausted, at which point
    /// `outcome()` is final. A returned error means the stream is unusable and
    /// `outcome()` is `Failed`; `out` is cleared in that case, never left holding
    /// a partial block.
    ///
    /// Never blocks on anything but the file itself and never waits for a
    /// consumer: it does one unit of real work or it is done.
    [[nodiscard]] std::expected<ChunkStatus, DecodeError> nextChunk(PcmChunk& out);

    /// `nextChunk()` followed by `AudioQueue::pushAll`, with the queue's own drop
    /// counters read back so the frame accounting is exact.
    ///
    /// A non-lossless result is reported, not repaired: for a renderer a dropped
    /// frame is a desync, so the caller must decide whether to drain and retry,
    /// and it cannot do that if the loss is invisible.
    [[nodiscard]] std::expected<QueuePush, DecodeError> pushChunkTo(AudioQueue& queue);

    [[nodiscard]] const OutputSpec& spec() const noexcept;
    [[nodiscard]] u32 sampleRate() const noexcept;
    [[nodiscard]] u32 channels() const noexcept;
    [[nodiscard]] u32 chunkFrames() const noexcept;

    /// Output frames produced so far, counted as they are resampled rather than
    /// as they are handed out -- so this is "how far into the file am I" and does
    /// not stall behind a consumer.
    [[nodiscard]] u64 framesDecoded() const noexcept;

    /// `framesDecoded()` at the output rate. Exactly comparable against
    /// `declaredDurationSeconds()` for a lossless source.
    [[nodiscard]] f64 positionSeconds() const noexcept;

    /// The duration of the **audio stream**, in seconds, or -1 when it declares
    /// none. This is deliberately not `AVFormatContext::duration`: for a
    /// multi-track container that is the *longest* track, so comparing decoded
    /// audio against it would flag every file whose picture outlasts its sound
    /// as truncated. The container value is used only as a fallback when the
    /// audio stream carries no duration of its own.
    ///
    /// See the header note: libavformat may have re-estimated this from the bytes
    /// it could read, which is what makes it unusable as a truncation signal for
    /// some containers.
    [[nodiscard]] f64 declaredDurationSeconds() const noexcept;

    /// `AVFormatContext::duration` in seconds, or -1. Kept because a render
    /// receipt wants the whole-file figure, not the audio track's.
    [[nodiscard]] f64 containerDurationSeconds() const noexcept;

    /// The exact code `av_read_frame` returned when the stream stopped. 0 means
    /// the demuxer has not stopped. `AVERROR_EOF` is a clean end.
    [[nodiscard]] int demuxerStopCode() const noexcept;

    /// The decoder's name, or empty before a successful `open()`.
    [[nodiscard]] std::string_view codecName() const noexcept;

    /// The path handed to `open()`, or empty.
    [[nodiscard]] const std::string& sourcePath() const noexcept;

    [[nodiscard]] DecodeOutcome outcome() const noexcept;

    /// The most recent failure, sticky. Meaningful when `outcome()` is `Failed`
    /// or when a call returned an error.
    [[nodiscard]] const DecodeError& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vc
