// AudioFileDecoder.cpp - the file->PCM half of the offline render bridge.
//
// The design rationale, the measurements behind it, and the two libav traps this
// file deliberately does not fall into (frame_size, and the ch_layout finding that
// turned out not to be a bug) all live in AudioFileDecoder.hpp. Read that first.
//
// No logging here on purpose: every failure is a returned DecodeError, and this
// class has no business choosing a level for its caller.

#include "recorder/AudioFileDecoder.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
}

#include "audio/AudioQueue.hpp"
#include "recorder/FFmpegUtils.hpp"
#include "recorder/ResamplerEngine.hpp"

#include <algorithm>

namespace vc {

namespace {

/// Frames -> seconds at a given rate, for the progress and truncation
/// comparisons. Zero when the rate is unusable, so a decoder that never opened
/// reports 0 rather than a NaN that would compare false against everything.
f64 secondsFor(u64 frames, u32 sampleRate) noexcept {
    return sampleRate > 0 ? static_cast<f64>(frames) / static_cast<f64>(sampleRate) : 0.0;
}

/// `std::unexpected` spelled once. libc++'s `expected` has no implicit
/// `expected<void, E>` construction from a bare E on every release, so this is the
/// portable spelling and it appears in one place rather than at every `return`.
std::unexpected<DecodeError> asUnexpected(DecodeError error) {
    return std::unexpected<DecodeError>(std::move(error));
}

} // namespace

std::string_view decodeErrorKindName(DecodeErrorKind kind) noexcept {
    switch (kind) {
        case DecodeErrorKind::NotOpen:
            return "NotOpen";
        case DecodeErrorKind::InvalidOutputSpec:
            return "InvalidOutputSpec";
        case DecodeErrorKind::OpenFailed:
            return "OpenFailed";
        case DecodeErrorKind::StreamInfoFailed:
            return "StreamInfoFailed";
        case DecodeErrorKind::NoAudioStream:
            return "NoAudioStream";
        case DecodeErrorKind::NoDecoder:
            return "NoDecoder";
        case DecodeErrorKind::UnusableStreamParameters:
            return "UnusableStreamParameters";
        case DecodeErrorKind::EmptyAudioStream:
            return "EmptyAudioStream";
        case DecodeErrorKind::DecoderOpenFailed:
            return "DecoderOpenFailed";
        case DecodeErrorKind::ResamplerFailed:
            return "ResamplerFailed";
        case DecodeErrorKind::DecodeFailed:
            return "DecodeFailed";
    }
    return "Unknown";
}

std::string DecodeError::describe() const {
    std::string out(decodeErrorKindName(kind));
    out += ": ";
    out += message;
    return out;
}

std::string_view decodeOutcomeName(DecodeOutcome outcome) noexcept {
    switch (outcome) {
        case DecodeOutcome::NotStarted:
            return "NotStarted";
        case DecodeOutcome::Streaming:
            return "Streaming";
        case DecodeOutcome::Complete:
            return "Complete";
        case DecodeOutcome::Truncated:
            return "Truncated";
        case DecodeOutcome::Failed:
            return "Failed";
    }
    return "Unknown";
}

// ── Impl ─────────────────────────────────────────────────────────────────────
// Owns every libav handle. Held by pointer so the public header carries no FFmpeg
// includes at all: this class is the one a render worker or a CLI will include,
// and none of them care that it is built on libavformat.

struct AudioFileDecoder::Impl {
    ~Impl() {
        // For an AV_CHANNEL_ORDER_CUSTOM layout the mask was malloc'd by libav and
        // this is the only place it can be returned. NATIVE and UNSPEC layouts own
        // nothing and uninit is a no-op, so this is safe on every path.
        av_channel_layout_uninit(&outLayout);
    }

    OutputSpec spec{};

    AVFormatContextInPtr format;
    AVCodecContextPtr codec;
    SwrContextPtr resampler;
    AVPacketPtr packet;
    AVFramePtr frame;
    AVChannelLayout outLayout{};

    int streamIndex{-1};
    int inChannels{0};
    int inSampleRate{0};
    std::string codecName;
    std::string sourcePath;

    /// Resampled PCM not yet handed to the caller. `pendingHead` is the read
    /// cursor: erasing from the front per chunk would be O(n) on a buffer a rate
    /// conversion can leave holding thousands of frames.
    std::vector<f32> pending;
    usize pendingHead{0};
    /// One `swr_convert` destination, reused. Sized per call from
    /// `swr_get_out_samples`, never from any declared frame size.
    std::vector<f32> scratch;

    u64 framesDecoded{0};
    /// The audio track's own duration, and the container's separately. Two fields
    /// because they are two different questions: "how long is the audio" (which
    /// is what the truncation check compares against) and "how long is the file"
    /// (which is what a render receipt records).
    f64 declaredDurationSeconds{-1.0};
    f64 containerDurationSeconds{-1.0};
    int demuxerStopCode{0};
    bool sawEof{false};
    bool drained{false};
    DecodeOutcome outcome{DecodeOutcome::NotStarted};
    DecodeError lastError{};

    [[nodiscard]] usize pendingFrames() const noexcept {
        return (pending.size() - pendingHead) / spec.channels;
    }

    /// Build the error, remember it, and decide whether it ends the stream.
    /// `NotOpen` is the one kind that is a caller mistake rather than a decode
    /// failure, so it alone leaves `outcome` alone. `what` is taken by value
    /// because several call sites build it with string concatenation, and a
    /// `std::string_view` parameter would dangle on every one of them.
    std::expected<void, DecodeError> fail(DecodeErrorKind kind, std::string what, int avCode = 0) {
        DecodeError error;
        error.kind = kind;
        error.avCode = avCode;
        if (avCode != 0) {
            what += ": ";
            what += ffmpegError(avCode);
        }
        error.message = std::move(what);
        lastError = error;
        if (kind != DecodeErrorKind::NotOpen) {
            outcome = DecodeOutcome::Failed;
        }
        return asUnexpected(std::move(error));
    }

    std::expected<void, DecodeError> buildResampler();
    std::expected<void, DecodeError> pump();
    std::expected<void, DecodeError> resampleFrame(AVFrame& frame);
    std::expected<void, DecodeError> drainCodecAndResampler();
    void appendPending(std::span<const f32> samples);
    void finish();
};

std::expected<void, DecodeError> AudioFileDecoder::Impl::buildResampler() {
    // AV_SAMPLE_FMT_FLT is *packed*, so `out` is one pointer. The input layout,
    // sample format and rate come from the opened decoder context rather than from
    // the stream's codecpar: the context is what avcodec_receive_frame will
    // actually produce, and for a decoder with a bitstream or hardware variant
    // those differ.
    av_channel_layout_default(&outLayout, static_cast<int>(spec.channels));

    SwrContext* raw = nullptr;
    const int allocated = swr_alloc_set_opts2(&raw, &outLayout, AV_SAMPLE_FMT_FLT,
                                              static_cast<int>(spec.sampleRate), &codec->ch_layout,
                                              codec->sample_fmt, codec->sample_rate, 0, nullptr);
    // Both checked. VideoRecorderFFmpeg discarded these returns and then
    // dereferenced the context on the very next line, so a resampler that could
    // not be configured became a null dereference inside swr_convert rather than
    // an error.
    if (allocated < 0 || !raw) {
        if (raw) swr_free(&raw);
        return fail(DecodeErrorKind::ResamplerFailed, "swr_alloc_set_opts2 failed", allocated);
    }

    // Mono -> stereo at unity, and this must happen BEFORE swr_init.
    //
    // Two measured reasons. First, libswresample's default mono->stereo matrix
    // multiplies by 1/sqrt(2) -- a mono DC of 0.5f arrives as exactly
    // 0.353553385f, bit-identical to 0.5f * 0.70710678118654752440f -- because
    // it treats the source as a centre channel. That is 3 dB down, and
    // AudioQueue::pushInternal duplicates mono at unity (AudioQueue.hpp:215-218),
    // so leaving the default in place would make every mono source quieter
    // through the decoder than through playback. Second, FFmpeg's own header says
    // the context must be "allocated, not yet initialized" for swr_set_matrix.
    //
    // stride 1 with {1, 1}: matrix[i + stride * o] is the weight of input i in
    // output o, so out0 = 1*in0 and out1 = 1*in0. Measured: restores bit-exact
    // 0.5f with L == R for every frame.
    if (inChannels == 1 && spec.channels == 2) {
        const double unity[2] = {1.0, 1.0};
        const int matrixed = swr_set_matrix(raw, unity, 1);
        if (matrixed < 0) {
            swr_free(&raw);
            return fail(DecodeErrorKind::ResamplerFailed, "swr_set_matrix failed", matrixed);
        }
    }

    // Engine selection, same ordering constraint as the matrix above and for the
    // same reason: options are settable only while the context is allocated and
    // not yet initialized. This is the one resampler in the tree that genuinely
    // *rate*-converts -- an arbitrary source rate becomes spec.sampleRate -- so it
    // is the one whose passband soxr improves. The call cannot fail the decode:
    // a refusal leaves libswresample's own resampler in place (measured) and
    // applyEngine has already logged why.
    applyEngine(raw, "AudioFileDecoder");

    if (const int inited = swr_init(raw); inited < 0) {
        swr_free(&raw);
        return fail(DecodeErrorKind::ResamplerFailed, "swr_init failed", inited);
    }
    resampler.reset(raw);
    return {};
}

void AudioFileDecoder::Impl::appendPending(std::span<const f32> samples) {
    if (samples.empty()) return;
    if (pendingHead > 0 && pendingHead == pending.size()) {
        pending.clear();
        pendingHead = 0;
    }
    pending.insert(pending.end(), samples.begin(), samples.end());
}

std::expected<void, DecodeError> AudioFileDecoder::Impl::resampleFrame(AVFrame& decoded) {
    // nb_samples, never AVCodecContext::frame_size. Measured: pcm_s16le reports
    // frame_size == 0, which is legal and means the codec decides, so a buffer
    // sized from it would be empty. The frame's own count is the only truth about
    // how much audio arrived.
    const int inSamples = decoded.nb_samples;
    if (inSamples <= 0) return {};

    // The documented upper bound for the *next* convert. Allocating exactly this
    // is safe: out_count is a maximum, so swr cannot write past it.
    const int capacity = swr_get_out_samples(resampler.get(), inSamples);
    if (capacity <= 0) {
        return fail(DecodeErrorKind::ResamplerFailed,
                    "swr_get_out_samples reported no output capacity");
    }

    scratch.assign(static_cast<usize>(capacity) * spec.channels, 0.0f);
    u8* outPlanes[1] = {reinterpret_cast<u8*>(scratch.data())};
    // extended_data, never data[0]: a planar codec (FLAC, most AAC) has one
    // pointer per plane there and swr expects the array, so passing data[0] would
    // read only the first plane. The reinterpret_cast is the one FFmpeg's own
    // examples use; it is a qualification change across two pointer levels, which
    // no implicit conversion performs.
    const int produced =
            swr_convert(resampler.get(), outPlanes, capacity,
                        reinterpret_cast<const u8* const*>(decoded.extended_data), inSamples);
    if (produced < 0) {
        return fail(DecodeErrorKind::ResamplerFailed, "swr_convert failed", produced);
    }

    appendPending(
            std::span<const f32>{scratch.data(), static_cast<usize>(produced) * spec.channels});
    framesDecoded += static_cast<u64>(produced);
    return {};
}

std::expected<void, DecodeError> AudioFileDecoder::Impl::drainCodecAndResampler() {
    if (drained) return {};
    drained = true;

    // Flush the decoder. A null packet is the documented flush signal; a codec
    // with encoder-style delay (AAC's initial_padding, measured at 1024 samples)
    // is still holding samples until it arrives.
    if (const int flushed = avcodec_send_packet(codec.get(), nullptr);
        flushed < 0 && flushed != AVERROR_EOF) {
        return fail(DecodeErrorKind::DecodeFailed, "avcodec_send_packet(flush) failed", flushed);
    }
    while (true) {
        const int got = avcodec_receive_frame(codec.get(), frame.get());
        if (got == AVERROR_EOF || got == AVERROR(EAGAIN)) break;
        if (got < 0) {
            return fail(DecodeErrorKind::DecodeFailed, "avcodec_receive_frame failed", got);
        }
        const auto resampled = resampleFrame(*frame);
        av_frame_unref(frame.get());
        if (!resampled) return resampled;
    }

    // Then flush the resampler. Without this a rate conversion loses its tail: the
    // filter's internal delay never becomes output. Measured over a whole file,
    // the drained total is exactly av_rescale(inFrames, outRate, inRate) -- with
    // the flush removed it falls short by the resampler's latency, which is how a
    // three-minute render comes out with a clipped last syllable.
    while (true) {
        const int capacity = swr_get_out_samples(resampler.get(), 0);
        if (capacity <= 0) break;
        scratch.assign(static_cast<usize>(capacity) * spec.channels, 0.0f);
        u8* outPlanes[1] = {reinterpret_cast<u8*>(scratch.data())};
        const int produced = swr_convert(resampler.get(), outPlanes, capacity, nullptr, 0);
        if (produced <= 0) break; // also absorbs the AVERROR the API may return
        appendPending(
                std::span<const f32>{scratch.data(), static_cast<usize>(produced) * spec.channels});
        framesDecoded += static_cast<u64>(produced);
    }
    return {};
}

std::expected<void, DecodeError> AudioFileDecoder::Impl::pump() {
    if (sawEof) return drainCodecAndResampler();

    const int read = av_read_frame(format.get(), packet.get());
    if (read < 0) {
        demuxerStopCode = read;
        if (read == AVERROR_EOF) {
            sawEof = true;
            return drainCodecAndResampler();
        }
        return fail(DecodeErrorKind::DecodeFailed, "av_read_frame failed", read);
    }

    // A packet for another stream. Forwarding only our own is what keeps video
    // interleaved out of the decode path; a caller cannot observe the skip, and
    // would otherwise read a long run of them as a stall.
    if (packet->stream_index != streamIndex) {
        av_packet_unref(packet.get());
        return {};
    }

    const int sent = avcodec_send_packet(codec.get(), packet.get());
    av_packet_unref(packet.get());
    if (sent < 0 && sent != AVERROR(EAGAIN)) {
        return fail(DecodeErrorKind::DecodeFailed, "avcodec_send_packet failed", sent);
    }

    // Unbounded, because the API is: one send can release several frames, and
    // stopping at the first would leave the rest for the next packet to claim as
    // its own.
    while (true) {
        const int got = avcodec_receive_frame(codec.get(), frame.get());
        if (got == AVERROR(EAGAIN) || got == AVERROR_EOF) break;
        if (got < 0) {
            return fail(DecodeErrorKind::DecodeFailed, "avcodec_receive_frame failed", got);
        }
        const auto resampled = resampleFrame(*frame);
        av_frame_unref(frame.get());
        if (!resampled) return resampled;
    }
    return {};
}

void AudioFileDecoder::Impl::finish() {
    if (outcome == DecodeOutcome::Failed) return;

    // A demuxer that stopped for a reason other than end-of-file ran out of usable
    // data on a local file, which is what truncation looks like from here. A data
    // error is the unambiguous case; anything else (EIO, EMFILE) is an I/O
    // failure, and calling that "truncated" would be a different guess.
    if (demuxerStopCode != 0 && demuxerStopCode != AVERROR_EOF) {
        const bool dataError = demuxerStopCode == AVERROR_INVALIDDATA ||
                               demuxerStopCode == AVERROR(EILSEQ) ||
                               demuxerStopCode == AVERROR(ENOSPC);
        if (dataError) {
            outcome = DecodeOutcome::Truncated;
            return;
        }
        outcome = DecodeOutcome::Failed;
        lastError =
                DecodeError{DecodeErrorKind::DecodeFailed,
                            "demuxer stopped: " + ffmpegError(demuxerStopCode), demuxerStopCode};
        return;
    }

    // The duration check, and it is deliberately narrow. libavformat REWRITES
    // AVFormatContext::duration from the bytes it could actually read, so for a WAV
    // cut mid-data it reports exactly the frames it decoded -- measured: 61125 us
    // for a file whose header declares 0.5 s, matching the 489 frames read. The
    // comparison therefore catches containers whose header is authoritative and
    // says nothing about the ones that lie. See the header note.
    if (declaredDurationSeconds > 0.0 &&
        declaredDurationSeconds - secondsFor(framesDecoded, spec.sampleRate) >
                AudioFileDecoder::kDurationSlackSeconds) {
        outcome = DecodeOutcome::Truncated;
        return;
    }
    outcome = DecodeOutcome::Complete;
}

AudioFileDecoder::AudioFileDecoder() : AudioFileDecoder(OutputSpec{}) {}

AudioFileDecoder::AudioFileDecoder(OutputSpec spec) : impl_(std::make_unique<Impl>()) {
    impl_->spec = spec;
}

AudioFileDecoder::~AudioFileDecoder() = default;

AudioFileDecoder::AudioFileDecoder(AudioFileDecoder&&) noexcept = default;

AudioFileDecoder& AudioFileDecoder::operator=(AudioFileDecoder&&) noexcept = default;

std::expected<void, DecodeError> AudioFileDecoder::open(const fs::path& path) {
    close();

    const OutputSpec& spec = impl_->spec;
    if (spec.channels == 0 || spec.channels > kMaxChannels) {
        return impl_->fail(DecodeErrorKind::InvalidOutputSpec,
                           "channels must be 1 or 2 (AudioFrame is stereo-only)");
    }
    if (spec.sampleRate < kMinSampleRate || spec.sampleRate > kMaxSampleRate) {
        return impl_->fail(DecodeErrorKind::InvalidOutputSpec, "sample rate out of range");
    }
    if (spec.chunkFrames == 0 || spec.chunkFrames > kMaxChunkFrames) {
        return impl_->fail(DecodeErrorKind::InvalidOutputSpec, "chunkFrames must be 1..1048576");
    }

    impl_->sourcePath = path.string();

    // Handed to libav as UTF-8 bytes, which is what libavformat's file protocol
    // expects on Windows; fs::path::string() would narrow a wide path to the ANSI
    // code page there and a non-ASCII path would then fail to open. The explicit
    // byte copy is because path.u8string() is std::u8string and does not convert
    // implicitly.
    const std::u8string utf8 = path.u8string();
    const std::string pathUtf8(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    AVFormatContext* rawFormat = nullptr;
    const int opened = avformat_open_input(&rawFormat, pathUtf8.c_str(), nullptr, nullptr);
    if (opened < 0) {
        return impl_->fail(DecodeErrorKind::OpenFailed, "avformat_open_input failed", opened);
    }
    impl_->format.reset(rawFormat);

    if (const int info = avformat_find_stream_info(impl_->format.get(), nullptr); info < 0) {
        return impl_->fail(DecodeErrorKind::StreamInfoFailed, "avformat_find_stream_info failed",
                           info);
    }

    impl_->streamIndex =
            av_find_best_stream(impl_->format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (impl_->streamIndex < 0) {
        return impl_->fail(DecodeErrorKind::NoAudioStream,
                           "no audio stream in " + impl_->sourcePath);
    }
    AVStream* stream = impl_->format->streams[impl_->streamIndex];

    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return impl_->fail(DecodeErrorKind::NoDecoder,
                           "no decoder for codec id " +
                                   std::to_string(static_cast<int>(stream->codecpar->codec_id)));
    }
    impl_->codecName = decoder->name;

    impl_->codec.reset(avcodec_alloc_context3(decoder));
    if (!impl_->codec) {
        return impl_->fail(DecodeErrorKind::DecoderOpenFailed,
                           "avcodec_alloc_context3 returned null");
    }
    if (const int copied = avcodec_parameters_to_context(impl_->codec.get(), stream->codecpar);
        copied < 0) {
        return impl_->fail(DecodeErrorKind::DecoderOpenFailed,
                           "avcodec_parameters_to_context failed", copied);
    }
    if (const int openedCodec = avcodec_open2(impl_->codec.get(), decoder, nullptr);
        openedCodec < 0) {
        return impl_->fail(DecodeErrorKind::DecoderOpenFailed, "avcodec_open2 failed", openedCodec);
    }

    impl_->inChannels = impl_->codec->ch_layout.nb_channels;
    impl_->inSampleRate = impl_->codec->sample_rate;
    if (impl_->inChannels < 1 || impl_->inSampleRate < 1) {
        return impl_->fail(DecodeErrorKind::UnusableStreamParameters,
                           "stream declared " + std::to_string(impl_->inChannels) +
                                   " channels at " + std::to_string(impl_->inSampleRate) + " Hz");
    }

    impl_->packet.reset(av_packet_alloc());
    impl_->frame.reset(av_frame_alloc());
    if (!impl_->packet || !impl_->frame) {
        return impl_->fail(DecodeErrorKind::DecoderOpenFailed,
                           "av_packet_alloc/av_frame_alloc returned null");
    }

    if (const auto resampler = impl_->buildResampler(); !resampler) {
        return resampler;
    }

    impl_->containerDurationSeconds =
            impl_->format->duration > 0
                    ? static_cast<f64>(impl_->format->duration) / static_cast<f64>(AV_TIME_BASE)
                    : -1.0;
    // The audio stream's own duration, preferred, and it has to be: for a
    // multi-track container AVFormatContext::duration is the *longest* track, so
    // comparing decoded audio against it would report Truncated for every file
    // whose picture outlasts its sound -- which is most recorded videos.
    // AVStream::duration is in the stream's own time base, spelled as a division
    // rather than av_q2d because that helper has moved headers between releases.
    const AVRational timeBase = stream->time_base;
    impl_->declaredDurationSeconds = (stream->duration > 0 && timeBase.den > 0)
                                             ? static_cast<f64>(stream->duration) *
                                                       static_cast<f64>(timeBase.num) /
                                                       static_cast<f64>(timeBase.den)
                                             : impl_->containerDurationSeconds;
    impl_->outcome = DecodeOutcome::Streaming;
    return {};
}

void AudioFileDecoder::close() noexcept {
    if (!impl_) return;
    // Order: the codec and the resampler before the container, so nothing can be
    // mid-decode when the stream table it points into goes away. Every deleter is
    // null-safe and takes its pointer by value (FFmpegUtils.hpp:65-70 explains why
    // the demuxer close differs from the muxer close), so this is leak-free on
    // every failure path through open() as well as on the success path.
    impl_->frame.reset();
    impl_->packet.reset();
    impl_->codec.reset();
    impl_->resampler.reset();
    impl_->format.reset();
    av_channel_layout_uninit(&impl_->outLayout);

    impl_->streamIndex = -1;
    impl_->inChannels = 0;
    impl_->inSampleRate = 0;
    impl_->codecName.clear();
    impl_->sourcePath.clear();
    impl_->pending.clear();
    impl_->pendingHead = 0;
    impl_->scratch.clear();
    impl_->scratch.shrink_to_fit();
    impl_->framesDecoded = 0;
    impl_->declaredDurationSeconds = -1.0;
    impl_->containerDurationSeconds = -1.0;
    impl_->demuxerStopCode = 0;
    impl_->sawEof = false;
    impl_->drained = false;
    impl_->outcome = DecodeOutcome::NotStarted;
    impl_->lastError = DecodeError{};
}

bool AudioFileDecoder::isOpen() const noexcept { return impl_ && impl_->format != nullptr; }

std::expected<ChunkStatus, DecodeError> AudioFileDecoder::nextChunk(PcmChunk& out) {
    out.clear();
    if (!isOpen()) {
        // Not a decode failure: calling before open is a caller mistake, so
        // outcome() must not be dragged to Failed by it.
        return asUnexpected(
                impl_->fail(DecodeErrorKind::NotOpen, "nextChunk before a successful open()")
                        .error());
    }
    if (impl_->outcome == DecodeOutcome::Failed) {
        return asUnexpected(impl_->lastError);
    }

    // Fill to the cap or to end-of-stream, whichever comes first. Every iteration
    // is real demux/decode work, and the loop's only exits are "enough audio" and
    // "no more audio" -- so it cannot spin. There is nothing here that waits for a
    // consumer, which is the whole reason backpressure is the caller's decision.
    while (impl_->pendingFrames() < impl_->spec.chunkFrames && !impl_->drained) {
        if (const auto pumped = impl_->pump(); !pumped) {
            out.clear();
            return asUnexpected(pumped.error());
        }
    }

    const usize available = impl_->pendingFrames();
    if (available == 0) {
        // An audio stream that never produced a frame is not a success. Returning
        // EndOfStream here would be the silent zero this repo keeps being bitten
        // by: the caller checks neither the chunk nor the count, renders a
        // zero-length video, and reports it as a finished render. Measured on a WAV
        // carrying a valid header and no payload.
        if (impl_->framesDecoded == 0) {
            out.clear();
            return asUnexpected(impl_->fail(DecodeErrorKind::EmptyAudioStream,
                                            "the audio stream in " + impl_->sourcePath +
                                                    " decoded to zero frames")
                                        .error());
        }
        impl_->finish();
        return ChunkStatus::EndOfStream;
    }

    const usize take = std::min(available, static_cast<usize>(impl_->spec.chunkFrames));
    const usize first = impl_->pendingHead;
    const usize count = take * impl_->spec.channels;
    out.samples.assign(impl_->pending.begin() + static_cast<isize>(first),
                       impl_->pending.begin() + static_cast<isize>(first + count));
    out.channels = impl_->spec.channels;
    out.sampleRate = impl_->spec.sampleRate;

    impl_->pendingHead += count;
    if (impl_->pendingHead == impl_->pending.size()) {
        impl_->pending.clear();
        impl_->pendingHead = 0;
    }
    return ChunkStatus::Ready;
}

std::expected<QueuePush, DecodeError> AudioFileDecoder::pushChunkTo(AudioQueue& queue) {
    PcmChunk chunk;
    const auto next = nextChunk(chunk);
    if (!next) return asUnexpected(next.error());
    if (*next == ChunkStatus::EndOfStream) {
        return QueuePush{.frames = 0,
                         .framesDroppedByVisualizer = 0,
                         .framesDroppedByRecorder = 0,
                         .acceptedByBothQueues = false};
    }

    const u32 frames = chunk.frameCount();
    const u64 vizDropsBefore = queue.vizDropCount();
    const u64 recDropsBefore = queue.recDropCount();
    const bool accepted =
            queue.pushAll(chunk.samples.data(), frames, chunk.channels, chunk.sampleRate);

    // Read the queue's own counters rather than inferring from the bool. pushAll
    // pushes the same chunk to viz and rec independently and stops at the first
    // refused 8-sample AudioFrame on each (AudioQueue.hpp:199-227), so the two
    // counters can disagree, and a bool cannot tell a viz-only stall from a
    // rec-only one. Exact because those counters are what pushAll itself
    // increments.
    const u64 vizDropped = queue.vizDropCount() - vizDropsBefore;
    const u64 recDropped = queue.recDropCount() - recDropsBefore;
    return QueuePush{
            .frames = frames,
            .framesDroppedByVisualizer = static_cast<u32>(std::min<u64>(vizDropped, frames)),
            .framesDroppedByRecorder = static_cast<u32>(std::min<u64>(recDropped, frames)),
            .acceptedByBothQueues = accepted,
    };
}

const AudioFileDecoder::OutputSpec& AudioFileDecoder::spec() const noexcept { return impl_->spec; }

u32 AudioFileDecoder::sampleRate() const noexcept { return impl_->spec.sampleRate; }

u32 AudioFileDecoder::channels() const noexcept { return impl_->spec.channels; }

u32 AudioFileDecoder::chunkFrames() const noexcept { return impl_->spec.chunkFrames; }

u64 AudioFileDecoder::framesDecoded() const noexcept { return impl_->framesDecoded; }

f64 AudioFileDecoder::positionSeconds() const noexcept {
    return secondsFor(impl_->framesDecoded, impl_->spec.sampleRate);
}

f64 AudioFileDecoder::declaredDurationSeconds() const noexcept {
    return impl_->declaredDurationSeconds;
}

f64 AudioFileDecoder::containerDurationSeconds() const noexcept {
    return impl_->containerDurationSeconds;
}

int AudioFileDecoder::demuxerStopCode() const noexcept { return impl_->demuxerStopCode; }

std::string_view AudioFileDecoder::codecName() const noexcept { return impl_->codecName; }

const std::string& AudioFileDecoder::sourcePath() const noexcept { return impl_->sourcePath; }

DecodeOutcome AudioFileDecoder::outcome() const noexcept { return impl_->outcome; }

const DecodeError& AudioFileDecoder::lastError() const noexcept { return impl_->lastError; }

} // namespace vc
