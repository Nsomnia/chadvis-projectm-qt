#include <QtTest>
#include <QTemporaryDir>

#include "audio/AudioQueue.hpp"
#include "core/Config.hpp"
#include "lyrics/LyricsData.hpp"
#include "recorder/EncoderSettings.hpp"
#include "recorder/FFmpegUtils.hpp"
#include "recorder/FrameGrabber.hpp"
#include "recorder/SubtitleBurnIn.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "recorder/VideoRecorderFFmpeg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace vc;

namespace {

EncoderSettings testSettings(const QString& outputPath,
                              VideoCodec videoCodec = VideoCodec::H264,
                              AudioCodec audioCodec = AudioCodec::AAC,
                              Container container = Container::MP4) {
    EncoderSettings settings;
    settings.outputPath = fs::path(outputPath.toStdString());
    settings.video.codec = videoCodec;
    settings.video.width = 32;
    settings.video.height = 32;
    settings.video.fps = 30;
    settings.video.crf = 0;
    settings.video.preset = EncoderPreset::Ultrafast;
    settings.audio.codec = audioCodec;
    settings.audio.bitrate = 64;
    settings.audio.sampleRate = 48000;
    settings.audio.channels = 2;
    settings.container = container;
    return settings;
}

bool pushAudio(AudioQueue& queue) {
    std::array<float, 4096 * 2> samples{};
    samples.fill(0.1f);
    return queue.pushAll(samples.data(), 4096, 2, 48000);
}

// A minimal but genuine 8 kHz mono 8-bit PCM WAV, so the reader tests open a
// real demuxer rather than a hand-poked context. 4 KiB of payload keeps the
// format probe and find_stream_info both confident.
bool writeTestWav(const fs::path& path) {
    const std::vector<u8> data(4096, 0x40);

    const auto put32 = [](std::ostream& os, u32 value) {
        const char bytes[4] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu),
                               static_cast<char>((value >> 16) & 0xFFu),
                               static_cast<char>((value >> 24) & 0xFFu)};
        os.write(bytes, 4);
    };
    const auto put16 = [](std::ostream& os, u16 value) {
        const char bytes[2] = {static_cast<char>(value & 0xFFu),
                               static_cast<char>((value >> 8) & 0xFFu)};
        os.write(bytes, 2);
    };

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out.write("RIFF", 4);
    put32(out, 36u + static_cast<u32>(data.size()));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    put32(out, 16u);            // PCM fmt chunk size
    put16(out, 1u);             // format = PCM
    put16(out, 1u);             // channels
    put32(out, 8000u);          // sample rate
    put32(out, 8000u);          // byte rate = rate * channels * bits / 8
    put16(out, 1u);             // block align
    put16(out, 8u);             // bits per sample
    out.write("data", 4);
    put32(out, static_cast<u32>(data.size()));
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

// ── Presentation-timeline fixtures ──────────────────────────────────────────
//
// The timeline tests drive VideoRecorderFFmpeg directly instead of going
// through VideoRecorder's worker thread, for two load-bearing reasons. A worker
// pops one frame per loop iteration, so a *deliberately* dropped frame could
// not be positioned deterministically. And the drop being modelled is the one
// FrameGrabber performs before the encoder ever sees the frame -- which is
// reproduced exactly by simply not calling encodeVideo for it, with no other
// variable in play.

constexpr u32 kTimelineFps = 30;
constexpr f64 kTick = 1.0 / kTimelineFps;
constexpr i64 kIntervalUs = 1000000 / kTimelineFps; // 33333 us
// A steady_clock reading on a machine that has been up four hours. The point of
// a large origin is that an un-normalised PTS cannot be mistaken for a
// normalised one: this would become a start time of 14400 s.
constexpr i64 kOriginUs = 4LL * 60 * 60 * 1000000;

// Quarter-tick tolerance. The only rounding in play is the nearest-integer
// rescale of a capture time onto the frame grid, which is wrong by at most half
// a tick per frame; the 1e6/30 interval truncation adds 0.33 us per frame,
// about 1e-5 of a tick, and does not accumulate into anything observable here.
constexpr f64 kTickTolerance = 0.25 * kTick;

/// One 32x32 RGBA frame at a plausible capture time, for the encoder-level
/// tests that need real packets. `originOffsetUs` moves it forward so a second
/// frame does not repeat a timestamp; presentationTimestampFor floors the
/// sequence either way, so nothing depends on the exact values.
GrabbedFrame solidFrame(const u8 fill, const i64 originOffsetUs = 0) {
    GrabbedFrame frame;
    frame.width = 32;
    frame.height = 32;
    frame.timestamp = kOriginUs + originOffsetUs;
    frame.data = std::vector<u8>(32 * 32 * 4, fill);
    return frame;
}

EncoderSettings timelineSettings(const QString& outputPath) {
    EncoderSettings settings = testSettings(outputPath);
    // B-frames off so the finished file carries no edit list and no
    // decode-order reshuffle to reason about. The code under test -- the PTS
    // assignment in encodeVideo -- takes the identical path either way; this
    // only makes the read-back observation unambiguous.
    settings.video.bFrames = 0;
    return settings;
}

// Encode one frame per entry in captureUs, in order. Returns the path actually
// written, which init may have renamed to dodge an existing file.
bool encodeAtCaptureTimes(const fs::path& requested,
                          const std::vector<i64>& captureUs,
                          fs::path& actualOut) {
    VideoRecorderFFmpeg encoder;
    const auto settings =
        timelineSettings(QString::fromStdString(requested.string()));
    if (auto started = encoder.init(settings); !started) return false;
    actualOut = fs::path(encoder.getOutputPath());

    u64 bytesWritten = 0;
    for (usize i = 0; i < captureUs.size(); ++i) {
        GrabbedFrame frame;
        frame.width = 32;
        frame.height = 32;
        frame.timestamp = captureUs[i];
        frame.data = std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 11));
        if (!encoder.encodeVideo(frame, bytesWritten)) return false;
    }
    encoder.flush(bytesWritten);
    encoder.cleanup();
    return true;
}

struct EncodedTimeline {
    // Presentation times in seconds, ascending. This is the observation the
    // whole change rests on: a seam would only prove the arithmetic, not the
    // file a player actually gets.
    std::vector<f64> presentationSeconds;
    // The same times as read, before sorting -- lets a test assert the muxer's
    // own decode ordering was monotonic too.
    std::vector<f64> readOrderSeconds;
    // Sum of the samples' own durations, i.e. the container's video duration.
    f64 durationSeconds{0.0};
    f64 timeBaseSeconds{0.0};
};

bool readVideoTimeline(const fs::path& path, EncodedTimeline& out) {
    const std::string pathStr = path.string();
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, pathStr.c_str(), nullptr, nullptr) < 0)
        return false;

    AVFormatContextInPtr format(raw);
    if (!format) return false;
    if (avformat_find_stream_info(format.get(), nullptr) < 0) return false;

    const int index = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1,
                                          -1, nullptr, 0);
    if (index < 0) return false;
    AVStream* stream = format->streams[index];
    // Spelled out rather than av_q2d, which has moved headers between FFmpeg
    // releases; this is the same division.
    out.timeBaseSeconds = static_cast<f64>(stream->time_base.num) /
                          static_cast<f64>(stream->time_base.den);

    AVPacketPtr packet(av_packet_alloc());
    if (!packet) return false;

    while (av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == index &&
            packet->pts != AV_NOPTS_VALUE) {
            const f64 seconds = packet->pts * out.timeBaseSeconds;
            out.presentationSeconds.push_back(seconds);
            out.readOrderSeconds.push_back(seconds);
            if (packet->duration > 0)
                out.durationSeconds += packet->duration * out.timeBaseSeconds;
        }
        av_packet_unref(packet.get());
    }

    std::sort(out.presentationSeconds.begin(), out.presentationSeconds.end());
    return !out.presentationSeconds.empty();
}

// The presentation time of each frame expressed in frame ticks, measured from
// the file's own first frame. Anchoring on front() rather than on absolute zero
// keeps the step pattern immune to any constant the container may apply to
// every timestamp; whether that constant is zero is a separate claim, asserted
// by uniformCaptureCadenceProducesOneTickStepsAndAZeroOrigin.
std::vector<i64> frameTickIndices(const EncodedTimeline& timeline) {
    const f64 origin = timeline.presentationSeconds.front();
    std::vector<i64> ticks;
    ticks.reserve(timeline.presentationSeconds.size());
    for (f64 seconds : timeline.presentationSeconds)
        ticks.push_back(static_cast<i64>(std::llround((seconds - origin) / kTick)));
    return ticks;
}

std::vector<i64> captureTimesEveryTick(i64 count, i64 skipIndex = -1) {
    std::vector<i64> times;
    for (i64 i = 0; i < count; ++i) {
        if (i == skipIndex) continue; // the frame that was dropped
        times.push_back(kOriginUs + i * kIntervalUs);
    }
    return times;
}

// ── Karaoke subtitle track ──────────────────────────────────────────────────
//
// What the muxer tests read, and how. Everything here goes through
// avformat_open_input on the *finished file* -- the same three streams, the same
// extradata, the same packet payloads a player would see. Nothing asserts on the
// recorder's own state, because the recorder's state is the thing under test.
//
// The container finding these tests are pinned to, and it was measured rather
// than assumed, against this project's own FFmpeg 9.0.1 (libavcodec 63.1.101):
//
//   MP4/MOV  no.  `ffmpeg -i ... -c:s ass out.mp4` fails with "Could not find
//            tag for codec ass in stream #2, codec not currently supported in
//            container", then "Could not write header ... Invalid argument",
//            exit 234. The container's only text codec is mov_text, 3GPP timed
//            text, which has no override tag syntax at all -- a \kf karaoke cue
//            has nowhere to live. So the muxer must *not* declare the stream.
//   WebM     no.  "Only VP8 or VP9 or AV1 video and Vorbis or Opus audio and
//            WebVTT subtitles are supported for WebM", exit 234.
//   AVI      no.  "Not yet implemented in FFmpeg, patches welcome", exit 176.
//   MKV      yes. libavformat/matroska.c's ff_mkv_codec_tags maps S_TEXT/ASS,
//            S_TEXT/SSA, S_ASS and S_SSA to AV_CODEC_ID_ASS. A one-line .ass
//            muxed in came back out byte-identical through
//            `ffmpeg -c:s copy`, and ffprobe reported the cue as a 53-byte
//            packet at pts 0 with a 2 s duration.
//
// Nothing in libavcodec links libass: the `ass` encoder is a passthrough
// (`ass_encode_frame` is one av_strlcpy) gated on CONFIG_ASS_ENCODER, and
// `otool -L libavcodec.63.dylib` lists no libass. --enable-libass on the CLI
// gates the burn-in filters, not this. So the dependency set is unchanged.

/// One cue of read-back subtitle content, as a player would see it.
struct EncodedSubtitle {
    bool present{false};
    AVCodecID codecId{AV_CODEC_ID_NONE};
    int disposition{0};
    std::string header;                          // the stream's extradata
    /// (pts, duration) per cue, both in **milliseconds**.
    ///
    /// The second element is a length, not an end time. That distinction is the
    /// one an assertion most easily gets wrong, and getting it wrong here hid the
    /// writeSubtitlePacket bug: the expectation was written as a start-end pair
    /// read off the fixture (2.5-3.5 s) while the field holds a duration, so the
    /// numbers being compared were never the numbers the muxer produced.
    std::vector<std::pair<std::int64_t, std::int64_t>> timingsMs;
    std::vector<std::string> payloads;
};

struct EncodedStreams {
    std::vector<AVMediaType> types; // in stream-index order
    EncodedSubtitle subtitle;
    int videoStreamCount{0};
    int audioStreamCount{0};
};

/// Read a finished file's stream table, and its subtitle content if it has one.
///
/// Two things this must not do, both of which it originally did. Read them
/// before changing anything else here, because the second one is a property of
/// libavformat rather than of this code:
///
///  1. **A non-subtitle stream must never be recorded as a subtitle.** An
///     `if/else if` chain that counts video and audio and then *falls through*
///     into the subtitle block is enough to do it, and the symptom is
///     indistinguishable from the real thing: every file, with or without a
///     subtitle track, comes back with `present == true`. The mapping is now one
///     `if` with an unconditional `continue`, so the only way into the block is
///     a stream whose own `codec_type` is AVMEDIA_TYPE_SUBTITLE.
///
///  2. **A demuxer is a forward-only cursor, so it cannot be drained once per
///     stream.** `av_read_frame` advances through the file; a per-stream loop
///     gives the first stream the entire file and leaves every later stream with
///     nothing. The real subtitle stream is the last stream added, so it is
///     always the one that reads zero, which looks exactly like "the muxer wrote
///     no packets". The stream table is therefore read from `format->streams`
///     (metadata, no I/O) and the file is walked exactly once for packets.
bool readEncodedStreams(const fs::path& path, EncodedStreams& out) {
    const std::string pathStr = path.string();
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, pathStr.c_str(), nullptr, nullptr) < 0)
        return false;

    AVFormatContextInPtr format(raw);
    if (!format) return false;
    if (avformat_find_stream_info(format.get(), nullptr) < 0) return false;

    int subtitleIndex = -1;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        AVStream* stream = format->streams[i];
        const AVMediaType type = stream->codecpar->codec_type;
        out.types.push_back(type);

        if (type == AVMEDIA_TYPE_VIDEO) {
            ++out.videoStreamCount;
            continue;
        }
        if (type == AVMEDIA_TYPE_AUDIO) {
            ++out.audioStreamCount;
            continue;
        }
        if (type != AVMEDIA_TYPE_SUBTITLE) {
            continue; // attachments, data: not interesting here
        }
        if (subtitleIndex >= 0) {
            continue; // a second subtitle track; the assertions name one
        }

        subtitleIndex = static_cast<int>(i);
        out.subtitle.present = true;
        out.subtitle.codecId = stream->codecpar->codec_id;
        out.subtitle.disposition = stream->disposition;
        if (stream->codecpar->extradata_size > 0) {
            out.subtitle.header.assign(
                reinterpret_cast<const char*>(stream->codecpar->extradata),
                static_cast<std::size_t>(stream->codecpar->extradata_size));
        }
    }

    if (subtitleIndex < 0) {
        return true; // no subtitle track: there are no packets to look for
    }

    // Spelled out rather than av_q2d, which has moved headers between FFmpeg
    // releases; this is the same division, and readVideoTimeline above says so
    // for the same reason.
    AVStream* subtitleStream = format->streams[subtitleIndex];
    const AVRational timeBase = subtitleStream->time_base;

    AVPacketPtr packet(av_packet_alloc());
    if (!packet) return false;
    while (av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == subtitleIndex && packet->size > 0) {
            out.subtitle.payloads.emplace_back(
                reinterpret_cast<const char*>(packet->data),
                static_cast<std::size_t>(packet->size));
            if (packet->pts != AV_NOPTS_VALUE && timeBase.den > 0) {
                // Milliseconds, to match the field's name and the expectations.
                //
                // This was seconds: `pts * (num/den)` is the seconds conversion,
                // and the field was called `timingsMs` and asserted against
                // millisecond values. That mismatch hid the production bug in
                // writeSubtitlePacket entirely -- with the reader scaling down
                // and the writer scaling down by the same wrong factor, the two
                // errors cancelled and the assertion saw a plausible small
                // number instead of a 100x error. Rescaled from the container's
                // own time base rather than assuming 1/1000, so it stays right
                // whatever the muxer picks.
                //
                // One integer expression, so there is a single truncation rather
                // than a seconds value losing its remainder and then being
                // multiplied by 1000.
                const auto toMilliseconds = [&](const std::int64_t ticks) {
                    return ticks * timeBase.num * 1000 / timeBase.den;
                };
                out.subtitle.timingsMs.emplace_back(
                    toMilliseconds(packet->pts),
                    toMilliseconds(packet->duration));
            }
        }
        av_packet_unref(packet.get());
    }

    return true;
}

/// "video/ffv1, audio/aac" -- the stream table, for a failure message.
///
/// Added because `'!present' returned FALSE` on its own is a *misleading*
/// diagnostic: it names the assertion, which reads as "the muxer added a track",
/// when the reader can produce the same symptom for a file that has no subtitle
/// track at all. When the assertion fires, the stream table is what distinguishes
/// those, and it is already in hand.
QString streamTypeSummary(const EncodedStreams& streams) {
    QStringList summary;
    for (const AVMediaType type : streams.types) {
        summary << QString::fromLatin1(
            av_get_media_type_string(type) ? av_get_media_type_string(type) : "?");
    }
    return QStringLiteral("streams in the file: [%1]").arg(summary.join(", "));
}

/// The whole ASS script as a reader would reconstruct it: the CodecPrivate
/// followed by each cue, which is the only ordering that is a valid document.
std::string reassembledScript(const EncodedSubtitle& subtitle) {
    std::string script = subtitle.header;
    for (const auto& payload : subtitle.payloads) {
        script += payload;
        script += '\n';
    }
    return script;
}

/// Lyrics with word-level timing, so every cue carries \kf karaoke tags.
LyricsData karaokeLyrics() {
    LyricsData data;
    data.isSynced = true;
    data.title = "Test";

    const char* words[] = {"ka", "ra", "o", "ke"};
    const f32 starts[] = {0.0f, 0.5f, 1.0f, 1.5f};
    const f32 ends[] = {0.5f, 1.0f, 1.5f, 2.0f};

    LyricsLine first;
    first.text = "karaoke";
    first.startTime = 0.0f;
    first.endTime = 2.0f;
    first.isSynced = true;
    for (usize i = 0; i < 4; ++i) {
        LyricsWord word;
        word.text = words[i];
        word.startTime = starts[i];
        word.endTime = ends[i];
        first.words.push_back(word);
    }
    data.lines.push_back(first);

    LyricsLine second;
    second.text = "second line";
    second.startTime = 2.5f;
    second.endTime = 3.5f;
    second.isSynced = true;
    data.lines.push_back(second);

    return data;
}

/// Encode a short clip through VideoRecorderFFmpeg directly, with an optional
/// ASS document. Returns the path actually written (init may have suffixed it).
bool encodeWithSubtitle(const fs::path& requested,
                        const std::string& assDocument,
                        Container container,
                        VideoCodec videoCodec,
                        AudioCodec audioCodec,
                        fs::path& actualOut) {
    VideoRecorderFFmpeg encoder;
    auto settings = testSettings(QString::fromStdString(requested.string()),
                                 videoCodec, audioCodec, container);
    // B-frames off so the read-back frame count is exact, matching the
    // timeline fixtures. The subtitle path does not depend on it either way.
    settings.video.bFrames = 0;
    if (auto started = encoder.init(settings, assDocument); !started) return false;
    actualOut = fs::path(encoder.getOutputPath());

    u64 bytesWritten = 0;
    for (usize i = 0; i < 8; ++i) {
        GrabbedFrame frame;
        frame.width = 32;
        frame.height = 32;
        frame.timestamp = kOriginUs + static_cast<i64>(i) * kIntervalUs;
        frame.data = std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 13));
        if (!encoder.encodeVideo(frame, bytesWritten)) return false;
    }

    // Enough samples for at least one AAC frame (1024) in stereo, so the audio
    // stream carries data and the file is genuinely playable rather than a
    // header with an empty track.
    std::vector<f32> audio(4096 * 2, 0.05f);
    encoder.encodeAudio(audio, 2, bytesWritten);

    encoder.flush(bytesWritten);
    encoder.cleanup();
    return true;
}

// ── Burn-in (post-pass) ───────────────────────────────────────────────────
//
// A test that only checks the output file exists proves nothing: a burn-in that
// silently copied its input would pass it. So the assertion is on the *pixels* --
// decoded back out of the finished file and compared against the source clip.
//
// The recording used throughout is ffv1 in Matroska, which is lossless, so the
// re-encode is bit-exact everywhere the filter did not touch. That is what makes
// the comparison decisive rather than approximate: a frame with no cue on it must
// come back *identical*, and a frame with a cue on it must differ, and the
// difference must be in the bottom band where the writer puts its text.

/// One decoded frame as a flat vector of 8-bit luma samples, plus its dimensions.
/// Luma only: text is a luma *and* chroma difference, and comparing luma is both
/// sufficient and immune to chroma subsampling noise at the small sizes used here.
struct LumaFrame {
    int width{0};
    int height{0};
    std::vector<u8> samples;
};

/// Decode frame `index` of `path` to luma. False if the file will not open or the
/// frame index is out of range.
bool readLumaFrame(const fs::path& path, int index, LumaFrame& out) {
    const std::string pathStr = path.string();
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, pathStr.c_str(), nullptr, nullptr) < 0)
        return false;
    AVFormatContextInPtr format(raw);
    if (!format) return false;
    if (avformat_find_stream_info(format.get(), nullptr) < 0) return false;

    const int streamIndex =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) return false;
    AVStream* stream = format->streams[streamIndex];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) return false;

    AVCodecContextPtr ctx(avcodec_alloc_context3(decoder));
    if (!ctx) return false;
    if (avcodec_parameters_to_context(ctx.get(), stream->codecpar) < 0) return false;
    if (avcodec_open2(ctx.get(), decoder, nullptr) < 0) return false;

    AVPacketPtr packet(av_packet_alloc());
    AVFramePtr frame(av_frame_alloc());
    if (!packet || !frame) return false;

    int seen = 0;
    while (av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == streamIndex) {
            if (avcodec_send_packet(ctx.get(), packet.get()) >= 0) {
                while (true) {
                    const int ret = avcodec_receive_frame(ctx.get(), frame.get());
                    if (ret < 0) break;
                    if (seen == index) {
                        out.width = frame->width;
                        out.height = frame->height;
                        out.samples.resize(static_cast<usize>(
                            out.width * out.height));
                        const u8* luma = frame->data[0];
                        for (int y = 0; y < out.height; ++y) {
                            std::memcpy(out.samples.data() +
                                            static_cast<usize>(y * out.width),
                                        luma + static_cast<usize>(
                                                  y * frame->linesize[0]),
                                        static_cast<usize>(out.width));
                        }
                        return true;
                    }
                    ++seen;
                    av_frame_unref(frame.get());
                }
            }
        }
        av_packet_unref(packet.get());
    }
    return false;
}

/// How many luma samples differ, and how many of those are in the bottom eighth
/// of the frame.
struct FrameDiff {
    usize total{0};
    usize bottomBand{0};
};

FrameDiff diffFrames(const LumaFrame& a, const LumaFrame& b) {
    FrameDiff diff;
    if (a.width != b.width || a.height != b.height || a.samples.empty()) {
        return diff;
    }
    const int bandStart = a.height * 7 / 8;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const usize offset = static_cast<usize>(y * a.width + x);
            if (a.samples[offset] == b.samples[offset]) {
                continue;
            }
            ++diff.total;
            if (y >= bandStart) {
                ++diff.bottomBand;
            }
        }
    }
    return diff;
}

/// Bytes of a file, for the "input is unmodified" assertion.
std::vector<u8> readAllBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
}

/// Write `text` to `path` and return it. Used for the .ass input, which has to be
/// a real file on disk because that is what the filter is given.
bool writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << text;
    return file.good();
}

/// A clip long enough to contain a cue, a gap, and a second cue: 90 frames at
/// 30 fps is 3 s, and karaokeLyrics() cues at 0-2 s and 2.5-3.5 s, so frames
/// 0-59 are cued, 60-74 are bare, and 75-89 are cued again.
bool recordLongClip(const fs::path& path, fs::path& actualOut) {
    VideoRecorderFFmpeg encoder;
    auto settings = testSettings(QString::fromStdString(path.string()),
                                 VideoCodec::FFV1, AudioCodec::AAC,
                                 Container::MKV);
    // Lossless video and no audio, so the only thing that can change a pixel
    // between the input and the output of the post-pass is the filter itself.
    settings.audio.codec = AudioCodec::FLAC;
    if (auto started = encoder.init(settings); !started) return false;
    actualOut = fs::path(encoder.getOutputPath());

    u64 bytesWritten = 0;
    for (u64 i = 0; i < 90; ++i) {
        GrabbedFrame frame;
        frame.width = 320;
        frame.height = 240;
        frame.timestamp = kOriginUs + static_cast<i64>(i) * kIntervalUs;
        // A flat mid-grey frame, so any luma change is drawn text and nothing
        // else. Flat matters: a textured background would make "the frame
        // changed" true for reasons that have nothing to do with the subtitle.
        frame.data.assign(320 * 240 * 4, 0xC0);
        if (!encoder.encodeVideo(frame, bytesWritten)) return false;
    }
    encoder.flush(bytesWritten);
    encoder.cleanup();
    return true;
}

/// QTRY_VERIFY expands to a QVERIFY, which returns from the enclosing function
/// on failure, so it needs a void one. Split out for that reason alone.
void waitForFramesEncoded(VideoRecorder& recorder, u64 frameCount) {
    QTRY_VERIFY_WITH_TIMEOUT(recorder.getCurrentStats().framesWritten >= frameCount, 5000);
}

/// Drive the whole VideoRecorder path -- start on this thread, frames and audio
/// through the encoding worker, then stop -- and return the file it produced.
///
/// `forwardSubtitle` false takes the one-argument overload, which is what every
/// pre-existing call site compiles to, so the two tests using this exercise two
/// different call paths rather than the same one twice.
///
/// No QVERIFY here: it returns void from the enclosing function on failure, so
/// it cannot appear in a bool helper. Assertions belong in the test.
bool recordThroughVideoRecorder(const EncoderSettings& settings,
                                const std::string& subtitle,
                                bool forwardSubtitle,
                                fs::path& actualOut) {
    VideoRecorder recorder;
    AudioQueue queue;
    // Attached before start(), so the worker is created with it already bound.
    recorder.setAudioQueue(&queue);

    const auto started = forwardSubtitle ? recorder.start(settings, subtitle)
                                         : recorder.start(settings);
    if (!started) return false;
    // Read before stopping: after stop() the worker is released and the
    // recorder falls back to its own snapshot, which carries the same path.
    // VideoRecorderThread::start assigned it before VideoRecorder::start
    // returned, and getStats() takes the mutex, so this is a settled read.
    actualOut = fs::path(recorder.getCurrentStats().currentFile);
    if (!pushAudio(queue)) return false;

    // u64, because that is what RecordingStats::framesWritten is; an i64 here
    // compiles but is a sign-compare inside the QTRY macro.
    constexpr u64 frameCount = 8;
    for (u64 i = 0; i < frameCount; ++i) {
        recorder.submitVideoFrame(
            std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 17)), 32, 32,
            kOriginUs + static_cast<i64>(i) * kIntervalUs);
    }

    // Not optional. stop() sets the token and joins, and threadLoop's guard
    // `!stopToken.stop_requested()` is checked on entry -- so a thread body that
    // had not been scheduled yet exits without draining, and the file would come
    // out empty. Waiting for the counter is what makes this deterministic;
    // frameSubmissionsReachEncoder does the same for the same reason.
    waitForFramesEncoded(recorder, frameCount);

    (void)recorder.stop();
    return true;
}

} // namespace

class TestRecorder : public QObject {
    Q_OBJECT

private slots:
    // glReadPixels hands rows over bottom-up and the encoder uploads them in
    // the order they arrive, so this row reversal is the whole reason an
    // encoded frame is not upside down. Pinned here because it is a one-line
    // routine that is trivially "optimized" into nothing.
    void flipImageReversesRows() {
        constexpr u32 width = 3;
        constexpr u32 height = 4;
        std::vector<u8> pixels(static_cast<usize>(width) * height * 4);
        for (u32 y = 0; y < height; ++y) {
            for (u32 x = 0; x < width; ++x) {
                pixels[(y * width + x) * 4] = static_cast<u8>(y + 1);
            }
        }

        FrameGrabber::flipImage(pixels, width, height);

        for (u32 y = 0; y < height; ++y) {
            for (u32 x = 0; x < width; ++x) {
                QCOMPARE(pixels[(y * width + x) * 4],
                         static_cast<u8>(height - y));
            }
        }
    }

    void flipImageLeavesUndersizedBuffersAlone() {
        // A short buffer would read past its end; the guard is what keeps a
        // malformed frame from turning into memory corruption.
        std::vector<u8> pixels(8, 7);
        FrameGrabber::flipImage(pixels, 64, 64);
        QCOMPARE(pixels.size(), static_cast<usize>(8));
        QCOMPARE(pixels.front(), static_cast<u8>(7));
        QCOMPARE(pixels.back(), static_cast<u8>(7));
    }

    void flipImageIsItsOwnInverse() {
        constexpr u32 width = 5;
        constexpr u32 height = 7;
        std::vector<u8> original(static_cast<usize>(width) * height * 4);
        for (usize i = 0; i < original.size(); ++i)
            original[i] = static_cast<u8>((i * 31) % 251);

        std::vector<u8> pixels = original;
        FrameGrabber::flipImage(pixels, width, height);
        QVERIFY(pixels != original);
        FrameGrabber::flipImage(pixels, width, height);
        QCOMPARE(pixels, original);
    }

    void frameSubmissionsReachEncoder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto outputPath = QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "frames.mp4").string());
        VideoRecorder recorder;
        auto started = recorder.start(testSettings(outputPath));
        QVERIFY(started);

        constexpr u32 frameCount = 8;
        for (u32 i = 0; i < frameCount; ++i) {
            std::vector<u8> frame(32 * 32 * 4, static_cast<u8>(i * 17));
            recorder.submitVideoFrame(std::move(frame), 32, 32,
                                      static_cast<i64>(i) * 1000);
        }

        QTRY_VERIFY_WITH_TIMEOUT(
            recorder.getCurrentStats().framesWritten >= frameCount, 5000);
        const auto progress = recorder.getCurrentStats();
        QVERIFY(progress.framesWritten >= frameCount);
        (void)recorder.stop();
    }

    // ── Presentation timeline ───────────────────────────────────────────────
    // The encoder used to assign pts = videoFrameCount_++, so every frame it
    // received got the next tick regardless of when that frame was actually
    // captured. A frame dropped by FrameGrabber therefore cost the file one
    // frame interval of real time silently: the surviving frames were
    // renumbered contiguously and the segment played faster than the song.
    // These tests read the finished file back and assert on the presentation
    // times that came out of it.

    void uniformCaptureCadenceProducesOneTickStepsAndAZeroOrigin() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr i64 frameCount = 12;
        const fs::path outDir(dir.path().toStdString());
        fs::path actual;
        QVERIFY(encodeAtCaptureTimes(outDir / "uniform.mp4",
                                     captureTimesEveryTick(frameCount), actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(frameCount));

        // A constant capture interval gives a constant PTS step.
        for (usize i = 1; i < timeline.presentationSeconds.size(); ++i) {
            const f64 step = timeline.presentationSeconds[i] -
                             timeline.presentationSeconds[i - 1];
            QVERIFY2(std::abs(step - kTick) < kTickTolerance,
                     qPrintable(QStringLiteral("step %1 s at frame %2")
                                    .arg(step)
                                    .arg(i)));
        }

        // The origin is normalised. The capture clock said four hours uptime;
        // a PTS derived from it without an origin would put the first frame
        // there, and the file's duration and seek index with it.
        QVERIFY2(std::abs(timeline.presentationSeconds.front()) <
                     kTickTolerance,
                 qPrintable(QStringLiteral("first presentation time is %1 s")
                                .arg(timeline.presentationSeconds.front())));

        // And the wall time the file spans is the wall time that was captured:
        // eleven intervals across for twelve frames.
        const f64 span = timeline.presentationSeconds.back() -
                         timeline.presentationSeconds.front();
        QVERIFY2(std::abs(span - (frameCount - 1) * kTick) < 0.5 * kTick,
                 qPrintable(QStringLiteral("span is %1 s").arg(span)));
    }

    void aDroppedCaptureFrameLeavesAGapInTheEncodedTimeline() {
        // The regression guard for this change. Frame 4 of 12 never reaches
        // encodeVideo -- exactly what FrameGrabber does when the queue is full
        // -- so the file must contain the other eleven frames spread over
        // *twelve* frame intervals, with the missing tick at index 4.
        //
        // Under the old counter this file contained eleven frames on ticks
        // 0..10: no gap, eleven intervals of span, and the segment played
        // faster than the audio it was recorded against.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr i64 frameCount = 12;
        constexpr i64 droppedIndex = 4;
        const fs::path outDir(dir.path().toStdString());
        fs::path actual;
        QVERIFY(encodeAtCaptureTimes(outDir / "gap.mp4",
                                     captureTimesEveryTick(frameCount, droppedIndex),
                                     actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(frameCount - 1));

        // Exact tick sequence: {0..11} minus the dropped frame's tick. This is
        // the whole claim -- the gap is in the right place, not just present.
        const auto ticks = frameTickIndices(timeline);
        QCOMPARE(ticks.size(), static_cast<usize>(frameCount - 1));
        i64 expected = 0;
        for (i64 i = 0; i < frameCount; ++i) {
            if (i == droppedIndex) continue;
            QVERIFY2(ticks[static_cast<usize>(expected)] == i,
                     qPrintable(QStringLiteral("frame index %1 landed on tick %2")
                                    .arg(i)
                                    .arg(ticks[static_cast<usize>(expected)])));
            ++expected;
        }

        // The step across the gap is two ticks; every other step is one. So
        // the file is one frame interval longer than its frame count, which is
        // the dropped frame's time accounted for rather than compressed away.
        const auto gapIndex = static_cast<usize>(droppedIndex);
        const f64 gapStep =
            timeline.presentationSeconds[gapIndex] -
            timeline.presentationSeconds[gapIndex - 1];
        QVERIFY2(std::abs(gapStep - 2 * kTick) < kTickTolerance,
                 qPrintable(QStringLiteral("step across the gap is %1 s")
                                .arg(gapStep)));

        const f64 span = timeline.presentationSeconds.back() -
                         timeline.presentationSeconds.front();
        QVERIFY2(std::abs(span - (frameCount - 1) * kTick) < 0.5 * kTick,
                 qPrintable(QStringLiteral("gap file spans %1 s").arg(span)));
    }

    void uniformCadenceLeavesTheFileDurationUnchanged() {
        // Deliberately a non-discrimination test. A counter and a capture-time
        // PTS agree exactly on a uniform input, so this cannot tell them apart
        // -- it exists to pin that the new arithmetic does not stretch or
        // compress an ordinary recording, which is the risk the gap test alone
        // would not catch.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        constexpr i64 frameCount = 20;
        const fs::path outDir(dir.path().toStdString());
        fs::path actual;
        QVERIFY(encodeAtCaptureTimes(outDir / "duration.mp4",
                                     captureTimesEveryTick(frameCount), actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(frameCount));

        // The container's own video duration, summed from the samples' stored
        // durations rather than from a packet count.
        QVERIFY2(std::abs(timeline.durationSeconds - frameCount * kTick) <
                     kTick,
                 qPrintable(QStringLiteral("duration is %1 s, expected %2 s")
                                .arg(timeline.durationSeconds)
                                .arg(frameCount * kTick)));
    }

    void nonIncreasingCaptureTimestampsStillGiveAMonotonicTimeline() {
        // A clock that repeats a tick, two instants inside the same frame
        // interval, and one that steps backwards. Every one of these rescales
        // to a tick at or before its predecessor, which a naive
        // "elapsed / interval" assignment would hand straight to libavcodec.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::vector<i64> capture = {
            kOriginUs,             // tick 0
            kOriginUs + 1000,      // 1 ms: rounds to tick 0 as well
            kOriginUs + 1000,      // identical reading
            kOriginUs,             // backwards to the origin
            kOriginUs + 2 * kIntervalUs, // tick 2, already behind the floor
            kOriginUs + 3 * kIntervalUs, // tick 3
        };

        fs::path actual;
        const fs::path outDir(dir.path().toStdString());
        QVERIFY(encodeAtCaptureTimes(outDir / "nonmonotonic.mp4", capture,
                                     actual));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));

        // Every submitted frame survived, so nothing was dropped or refused on
        // the way through the encoder and the muxer.
        QCOMPARE(timeline.presentationSeconds.size(), capture.size());

        // Strictly increasing in presentation order...
        for (usize i = 1; i < timeline.presentationSeconds.size(); ++i) {
            QVERIFY2(timeline.presentationSeconds[i] >
                         timeline.presentationSeconds[i - 1],
                     qPrintable(QStringLiteral("pts %1 at %2 s is not above %3 s")
                                    .arg(i)
                                    .arg(timeline.presentationSeconds[i])
                                    .arg(timeline.presentationSeconds[i - 1])));
        }
        // ...and in the order the muxer wrote them, so the stream is not
        // merely sorted after the fact. bFrames is 0 for these fixtures, so
        // decode order and presentation order coincide.
        QVERIFY(std::is_sorted(timeline.readOrderSeconds.begin(),
                               timeline.readOrderSeconds.end()));
    }

    void frameGrabberDropsTheOldestFrameAndKeepsRealCaptureTimes() {
        // The precondition the gap test assumes: the drop discards a frame but
        // never rewrites the survivors' capture times, so the frame after a
        // drop still knows how much real time has passed. Without this the gap
        // test would be testing a fiction.
        FrameGrabber grabber;
        grabber.start();

        constexpr i64 submitted = 64;
        for (i64 i = 0; i < submitted; ++i) {
            GrabbedFrame frame;
            frame.width = 1;
            frame.height = 1;
            frame.timestamp = kOriginUs + i * kIntervalUs;
            grabber.pushFrame(std::move(frame));
        }

        const u32 dropped = grabber.droppedFrames();
        QVERIFY2(dropped > 0, "queue never overflowed, so nothing was dropped");
        QVERIFY(dropped < static_cast<u32>(submitted));

        GrabbedFrame frame;
        for (u32 i = 0; i < submitted - dropped; ++i) {
            QVERIFY(grabber.getNextFrame(frame, 100));
            const i64 expected = kOriginUs +
                                 (static_cast<i64>(dropped) + i) * kIntervalUs;
            QCOMPARE(frame.timestamp, expected);
        }
        GrabbedFrame drained;
        QVERIFY(!grabber.getNextFrame(drained, 10));
    }

    void audioQueueBeforeWorkerIsConsumed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto outputPath = QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "audio-before.mkv").string());
        AudioQueue queue;
        VideoRecorder recorder;
        recorder.setAudioQueue(&queue); // attachment before worker creation
        auto started = recorder.start(testSettings(
            outputPath, VideoCodec::FFV1, AudioCodec::AAC, Container::MKV));
        QVERIFY(started);
        QVERIFY(pushAudio(queue));

        QTRY_VERIFY_WITH_TIMEOUT(queue.recDepth() == 0, 3000);
        (void)recorder.stop();
    }

    void audioQueueAfterWorkerIsConsumed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto outputPath = QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "audio-after.mkv").string());
        AudioQueue queue;
        VideoRecorder recorder;
        auto started = recorder.start(testSettings(
            outputPath, VideoCodec::FFV1, AudioCodec::AAC, Container::MKV));
        QVERIFY(started);
        recorder.setAudioQueue(&queue); // attachment after worker creation
        QVERIFY(pushAudio(queue));

        QTRY_VERIFY_WITH_TIMEOUT(queue.recDepth() == 0, 3000);
        (void)recorder.stop();
    }

    void configBuildsSanitizedContainerPath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto original = Config::instance().recording();
        auto& recording = Config::instance().recording();
        recording.outputDirectory = fs::path(dir.path().toStdString());
        recording.defaultFilename = "capture.v2/{date}_{time}.mp4";
        recording.container = "mkv";
        recording.video.codec = "libx265";
        recording.video.crf = 17;

        const auto settings = EncoderSettings::fromConfig();
        const auto selectedPath = EncoderSettings::outputPathForContainer(
            "capture.mp4", Container::MKV);
        const auto filename = settings.outputPath.filename().string();
        const bool hasPathSeparator =
            filename.find('/') != std::string::npos ||
            filename.find('\\') != std::string::npos;
        const bool basenamePreserved =
            filename.find("capture.v2_") != std::string::npos;
        const bool extensionMatches =
            settings.outputPath.extension() == ".mkv";
        const bool selectedExtensionMatches =
            selectedPath.extension() == ".mkv";
        const bool codecWasApplied =
            settings.video.codec == VideoCodec::H265;
        const bool crfWasApplied = settings.video.crf == 17;

        Config::instance().recording() = original;
        QVERIFY(!settings.outputPath.empty());
        QVERIFY(!hasPathSeparator);
        QVERIFY(basenamePreserved);
        QVERIFY(extensionMatches);
        QVERIFY(selectedExtensionMatches);
        QVERIFY(codecWasApplied);
        QVERIFY(crfWasApplied);
    }

    // ── Karaoke subtitle track ──────────────────────────────────────────────
    // See the block comment above readEncodedStreams for the container finding
    // these pin. The shape being defended throughout: a subtitle is a
    // *nice-to-have* on a recording, and no one of these tests would be worth a
    // broken video file.

    void mkvRecordingCarriesTheKaraokeTrack() {
        // The one container of the five EncoderSettings offers that can carry
        // ASS, so this is the case the feature exists for.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::string document = LyricsExport::toAssDocument(karaokeLyrics());
        QVERIFY(document.find("{\\kf") != std::string::npos);

        fs::path actual;
        QVERIFY(encodeWithSubtitle(fs::path(dir.path().toStdString()) / "karaoke.mkv",
                                   document, Container::MKV, VideoCodec::FFV1,
                                   AudioCodec::AAC, actual));

        EncodedStreams streams;
        QVERIFY(readEncodedStreams(actual, streams));

        // Three streams, in the declared order: the subtitle was appended after
        // the two existing ones and did not reorder them.
        QCOMPARE(streams.types.size(), static_cast<std::size_t>(3));
        QCOMPARE(streams.videoStreamCount, 1);
        QCOMPARE(streams.audioStreamCount, 1);
        QVERIFY(streams.subtitle.present);
        QCOMPARE(streams.types[2], AVMEDIA_TYPE_SUBTITLE);
        QCOMPARE(streams.subtitle.codecId, AV_CODEC_ID_ASS);

        // On by default, or a player shows a track nobody turns on. Matroska
        // infers FlagDefault from the codec type and would leave a subtitle with
        // no disposition switched off.
        QVERIFY(streams.subtitle.disposition & AV_DISPOSITION_DEFAULT);

        // Both cues made it, with the exact timestamps the writer emitted. The
        // fixture's first line is 0.0-2.0 s and its second is 2.5-3.5 s, so the
        // expected (pts, duration) pairs in milliseconds are (0, 2000) and
        // (2500, 1000).
        //
        // The exact millisecond values are the whole point of these four
        // assertions, and they are what caught a real 100x error in
        // writeSubtitlePacket: the centisecond-to-millisecond conversion divided
        // by ten where it had to multiply, so a two-second cue was written as
        // 20 ms. Asserting only "the duration is positive", or "about two
        // seconds", would have passed on every version of that bug. Note the
        // second element is a duration, not the 3.5 s end time.
        //
        // The second line has no word timings, so its cue is an untagged caption
        // -- still a cue, and still on the timeline.
        QCOMPARE(streams.subtitle.payloads.size(), static_cast<std::size_t>(2));
        QCOMPARE(streams.subtitle.timingsMs.size(), static_cast<std::size_t>(2));
        QCOMPARE(streams.subtitle.timingsMs[0].first, std::int64_t{0});
        QCOMPARE(streams.subtitle.timingsMs[0].second, std::int64_t{2000});
        QCOMPARE(streams.subtitle.timingsMs[1].first, std::int64_t{2500});
        QCOMPARE(streams.subtitle.timingsMs[1].second, std::int64_t{1000});

        // The content, which is the actual claim. The script is the CodecPrivate
        // and each cue is a packet, so the two have to be joined in that order
        // to be a document at all -- and the result has to be the document the
        // writer produced.
        const std::string script = reassembledScript(streams.subtitle);
        QVERIFY2(script.find("[Script Info]") != std::string::npos,
                 "the stream carries no script header");
        QVERIFY2(script.find("[Events]") != std::string::npos,
                 "the stream carries no Events section");
        // The exact cue text, with the separator spaces the writer puts between
        // words. 50 centiseconds per half-second word, four of them.
        //
        // The spaces are load-bearing and this expectation used to be wrong
        // about them. LyricsExport::assEventText joins word tokens with a single
        // space after each \kf tag, and that is what separates one word's tag
        // from the next: "{\kf50}ka{\kf50}ra" is a tag immediately followed by
        // the next tag with no glyph between them, where a renderer would draw
        // the words run together. tests/unit/lyrics/test_LyricsExport.cpp:330
        // pins the same thing as a golden byte -- "{\kf50}one {\kf40}two
        // {\kf50}three" -- and that suite passes, so the spaced form is the
        // writer's contract and the unspaced form here was copied from a
        // hand-typed probe file rather than from the writer.
        QVERIFY2(script.find("{\\kf50}ka {\\kf50}ra {\\kf50}o {\\kf50}ke") !=
                     std::string::npos,
                 qPrintable(QStringLiteral("reassembled script was:\n%1")
                                .arg(QString::fromStdString(script))));
        QVERIFY2(script.find("second line") != std::string::npos,
                 "the untagged line did not survive the mux");

        // And the payload shape is FFmpeg's, not merely parseable: readorder,
        // layer, then the Dialogue text-and-effects fields. A whole
        // `Dialogue: 0,...` line per packet would be a second, conflicting copy
        // of a script a renderer concatenates.
        QVERIFY2(streams.subtitle.payloads[0].rfind("0,0,Default,,0,0,0,,", 0) == 0,
                 qPrintable(QStringLiteral("cue payload was: %1")
                                .arg(QString::fromStdString(
                                    streams.subtitle.payloads[0]))));
    }

    void mp4RecordingDropsTheKaraokeTrackAndStillProducesAPlayableFile() {
        // The requirement, stated as a test: an MP4 must come out playable.
        //
        // MP4 has no ASS/SSA stream. libavformat's init_muxer looks the codec's
        // tag up, finds none, and fails avformat_write_header with EINVAL --
        // measured: "Could not find tag for codec ass in stream #2, codec not
        // currently supported in container", exit 234. So the correct behaviour
        // is to *not declare the stream*, and the observable consequence is a
        // normal two-stream MP4 plus a log line. Declaring it unconditionally
        // loses the recording.
        //
        // The alternative -- silently transcoding to mov_text -- is not
        // available: mov_text is 3GPP timed text with no override tag syntax, so
        // a \kf karaoke cue would be flattened to plain text and the feature
        // would appear to work while being exactly the thing it must not be.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::string document = LyricsExport::toAssDocument(karaokeLyrics());
        fs::path actual;
        QVERIFY(encodeWithSubtitle(fs::path(dir.path().toStdString()) / "karaoke.mp4",
                                   document, Container::MP4, VideoCodec::H264,
                                   AudioCodec::AAC, actual));

        EncodedStreams streams;
        QVERIFY(readEncodedStreams(actual, streams));
        QCOMPARE(streams.types.size(), static_cast<std::size_t>(2));
        QCOMPARE(streams.videoStreamCount, 1);
        QCOMPARE(streams.audioStreamCount, 1);
        QVERIFY2(!streams.subtitle.present,
                 qPrintable(QStringLiteral("the muxer declared an ASS stream in "
                                          "an MP4, which means the capability "
                                          "probe failed closed nowhere; %1")
                                .arg(streamTypeSummary(streams))));

        // And the video is intact, not merely a header: the frame count the
        // encoder produced is what the file carries.
        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(), static_cast<usize>(8));
    }

    void aRecordingWithNoLyricsHasNoSubtitleStream() {
        // The default path: init() called with no document at all. The subtitle
        // code must be inert -- not an error, not an empty subtitle track, not a
        // wasted stream index that shifts the other two.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        fs::path actual;
        QVERIFY(encodeWithSubtitle(fs::path(dir.path().toStdString()) / "plain.mkv",
                                   std::string(), Container::MKV, VideoCodec::FFV1,
                                   AudioCodec::AAC, actual));

        EncodedStreams streams;
        QVERIFY(readEncodedStreams(actual, streams));
        QCOMPARE(streams.types.size(), static_cast<std::size_t>(2));
        QCOMPARE(streams.types[0], AVMEDIA_TYPE_VIDEO);
        QCOMPARE(streams.types[1], AVMEDIA_TYPE_AUDIO);
        QVERIFY2(!streams.subtitle.present,
                 qPrintable(streamTypeSummary(streams)));

        // The video is whole, so "no lyrics" demonstrably cost nothing.
        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(), static_cast<usize>(8));
    }

    void aHeaderlessAssDocumentIsRefusedRatherThanMuxed() {
        // A document with cues but no script, or a script with no cues, is not a
        // subtitle track a player can render. Both are refused before the output
        // context is touched, so the recording is unaffected -- and the file says
        // so, which is the difference between "no lyrics" and "lyrics that did
        // not work" being told apart in a bug report.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const char* unusable[] = {
            "",                                                   // nothing at all
            "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,hello\n", // no header
            "[Script Info]\nScriptType: v4.00+\n[Events]\n",         // no cues
        };

        for (usize i = 0; i < 3; ++i) {
            const std::string name = "unusable" + std::to_string(i) + ".mkv";
            fs::path actual;
            QVERIFY2(encodeWithSubtitle(
                         fs::path(dir.path().toStdString()) / name, unusable[i],
                         Container::MKV, VideoCodec::FFV1, AudioCodec::AAC, actual),
                     qPrintable(QStringLiteral("fixture %1 did not record at all")
                                    .arg(i)));

            EncodedStreams streams;
            QVERIFY(readEncodedStreams(actual, streams));
            QVERIFY2(!streams.subtitle.present,
                     qPrintable(QStringLiteral("fixture %1 produced a subtitle track; %2")
                                    .arg(i).arg(streamTypeSummary(streams))));
            QCOMPARE(streams.videoStreamCount, 1);
            QCOMPARE(streams.audioStreamCount, 1);
        }
    }

    // ── The production call path ────────────────────────────────────────────
    // The two tests below drive VideoRecorder::start, which is the only path a
    // user can reach. Everything above it calls VideoRecorderFFmpeg::init
    // directly and therefore proves the muxer but not the wiring.

    void videoRecorderStartForwardsTheSubtitleDocumentToTheMuxer() {
        // The regression guard for the missing hop.
        //
        // mkvRecordingCarriesTheKaraokeTrack calls ffmpeg_.init on the calling
        // thread, so it proves the muxer and nothing above it. This drives
        // VideoRecorder::start -> VideoRecorderThread::start -> ffmpeg_.init,
        // where the encoding runs on a *different* thread and the document
        // travels as a borrowed view. So it proves two things the direct test
        // structurally cannot:
        //
        //   1. the parameter is forwarded at all -- drop the forwarding and this
        //      fails on the stream count, with init() itself untouched;
        //   2. what the worker reads long after the call is ffmpeg_'s own copy
        //      of the cues, not the caller's buffer. The first cue is written by
        //      the first video packet, the second by cleanup(), so this exercises
        //      a cross-thread read of subtitle_->events on both paths.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::string document = LyricsExport::toAssDocument(karaokeLyrics());
        fs::path actual;
        QVERIFY(recordThroughVideoRecorder(
            testSettings(QString::fromStdString(
                             (fs::path(dir.path().toStdString()) / "wired.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV),
            document, /*forwardSubtitle=*/true, actual));

        EncodedStreams streams;
        QVERIFY(readEncodedStreams(actual, streams));
        QCOMPARE(streams.types.size(), static_cast<std::size_t>(3));
        QCOMPARE(streams.types[2], AVMEDIA_TYPE_SUBTITLE);
        QVERIFY(streams.subtitle.present);
        QCOMPARE(streams.subtitle.codecId, AV_CODEC_ID_ASS);
        QVERIFY(streams.subtitle.disposition & AV_DISPOSITION_DEFAULT);

        QCOMPARE(streams.subtitle.payloads.size(), static_cast<std::size_t>(2));
        const std::string script = reassembledScript(streams.subtitle);
        // Spaced between words, for the reason given in
        // mkvRecordingCarriesTheKaraokeTrack: assEventText joins tokens with a
        // space, and test_LyricsExport.cpp:330 pins that as the golden byte.
        QVERIFY2(script.find("{\\kf50}ka {\\kf50}ra {\\kf50}o {\\kf50}ke") !=
                     std::string::npos,
                 qPrintable(QStringLiteral("reassembled script was:\n%1")
                                .arg(QString::fromStdString(script))));

        // The video half is untouched by any of this.
        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(), static_cast<usize>(8));
    }

    void videoRecorderStartWithoutLyricsStillRecordsNormally() {
        // The one-argument overload, which is what every pre-existing call site
        // compiles to -- frameSubmissionsReachEncoder, the integration
        // GL suite's `recorder.start(settings)`, and the fs::path overload's
        // internal call. So this is the regression guard for the *default*:
        // adding the parameter must not change what a no-lyrics recording
        // produces, and in particular must not reserve a third stream index.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        fs::path actual;
        QVERIFY(recordThroughVideoRecorder(
            testSettings(QString::fromStdString(
                             (fs::path(dir.path().toStdString()) / "plain-wired.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV),
            std::string(), /*forwardSubtitle=*/false, actual));

        EncodedStreams streams;
        QVERIFY(readEncodedStreams(actual, streams));
        QCOMPARE(streams.types.size(), static_cast<std::size_t>(2));
        QCOMPARE(streams.types[0], AVMEDIA_TYPE_VIDEO);
        QCOMPARE(streams.types[1], AVMEDIA_TYPE_AUDIO);
        QVERIFY2(!streams.subtitle.present,
                 qPrintable(streamTypeSummary(streams)));

        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(actual, timeline));
        QCOMPARE(timeline.presentationSeconds.size(), static_cast<usize>(8));
    }

    // ── Burn-in: the optional post-pass ─────────────────────────────────────
    //
    // Burn-in is a post-pass over a *finished* file, never part of recording, and
    // it depends on libavfilter, which is optional by design. So the first thing
    // these tests do is check the gate itself: on a build without the feature the
    // answer must be a visible "unsupported", not a silent no-op.

    void burnInReportsWhetherThisBuildCanRender() {
        // The gate, and the reason it gives. Both must be meaningful: an empty
        // reason claiming availability, or a bare `false` with nothing to show the
        // user, is the silent no-op this requirement exists to prevent.
        const bool available = burnInAvailable();
        const std::string reason = burnInUnavailableReason();

        if (available) {
            QVERIFY2(reason.empty(),
                     "burn-in is available but still carries a reason");
            return;
        }

        QVERIFY2(!reason.empty(),
                 "burn-in is unavailable and gives the user no reason");
        // The two causes are different problems with different fixes, and the
        // user cannot act on "unavailable" alone.
        const bool noLibrary =
            reason.find("CHADVIS_POSTPROCESS") != std::string::npos;
        const bool noFilter = reason.find("libass") != std::string::npos;
        QVERIFY2(noLibrary || noFilter,
                 qPrintable(QStringLiteral(
                     "the unsupported reason names neither a missing libavfilter "
                     "nor a missing filter: %1")
                                .arg(QString::fromStdString(reason))));

        // And a call in that state must fail visibly rather than write an output
        // file that is a byte-for-byte copy of its input.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        fs::path clip;
        QVERIFY(recordLongClip(fs::path(dir.path().toStdString()) / "gate.mkv",
                               clip));
        const auto ass = fs::path(dir.path().toStdString()) / "gate.ass";
        QVERIFY(writeTextFile(ass, "x"));
        const fs::path out = fs::path(dir.path().toStdString()) / "gate-out.mkv";

        BurnInOptions options;
        options.inputVideo = clip;
        options.subtitleFile = ass;
        options.outputVideo = out;
        const auto result = burnInSubtitles(options);
        QVERIFY2(!result,
                 "a build that cannot burn in reported success for the attempt");
        QVERIFY(!fs::exists(out));
    }

    void burnInDrawsTheCueAndLeavesTheBareFramesAlone() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        if (!burnInAvailable()) {
            QSKIP("this build has no burn-in; burnInReportsWhetherThisBuildCanRender"
                  " covers the unsupported state");
        }

        const fs::path dirPath(dir.path().toStdString());
        fs::path clip;
        QVERIFY(recordLongClip(dirPath / "source.mkv", clip));
        const std::vector<u8> inputBefore = readAllBytes(clip);
        QVERIFY(!inputBefore.empty());

        // The real document, from the real writer, so this is the same bytes the
        // sidecar export and the muxed track use. Nothing hand-typed.
        const std::string document = LyricsExport::toAssDocument(karaokeLyrics());
        const fs::path ass = dirPath / "karaoke.ass";
        QVERIFY(writeTextFile(ass, document));

        const fs::path out = dirPath / "burned.mkv";
        BurnInOptions options;
        options.inputVideo = clip;
        options.subtitleFile = ass;
        options.outputVideo = out;
        // ffv1/MKV: the input's own codec, and lossless, so the only thing that
        // can differ between the two files is what the filter drew.
        const auto result = burnInSubtitles(options);
        QVERIFY2(result, qPrintable(QString::fromStdString(
                             result.error().message)));
        QVERIFY(fs::exists(out));

        // The input is untouched. The pass opens it read-only, and this is the
        // check that says so rather than assuming it.
        QCOMPARE(readAllBytes(clip), inputBefore);

        // Frame 10 is inside the 0-2 s cue; frame 66 is in the 2.0-2.5 s gap
        // between the two cues. Same decoder, same index, same source.
        constexpr int kCuedFrame = 10;
        constexpr int kBareFrame = 66;

        LumaFrame inputCued;
        LumaFrame outputCued;
        LumaFrame inputBare;
        LumaFrame outputBare;
        QVERIFY(readLumaFrame(clip, kCuedFrame, inputCued));
        QVERIFY(readLumaFrame(out, kCuedFrame, outputCued));
        QVERIFY(readLumaFrame(clip, kBareFrame, inputBare));
        QVERIFY(readLumaFrame(out, kBareFrame, outputBare));

        QVERIFY(outputCued.width > 0);
        QCOMPARE(outputCued.width, inputCued.width);
        QCOMPARE(outputCued.height, inputCued.height);

        // A cued frame must have changed. This is the assertion that fails if the
        // pass copies its input, and a file-that-exists check would not.
        const FrameDiff cued = diffFrames(inputCued, outputCued);
        QVERIFY2(cued.total > 0,
                 "the cued frame is pixel-identical to the input, so nothing was "
                 "drawn -- the post-pass silently did nothing");

        // ...and the change is where the text is. The karaoke writer's style row
        // is Alignment 2 with MarginV 10, so the text sits at the bottom. A
        // difference scattered evenly over the frame would be a scaling or
        // colour-space change, not a subtitle.
        QVERIFY2(cued.bottomBand * 2 > cued.total,
                 qPrintable(QStringLiteral(
                     "of %1 changed samples only %2 are in the bottom eighth, so "
                     "the difference is not the subtitle")
                                .arg(cued.total).arg(cued.bottomBand)));

        // A frame with no cue on it must be untouched. Lossless in, lossless out,
        // and the filter composites nothing where there is nothing to composite.
        const FrameDiff bare = diffFrames(inputBare, outputBare);
        QVERIFY2(bare.total == 0,
                 qPrintable(QStringLiteral(
                     "%1 samples of a frame with no cue changed, so the pass "
                     "altered pixels it should not have touched")
                                .arg(bare.total)));
    }

    void burnInRefusesToOverwriteAndReportsMissingInputs() {
        // The failure paths, all of which must be a Result error rather than a
        // plausible-looking output. The overwrite case is the one with teeth: a
        // post-pass is meant to be re-runnable, and silently clobbering the
        // previous render is how a comparison between two settings is lost.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());

        BurnInOptions options;
        options.inputVideo = dirPath / "nothing-here.mkv";
        options.subtitleFile = dirPath / "nothing.ass";
        options.outputVideo = dirPath / "out.mkv";

        // Missing input.
        QVERIFY2(!burnInSubtitles(options), "a missing input reported success");

        // Missing subtitle document, with the input now present.
        fs::path clip;
        QVERIFY(recordLongClip(dirPath / "present.mkv", clip));
        options.inputVideo = clip;
        QVERIFY2(!burnInSubtitles(options),
                 "a missing subtitle document reported success");
        QVERIFY2(!burnInSubtitles(options).error().message.empty(),
                 "a refusal carried no reason");

        // An empty subtitle document is refused for a different reason than a
        // missing one, and that distinction matters: one is a user error, the
        // other would produce an output identical to its input.
        const fs::path emptyAss = dirPath / "empty.ass";
        QVERIFY(writeTextFile(emptyAss, ""));
        options.subtitleFile = emptyAss;
        const auto emptyResult = burnInSubtitles(options);
        if (burnInAvailable()) {
            QVERIFY2(!emptyResult,
                     "an empty subtitle document produced a reported success");
        }

        // Refusing to overwrite an existing output.
        if (burnInAvailable()) {
            const fs::path ass = dirPath / "real.ass";
            QVERIFY(writeTextFile(ass, LyricsExport::toAssDocument(karaokeLyrics())));
            options.subtitleFile = ass;
            options.outputVideo = dirPath / "twice.mkv";
            QVERIFY(burnInSubtitles(options));
            QVERIFY(fs::exists(options.outputVideo));

            const std::vector<u8> firstOutput = readAllBytes(options.outputVideo);
            const auto second = burnInSubtitles(options);
            QVERIFY2(!second, "a second run silently overwrote the first output");
            QCOMPARE(readAllBytes(options.outputVideo), firstOutput);
        }
    }

    // ── Degraded audio: a resampler failure ────────────────────────────────
    //
    // The counterpart to the muxer-failure tests above, and the deliberately
    // *different* case. A resampler failure does not damage the file: the
    // container, the index and the audio stream all stay valid, and the result
    // decodes with gaps in the sound. That is exactly why it used to be invisible
    // -- and exactly why it must not borrow the corruption wording.

    void aResamplerFailureIsCountedAndReportedAsDegradedAudio() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AudioQueue queue;
        VideoRecorder recorder;
        recorder.setAudioQueue(&queue);

        auto settings = testSettings(QString::fromStdString(
            (fs::path(dir.path().toStdString()) / "degraded.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV);
        QVERIFY(recorder.start(settings));
        const fs::path clip(recorder.getCurrentStats().currentFile);

        // The injection is armed BEFORE the audio is pushed, and that ordering is
        // the whole determinism of this test. Armed afterwards, it races the
        // worker: how many audio frames have been pulled off the queue by the
        // time the seam is set is a scheduling detail, so a test that injects "N
        // drops" after pushing has an N-dependent chance of dropping every frame
        // that exists and landing in the total-loss branch instead of the
        // degraded one. Armed first, *every* frame the worker consumes is counted
        // deterministically and only the first N are dropped.
        constexpr u32 kInjectedDrops = 2;
        recorder.simulateResampleFailureForTesting(kInjectedDrops);

        // 8 batches of 4096 samples, and 4096 samples at 48 kHz is 4 AAC frames,
        // so at least 32 frames exist. N therefore only has to be smaller than
        // that for the interleaved case; 2 leaves every batch with successes
        // after its drops.
        for (int i = 0; i < 8; ++i) {
            QVERIFY(pushAudio(queue));
        }

        // Video, which the previous version of this test did not submit at all
        // and then asserted 8 frames of. A recording with no video frames still
        // gets a declared video stream, because initVideoStream always creates
        // one and writes the header -- which is why readEncodedStreams reported a
        // video stream and readVideoTimeline still failed, on its last line,
        // having found zero video *packets*. Submitting real frames is what makes
        // "the video is intact" a claim rather than a formality.
        constexpr u64 kVideoFrames = 8;
        for (u64 i = 0; i < kVideoFrames; ++i) {
            recorder.submitVideoFrame(
                std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 17)), 32, 32,
                kOriginUs + static_cast<i64>(i) * kIntervalUs);
        }

        // Wait for both to be consumed before stopping, rather than letting
        // stop()'s flush race the worker. recDepth is the idiom the audio tests
        // already use; framesWritten is the one the video tests use.
        QTRY_VERIFY_WITH_TIMEOUT(queue.recDepth() == 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(recorder.getCurrentStats().framesWritten >= kVideoFrames, 5000);

        const auto stopped = recorder.stop();

        // The user-visible outcome: a failure, with wording that says the audio
        // is degraded and that the rest of the file is fine.
        QVERIFY2(!stopped, "a recording with a degraded audio track reported "
                           "success");
        const QString reason = QString::fromStdString(stopped.error().message);
        QVERIFY2(reason.contains(QStringLiteral("audio is degraded")),
                 qPrintable(QStringLiteral(
                     "expected the message to call the audio degraded, got: %1")
                                .arg(reason)));
        QVERIFY2(!reason.contains(QStringLiteral("incomplete")),
                 qPrintable(QStringLiteral(
                     "the degraded-audio message borrowed the corruption wording: %1")
                                .arg(reason)));
        QVERIFY2(reason.contains(QStringLiteral("plays normally")),
                 qPrintable(QStringLiteral(
                     "the message should say the rest of the file is fine, got: %1")
                                .arg(reason)));

        // Counted, and the count is in the stats rather than only in a message.
        QCOMPARE(recorder.getCurrentStats().audioFramesDropped, u64{kInjectedDrops});

        // The file is still a real recording, which is the whole distinction
        // being tested: it opens, both streams are there, and the video is
        // intact. The frame count is now earned rather than assumed, because the
        // test submits them.
        EncodedStreams streams;
        QVERIFY(readEncodedStreams(clip, streams));
        QCOMPARE(streams.videoStreamCount, 1);
        QCOMPARE(streams.audioStreamCount, 1);
        EncodedTimeline timeline;
        QVERIFY(readVideoTimeline(clip, timeline));
        QCOMPARE(timeline.presentationSeconds.size(),
                 static_cast<usize>(kVideoFrames));
    }

    void stopReasonComposesEachProblemWithoutBorrowingWording() {
        // What extracting composeStopReason bought: the whole decision, as a
        // table, with no worker, no queue, no fault injection and no QTRY.
        //
        // The negatives are the load-bearing half and they are asserted in *both*
        // directions. The corruption wording must not leak into the degradation
        // case -- telling a user whose file plays fine that it is incomplete costs
        // their trust in everything else the application says. And the two
        // degradation severities must not leak into each other: "degraded" for a
        // track with nothing in it is as wrong as "incomplete" for a merely gapped
        // one. Neither assertion can be made from the end-to-end version of this
        // test, which only ever saw the string its own inputs happened to produce.
        //
        // The inputs are not stubbed: each is proven end to end by
        // aResamplerFailureIsCountedAndReportedAsDegradedAudio and
        // aMuxerWriteFailureIsReportedAndNotCounted. What is under test here is
        // only the composition of the two facts -- which inside stop() was
        // unreachable, because both come from a worker that has already been
        // released. See composeStopReason.
        struct Row {
            bool writeFailed;
            u64 droppedFrames;
            bool anyFrameWritten;
            /// Required in the composed reason; empty means "not required".
            const char* contains;
            const char* alsoContains;
            /// Forbidden. Every row names at least the wording it must not borrow.
            const char* excludes;
            const char* alsoExcludes;
        };

        // 42 is the frame count stop() reports, and it is threaded through here so
        // row 2 can check the number really reaches the sentence.
        constexpr u64 kFramesWritten = 42;
        constexpr Row kRows[] = {
            // Clean: nothing to say, so nothing is said.
            {false, 0, false, "", "", "incomplete", "audio"},
            // Corruption alone, and it names where it stopped.
            {true, 0, false, "incomplete", "frame 42", "audio", "degraded"},
            // Gaps in the sound: degraded, and the rest of the file is fine.
            {false, 2, true, "audio is degraded", "plays normally", "incomplete", "no audio"},
            // Nothing of the track survived, which is not a degradation.
            {false, 2, false, "has no audio", "plays normally", "degraded", "incomplete"},
            // Both, in one string, corruption first.
            {true, 2, true, "incomplete", "audio is degraded", "no audio", "has no audio"},
        };
        constexpr usize kRowCount = sizeof(kRows) / sizeof(kRows[0]);

        for (usize i = 0; i < kRowCount; ++i) {
            const Row& row = kRows[i];
            const VideoRecorderFFmpeg::AudioResampleReport audio{
                row.droppedFrames, row.anyFrameWritten};

            const QString reason = QString::fromStdString(
                composeStopReason(row.writeFailed, kFramesWritten, audio));

            if (i == 0) {
                // Checked as emptiness rather than as an absence, because "the
                // message says nothing" is the whole claim for a healthy recording.
                QVERIFY2(reason.isEmpty(),
                         qPrintable(QStringLiteral(
                             "a clean recording was reported as: %1").arg(reason)));
            }

            for (const char* required : {row.contains, row.alsoContains}) {
                if (required == nullptr || *required == '\0') {
                    continue;
                }
                QVERIFY2(reason.contains(QString::fromLatin1(required)),
                         qPrintable(QStringLiteral(
                             "row %1 is missing \"%2\"; it produced: %3")
                                        .arg(i)
                                        .arg(QString::fromLatin1(required))
                                        .arg(reason)));
            }

            for (const char* forbidden : {row.excludes, row.alsoExcludes}) {
                if (forbidden == nullptr || *forbidden == '\0') {
                    continue;
                }
                QVERIFY2(!reason.contains(QString::fromLatin1(forbidden)),
                         qPrintable(QStringLiteral(
                             "row %1 borrowed the wording \"%2\"; it produced: %3")
                                        .arg(i)
                                        .arg(QString::fromLatin1(forbidden))
                                        .arg(reason)));
            }
        }
    }

    void aHealthyRecordingReportsNothingAndKeepsItsAudio() {
        // The negative case, and the one that would catch a counter that is
        // always non-zero or a message that fires on a clean recording. A test
        // suite that only ever injects failures proves nothing about the path a
        // user actually takes.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AudioQueue queue;
        VideoRecorder recorder;
        recorder.setAudioQueue(&queue);
        QVERIFY(recorder.start(testSettings(
            QString::fromStdString(
                (fs::path(dir.path().toStdString()) / "healthy.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));
        for (int i = 0; i < 4; ++i) {
            QVERIFY(pushAudio(queue));
        }

        // Wait for the audio to be *encoded*, not merely dequeued, before stopping.
        //
        // The encoding loop waits up to 10 ms for a video frame before it looks at
        // the audio queue, and this test submits none, so stop() can arrive before
        // the worker's first pass and the file can come out with two declared
        // streams and no packets in either. readEncodedStreams cannot even open
        // such a file, and "reported clean" over a file with nothing in it would
        // prove nothing -- which is exactly how this test failed before the wait
        // was added.
        //
        // bytesWritten rather than recDepth() == 0: the depth only says a batch
        // left the queue, and the pop happens before the encode inside the same
        // pass. bytesWritten advances only after encodeAudio reported success, so
        // it is proof that audio reached the muxer -- and here audio is the only
        // thing being encoded, so it is unambiguous.
        QTRY_VERIFY_WITH_TIMEOUT(recorder.getCurrentStats().bytesWritten > 0, 3000);

        const auto stopped = recorder.stop();
        QVERIFY2(stopped, qPrintable(QString::fromStdString(
                              stopped.error().message)));
        QCOMPARE(recorder.getCurrentStats().audioFramesDropped, u64{0});

        // And the audio really is there, so "reported clean" is not the same
        // thing as "recorded nothing".
        const fs::path clip(recorder.getCurrentStats().currentFile);
        EncodedStreams streams;
        QVERIFY(readEncodedStreams(clip, streams));
        QCOMPARE(streams.audioStreamCount, 1);
    }

    void resampleFailureCountingIsNotStickyAcrossRecordings() {
        // The encoder is reusable, so a recording that lost audio frames must not
        // make the next one look like it did. Same reason the muxer-failure flag
        // is reset in cleanup().
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());

        {
            VideoRecorderFFmpeg encoder;
            QVERIFY(encoder.init(testSettings(
                QString::fromStdString((dirPath / "first.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));
            QVERIFY(encoder.audioResampleReport().droppedFrames == 0);
            QVERIFY(encoder.audioResampleReport().anyFrameWritten == false);

            std::vector<f32> samples(4096 * 2, 0.05f);
            u64 bytes = 0;
            QVERIFY(encoder.encodeAudio(samples, 2, bytes) ==
                    VideoRecorderFFmpeg::AudioEncodeOutcome::Encoded);
            QVERIFY(encoder.audioResampleReport().anyFrameWritten);

            encoder.simulateResampleFailureForTesting(3);
            std::vector<f32> more(4096 * 2, 0.05f);
            encoder.encodeAudio(more, 2, bytes);
            QVERIFY2(encoder.audioResampleReport().droppedFrames == 3,
                     "the injected failures were not counted");
            encoder.cleanup();
            QVERIFY2(encoder.audioResampleReport().droppedFrames == 0,
                     "cleanup() left the count non-zero for the next recording");
        }

        {
            VideoRecorderFFmpeg encoder;
            QVERIFY(encoder.init(testSettings(
                QString::fromStdString((dirPath / "second.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));
            QVERIFY(encoder.audioResampleReport().droppedFrames == 0);
            std::vector<f32> samples(4096 * 2, 0.05f);
            u64 bytes = 0;
            QVERIFY(encoder.encodeAudio(samples, 2, bytes) ==
                    VideoRecorderFFmpeg::AudioEncodeOutcome::Encoded);
            QVERIFY2(encoder.audioResampleReport().droppedFrames == 0,
                     "a fresh encoder reported drops from a previous recording");
        }
    }

    // ── Which "nothing happened" ─────────────────────────────────────────────
    //
    // encodeAudio returned one bool for two opposite situations, and three things
    // downstream had to guess which one they were looking at. The three tests
    // below pin the distinction at the seam that decides it.

    void aTotalResamplerLossIsNotAnEncodingError() {
        // The dropped-frame batch came back `false`, the encoding loop read that as
        // an encoding error, and the user was told "Encoding error occurred" up to
        // ~100 times a second through RecordingBridge -- and then, at stop(), told
        // the file was fine. The drop path already reports this, once, in its own
        // words; the loop must not announce it as a generic fault.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());

        VideoRecorderFFmpeg encoder;
        QVERIFY(encoder.init(testSettings(
            QString::fromStdString((dirPath / "all-dropped.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));

        // 4096 samples at 48 kHz is 4 AAC frames, and the injection covers all
        // four, so this is the total-loss case: no audio, and nothing wrong. Exact
        // because nothing here runs on a worker thread.
        constexpr u32 kFramesInBatch = 4;
        encoder.simulateResampleFailureForTesting(kFramesInBatch + 4);
        std::vector<f32> samples(4096 * 2, 0.05f);
        u64 bytes = 0;

        const auto outcome = encoder.encodeAudio(samples, 2, bytes);
        QVERIFY2(outcome != VideoRecorderFFmpeg::AudioEncodeOutcome::Failed,
                 "a batch whose every frame was dropped by the resampler was "
                 "reported as an encoder failure, which the loop turns into a "
                 "user-visible error every pass");
        QCOMPARE(encoder.audioResampleReport().droppedFrames, u64{kFramesInBatch});
        QVERIFY(!encoder.audioResampleReport().anyFrameWritten);
        QCOMPARE(bytes, u64{0});

        // And the partial case is progress rather than a fault, which is the other
        // half: some of the batch got through, so something reached the file.
        encoder.simulateResampleFailureForTesting(2);
        std::vector<f32> more(4096 * 2, 0.05f);
        QVERIFY(encoder.encodeAudio(more, 2, bytes) ==
                VideoRecorderFFmpeg::AudioEncodeOutcome::Encoded);
        QVERIFY(encoder.audioResampleReport().anyFrameWritten);
        QVERIFY2(bytes > 0, "no audio packet was counted after a partial drop");
    }

    void aRefusedAudioWriteIsNotReportedAsAFrameThatReachedTheFile() {
        // `anyFrameEncoded` was seeded from audioFrameCount_, which is the pts
        // counter and is advanced *before* the frame is encoded -- so it answered
        // "did the encoder accept a frame", and could report an essentially empty
        // audio track as encoded. stop() then attributed every hole in the sound
        // to the resampler when the cause was a mux loss. Renaming it would have
        // hidden that; the fix is the second counter, and this is its test.
        //
        // One AAC frame -- 1024 samples at 48 kHz, stereo -- per call, and the muxer
        // refuses the first packet that comes out. The audio track ends up empty
        // with zero resample drops, which is the case the old counter got exactly
        // backwards.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        VideoRecorderFFmpeg encoder;
        QVERIFY(encoder.init(testSettings(
            QString::fromStdString(
                (fs::path(dir.path().toStdString()) / "lost-audio.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));

        u64 bytes = 0;
        encoder.simulateWriteFailureForTesting();

        // One AAC frame per call -- 1024 samples at 48 kHz, stereo -- fed until
        // the muxer refuses. How many sends it takes is the encoder's own delay,
        // which is exactly the thing not worth hard-coding: a mux loss is a mux
        // loss however many packets the encoder was holding behind it. The refusal
        // is one-shot, so no later frame can succeed, and encodeAudio
        // short-circuits on writeFailed_ anyway.
        bool refused = false;
        for (int i = 0; i < 8 && !refused; ++i) {
            std::vector<f32> oneFrame(1024 * 2, 0.05f);
            const auto outcome = encoder.encodeAudio(oneFrame, 2, bytes);
            QVERIFY2(outcome != VideoRecorderFFmpeg::AudioEncodeOutcome::Failed,
                     "a mux loss was reported as an encoder fault; it has its own "
                     "one-shot report");
            refused = encoder.writeFailed();
        }

        QVERIFY2(refused,
                 "the injected refusal never reached the muxer, so no audio packet "
                 "was attempted and this test proves nothing");
        QCOMPARE(encoder.audioResampleReport().droppedFrames, u64{0});
        QVERIFY2(!encoder.audioResampleReport().anyFrameWritten,
                 "a refused audio packet was reported as a frame that reached "
                 "the file");
        QCOMPARE(bytes, u64{0});
    }

    void theFlushDrainStopsOnceTheMuxerHasRefusedAWrite() {
        // flush() was the one write path that ignored writeFailed_, while
        // encodeVideo and encodeAudio both short-circuit on it -- the same decision,
        // two paths. On a full volume the drain kept writing, and kept emitting a
        // LOG_ERROR per packet because writeFailureReported_ guards only the
        // Backpressure line, into a container that had already been declared
        // untrustworthy. av_write_trailer in cleanup() deliberately still runs; see
        // the comment there.
        //
        // Observable, because a *refused* packet contributes no bytes and a
        // successful one does. So the ordering matters: one write is refused
        // first, while a packet the encoders are still holding waits for the
        // drain, and the refusal is one-shot -- the drain's write would succeed.
        // Bytes unchanged across flush() is then an observation, not a tautology.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path dirPath(dir.path().toStdString());

        {
            VideoRecorderFFmpeg encoder;
            QVERIFY(encoder.init(testSettings(
                QString::fromStdString((dirPath / "drain.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));

            u64 bytes = 0;
            QVERIFY(encoder.encodeVideo(solidFrame(11), bytes));

            // One AAC frame per call, fed until the muxer refuses: the refusal is
            // the first write, and the frame that triggered it is still inside the
            // encoder, so there is a packet waiting for the drain. The seam is
            // one-shot, so the drain's write would succeed -- which is what makes
            // the byte count an observation instead of a tautology.
            encoder.simulateWriteFailureForTesting();
            bool refused = false;
            for (int i = 0; i < 8 && !refused; ++i) {
                std::vector<f32> oneFrame(1024 * 2, 0.05f);
                encoder.encodeAudio(oneFrame, 2, bytes);
                refused = encoder.writeFailed();
            }
            QVERIFY2(refused,
                     "the injected refusal never reached the muxer, so the drain "
                     "had nothing to be compared against");

            const u64 beforeDrain = bytes;
            encoder.flush(bytes);
            QVERIFY2(bytes == beforeDrain,
                     qPrintable(QStringLiteral(
                         "the drain wrote into a container the muxer had already "
                         "refused: %1 bytes became %2")
                                    .arg(beforeDrain).arg(bytes)));
            encoder.cleanup();
        }

        // The control, and the reason the assertion above is not vacuous: the same
        // fixture with nothing refused *does* still hold a packet when the drain
        // runs, and the drain writes it. If this fails, the codec under test emits
        // everything immediately, "bytes did not change" would hold for a drain
        // that had nothing to do, and the test above would be proving nothing.
        {
            VideoRecorderFFmpeg control;
            QVERIFY(control.init(testSettings(
                QString::fromStdString((dirPath / "drain-control.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));

            u64 bytes = 0;
            QVERIFY(control.encodeVideo(solidFrame(11), bytes));
            std::vector<f32> oneFrame(1024 * 2, 0.05f);
            control.encodeAudio(oneFrame, 2, bytes);
            const u64 beforeDrain = bytes;
            control.flush(bytes);
            QVERIFY2(bytes > beforeDrain,
                     "the control's drain wrote nothing, so the assertion above "
                     "could not tell a gated drain from an empty one");
            control.cleanup();
        }
    }

    // ── Failure reporting ───────────────────────────────────────────────────
    // The recorder's whole job is producing a correct file, and until these
    // landed it reported success in two independent ways: a start that never
    // opened a muxer became an active recording, and a muxer that had been
    // refusing packets for a minute still had its frame counter climbing.

    void aFailedStartDoesNotBecomeAnActiveRecording() {
        // Deterministic, permission-free failure: the output path's parent is a
        // *regular file*, so init()'s exclusive create fails with ENOTDIR. Not
        // EEXIST, so it is not mistaken for a filename collision and retried, and
        // not EACCES, so this does not depend on the test not running as root --
        // which is what makes an "unwritable directory" fixture unreliable.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const fs::path blocker =
            fs::path(dir.path().toStdString()) / "not-a-directory";
        {
            std::ofstream file(blocker);
            QVERIFY(file.is_open());
            file << "x";
        }
        QVERIFY(fs::is_regular_file(blocker));

        auto settings = testSettings(QString::fromStdString(
            (blocker / "recording.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV);

        VideoRecorder recorder;
        const auto started = recorder.start(settings);

        // The Result, not just the absence of a file: the caller has to be able
        // to tell, and RecordingBridge::startRecording already reports
        // result.error().message verbatim.
        QVERIFY2(!started, "a start that never opened a muxer reported success");
        QVERIFY2(!started.error().message.empty(),
                 "the failure carried no reason for the user");

        // The state, which is what the UI reads. On the old code this was
        // Recording: the frame counter climbed, the timer ran, and no file ever
        // appeared.
        QVERIFY2(!recorder.isRecording(),
                 "the recorder claims to be recording after a failed start");
        QCOMPARE(recorder.state(), RecordingState::Error);

        // Nothing was recorded. The file may or may not exist -- init() claims
        // the path before opening the muxer -- but the counters must not claim a
        // frame reached it.
        QCOMPARE(recorder.getCurrentStats().framesWritten, u64{0});
        QCOMPARE(recorder.getCurrentStats().bytesWritten, u64{0});

        // Error is recoverable, and this is the part that would otherwise be a
        // worse bug than the one being fixed: start() used to refuse any state
        // that was not Stopped, so one failed recording bricked the recorder for
        // the rest of the session.
        const auto retried = recorder.start(
            testSettings(QString::fromStdString(
                             (fs::path(dir.path().toStdString()) / "retry.mkv").string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV));
        QVERIFY2(retried, qPrintable(QString::fromStdString(
                               retried.error().message)));
        QVERIFY(recorder.isRecording());
        (void)recorder.stop();
    }

    void aMuxerWriteFailureIsReportedAndNotCounted() {
        // The (b) half. av_interleaved_write_frame's failure cannot be provoked
        // portably -- see VideoRecorderFFmpeg::simulateWriteFailureForTesting for
        // the three mechanisms rejected and why -- so what is tested here is the
        // half this application owns: once the muxer has said no, the encoder
        // stops reporting frames as written, and stop() refuses to call the
        // recording complete. Which return codes mean what is covered separately
        // by muxerWriteResultsAreClassifiedByDamage.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        VideoRecorder recorder;
        auto settings = testSettings(QString::fromStdString(
                                         (fs::path(dir.path().toStdString()) / "io.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV);
        QVERIFY(recorder.start(settings));

        // Count frames the healthy way first, so there is a real number for the
        // failure to be measured against.
        for (u64 i = 0; i < 8; ++i) {
            recorder.submitVideoFrame(
                std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 17)), 32, 32,
                kOriginUs + static_cast<i64>(i) * kIntervalUs);
        }
        QTRY_VERIFY_WITH_TIMEOUT(recorder.getCurrentStats().framesWritten >= 8, 5000);

        // The muxer starts refusing. The encoding loop sees this on its next
        // pass, stops producing, and emits exactly one error -- which reaches the
        // user through RecordingBridge's existing connection to
        // VideoRecorder::error.
        const u64 healthyFrames = recorder.getCurrentStats().framesWritten;
        recorder.simulateWriteFailureForTesting();

        // Deliberately submitted *after* the failure, so a counter that keeps
        // climbing here is a counter counting frames the file never received.
        for (u64 i = 8; i < 24; ++i) {
            recorder.submitVideoFrame(
                std::vector<u8>(32 * 32 * 4, static_cast<u8>(i * 17)), 32, 32,
                kOriginUs + static_cast<i64>(i) * kIntervalUs);
        }

        const auto stopped = recorder.stop();

        // This is the assertion the old code could not pass in any form: it
        // returned ok() and the caller reported a completed recording for a file
        // with holes in it.
        const QString reason = QString::fromStdString(stopped.error().message);
        QVERIFY2(!stopped, "a recording with a failing muxer reported success");
        QVERIFY2(reason.contains(QStringLiteral("incomplete")),
                 qPrintable(QStringLiteral("expected an 'incomplete' reason, got: %1")
                                .arg(reason)));
        QVERIFY2(reason.contains(QStringLiteral("full volume")),
                 qPrintable(QStringLiteral("expected the likely cause to be named: %1")
                                .arg(reason)));

        // And the counter stopped the moment the muxer did. Sixteen further
        // frames were submitted after the failure, and at most one of them could
        // still be in flight -- the loop checks for the failure at the top of
        // each pass, so exactly one frame per pass can escape the check. The old
        // code incremented framesWritten on a `true` that writePacket's discarded
        // answer could not reach, so it climbed through all sixteen.
        const u64 failedFrames = recorder.getCurrentStats().framesWritten;
        QVERIFY2(failedFrames >= healthyFrames,
                 "the frame counter went backwards");
        QVERIFY2(failedFrames <= healthyFrames + 1,
                 qPrintable(QStringLiteral("framesWritten advanced from %1 to %2 "
                                          "after the muxer started failing")
                                .arg(healthyFrames).arg(failedFrames)));
        QCOMPARE(recorder.state(), RecordingState::Stopped);
    }

    void muxerWriteResultsAreClassifiedByDamage() {
        // The decision, tested directly, because it is the one thing about a
        // muxer that cannot be provoked: libavformat's reliable way to be handed
        // a bad return is a full filesystem. The carve-out that matters most is
        // EAGAIN -- backpressure, not damage -- and misclassifying it is what
        // would turn a recording that merely runs a moment behind into one
        // reported as broken.
        // QVERIFY rather than QCOMPARE: QCOMPARE needs an operator<< for the
        // failure report, and gaining one for a three-valued enum that only has
        // to be compared is not worth the API.
        QVERIFY(classifyWriteResult(0) == WriteOutcome::Written);
        QVERIFY(classifyWriteResult(4096) == WriteOutcome::Written);

        QVERIFY(classifyWriteResult(AVERROR(EAGAIN)) == WriteOutcome::Backpressure);

        QVERIFY(classifyWriteResult(AVERROR(ENOSPC)) == WriteOutcome::Failed);
        QVERIFY(classifyWriteResult(AVERROR(EIO)) == WriteOutcome::Failed);
        QVERIFY(classifyWriteResult(AVERROR(EINVAL)) == WriteOutcome::Failed);
    }

    void aRefusedPacketIsClassifiedAsFailedThroughTheWritePath() {
        // writePacket's `case WriteOutcome::Failed`, executed. The seam used to
        // assign writeFailed_ itself, so it set the *consequence* and left the one
        // statement that produces it uncovered by any test anywhere: the arm, the
        // sticky flag, and the packet's bytes not being counted were all reached
        // only by tests that never made the muxer say no.
        //
        // The seam now substitutes the return value at the call, so this runs the
        // same production classification a full volume does -- which is the point:
        // the arm is the classification, and the classification is the behaviour.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        VideoRecorderFFmpeg encoder;
        QVERIFY(encoder.init(testSettings(
            QString::fromStdString(
                (fs::path(dir.path().toStdString()) / "refused.mkv").string()),
            VideoCodec::FFV1, AudioCodec::AAC, Container::MKV)));

        u64 bytes = 0;
        // One healthy frame first, so the refused one is refused against a muxer
        // that was working.
        QVERIFY(encoder.encodeVideo(solidFrame(11), bytes));

        // The volume fills: the very next write is refused. That is the
        // ENOSPC-at-zero-bytes case, and it is the right default -- `writeFailed_`
        // gates packets, not the header, so a refusal here leaves a file that has a
        // valid header and a hole in it. (The old test's comment claimed the
        // opposite, which is part of why it could never be set up.)
        encoder.simulateWriteFailureForTesting();

        QVERIFY2(!encoder.encodeVideo(solidFrame(23, kIntervalUs), bytes),
                 "a refused packet was reported as written");
        QVERIFY2(encoder.writeFailed(),
                 "the refusal did not mark the file damaged -- so nothing above "
                 "executed the Failed arm and this test proves nothing");
        // And it is the one fault the encoding loop is told about once, rather
        // than a generic encode error per frame. This is also the whole chain the
        // old seam skipped: VideoRecorderThread increments framesWritten on a true
        // from here, and stop() reads the flag this arm sets.
        QVERIFY(encoder.reportWriteFailure());
        QVERIFY(!encoder.reportWriteFailure());
        encoder.cleanup();
    }

    void aWriteFailureIsNotStickyAcrossRecordings() {
        // The encoder is reusable, so a failed instance must not leave the next
        // recording looking broken. cleanup() resets the flag; the reporting
        // snapshot has to be taken before it does, which is why
        // VideoRecorderThread::stop reads ffmpeg_.writeFailed() before cleanup().
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        auto settingsFor = [&](const char* name) {
            return testSettings(QString::fromStdString(
                                   (fs::path(dir.path().toStdString()) / name).string()),
                VideoCodec::FFV1, AudioCodec::AAC, Container::MKV);
        };

        {
            VideoRecorderFFmpeg encoder;
            QVERIFY(encoder.init(settingsFor("first.mkv")));
            QVERIFY(!encoder.writeFailed());
            // Arm the seam, then drive a real write. Setting the flag directly --
            // which is all the old seam could do -- would leave this test asserting
            // that a member can be assigned, and would skip the classification that
            // actually sets it.
            encoder.simulateWriteFailureForTesting();
            u64 bytes = 0;
            QVERIFY(!encoder.encodeVideo(solidFrame(3), bytes));
            QVERIFY2(encoder.writeFailed(),
                     "a refused packet did not mark the file damaged");
            // One-shot reporting: the first caller gets it, the second does not,
            // which is what keeps the encoding loop from emitting one error per
            // frame for the rest of the recording.
            QVERIFY(encoder.reportWriteFailure());
            QVERIFY(!encoder.reportWriteFailure());
            encoder.cleanup();
            QVERIFY2(!encoder.writeFailed(),
                     "cleanup() left the encoder looking failed to the next "
                     "recording");
        }

        {
            VideoRecorderFFmpeg encoder;
            QVERIFY(encoder.init(settingsFor("second.mkv")));
            QVERIFY(!encoder.writeFailed());
            GrabbedFrame frame;
            frame.width = 32;
            frame.height = 32;
            frame.timestamp = kOriginUs;
            frame.data = std::vector<u8>(32 * 32 * 4, 3);
            u64 bytes = 0;
            // Healthy again, so encodeVideo reports success rather than
            // short-circuiting on a stale flag.
            QVERIFY(encoder.encodeVideo(frame, bytes));
            encoder.flush(bytes);
            encoder.cleanup();
            QVERIFY(!encoder.writeFailed());
        }
    }

    // ── AVFormatContext ownership ───────────────────────────────────────────
    // A demuxer context has oformat == nullptr. The single shared deleter read
    // c->oformat->flags to decide whether to close pb, so every read path
    // segfaulted at 0x2c the moment the unique_ptr released its context. The
    // fix is in the types, so the guard that matters is a compile-time one: a
    // reader and a writer must not be interchangeable in either direction.

    void formatContextReaderAndWriterAreDistinctTypes() {
        static_assert(!std::is_same_v<AVFormatContextInPtr, AVFormatContextOutPtr>);
        static_assert(!std::is_same_v<AVFormatContextInDeleter,
                                      AVFormatContextOutDeleter>);
        // is_convertible is the check that actually rejects the mistake. Two
        // unique_ptrs over the same pointer are different types either way;
        // what must fail to compile is `out = in` and `in = out`.
        static_assert(!std::is_convertible_v<AVFormatContextInPtr,
                                             AVFormatContextOutPtr>);
        static_assert(!std::is_convertible_v<AVFormatContextOutPtr,
                                             AVFormatContextInPtr>);
        // Both still own the same C type, so a .get() is usable either way.
        static_assert(std::is_same_v<AVFormatContextInPtr::element_type,
                                     AVFormatContext>);
        static_assert(std::is_same_v<AVFormatContextOutPtr::element_type,
                                     AVFormatContext>);

        // Both aliases are usable as owning pointers right now. Constructing
        // each from its own allocation is the runtime half of the same claim.
        AVFormatContextInPtr in(avformat_alloc_context());
        AVFormatContextOutPtr out(avformat_alloc_context());
        QVERIFY(in != nullptr);
        QVERIFY(out != nullptr);
        QVERIFY(in->pb == nullptr);
        QVERIFY(out->pb == nullptr);
        in.reset();
        out.reset();
    }

    void inputAliasClosesADemuxerContext() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto path = fs::path(dir.path().toStdString()) / "demux-probe.wav";
        QVERIFY(writeTestWav(path));
        const auto pathStr = path.string();

        AVFormatContext* raw = nullptr;
        QCOMPARE(avformat_open_input(&raw, pathStr.c_str(), nullptr, nullptr), 0);
        QVERIFY(raw != nullptr);
        // The exact state that killed the old deleter: a demuxer has an
        // iformat and no oformat.
        QVERIFY(raw->iformat != nullptr);
        QVERIFY(raw->oformat == nullptr);

        i32 packetsRead = 0;
        {
            AVFormatContextInPtr format(raw);
            QVERIFY(format);
            // lavf opened this and owns it, so the close has to release it.
            QVERIFY(format->pb != nullptr);
            QVERIFY(avformat_find_stream_info(format.get(), nullptr) >= 0);
            QCOMPARE(av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1,
                                         nullptr, 0),
                     0);

            AVPacketPtr packet(av_packet_alloc());
            QVERIFY(packet);
            QCOMPARE(av_read_frame(format.get(), packet.get()), 0);
            av_packet_unref(packet.get());
            packetsRead = 1;
        } // releases here; the old deleter dereferenced oformat and crashed

        QCOMPARE(packetsRead, 1);
    }

    void inputAliasReleasesASelfAllocatedContext() {
        // A context with no iformat and no pb is still a context we own.
        // avformat_close_input skips the read_close hook and calls
        // avio_close(nullptr), so this is a clean avformat_free_context --
        // the input alias must not require a demuxer to be safe.
        {
            AVFormatContextInPtr context(avformat_alloc_context());
            QVERIFY(context);
            QVERIFY(context->iformat == nullptr);
            QVERIFY(context->pb == nullptr);
        }
        // Default construction and a reset with nothing to do are no-ops, which
        // is what every optional-path early return in the recorder relies on.
        AVFormatContextInPtr empty;
        empty.reset();
        QVERIFY(empty == nullptr);
    }

    void outputAliasClosesTheIOContextItOwns() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto path = fs::path(dir.path().toStdString()) / "mux-close.wav";
        const auto pathStr = path.string();

        AVFormatContext* raw = nullptr;
        QCOMPARE(avformat_alloc_output_context2(&raw, nullptr, nullptr,
                                                pathStr.c_str()),
                 0);
        QVERIFY(raw != nullptr);
        QVERIFY(raw->oformat != nullptr);
        // Not an AVFMT_NOFILE muxer, so the caller owns pb -- exactly the
        // production shape from VideoRecorderFFmpeg::init.
        QVERIFY((raw->oformat->flags & AVFMT_NOFILE) == 0);
        QCOMPARE(avio_open(&raw->pb, pathStr.c_str(), AVIO_FLAG_WRITE), 0);
        QVERIFY(raw->pb != nullptr);

        {
            AVFormatContextOutPtr format(raw);
            QVERIFY(format);
            const u8 payload[512] = {};
            avio_write(format->pb, payload, static_cast<int>(sizeof(payload)));
        } // releases here: close pb, then free the context

        // 512 bytes stay inside the 32 KiB AVIOContext buffer until it is
        // closed, so a non-empty file is the observable proof that the deleter
        // closed pb instead of leaking it. FFmpeg has no public allocation
        // counter, so this is the release assertion that actually observes
        // something.
        QVERIFY(fs::file_size(path) > 0);
    }

    void outputAliasLeavesACustomIOContextToItsOwner() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto path = fs::path(dir.path().toStdString()) / "custom-io.wav";
        const auto pathStr = path.string();

        AVFormatContext* raw = nullptr;
        QCOMPARE(avformat_alloc_output_context2(&raw, nullptr, nullptr,
                                                pathStr.c_str()),
                 0);
        QVERIFY(raw != nullptr);
        // Held separately because after the context dies we still have to
        // close this ourselves -- that is the whole point of the flag.
        AVIOContext* owned = nullptr;
        QCOMPARE(avio_open(&owned, pathStr.c_str(), AVIO_FLAG_WRITE), 0);
        QVERIFY(owned != nullptr);
        raw->pb = owned;
        // "The caller has supplied a custom AVIOContext, don't avio_close() it."
        // The old check tested oformat->flags instead, which describes the
        // muxer and cannot see this, so it closed a pb it did not own.
        raw->flags |= AVFMT_FLAG_CUSTOM_IO;

        {
            AVFormatContextOutPtr format(raw);
            const u8 payload[512] = {};
            avio_write(format->pb, payload, static_cast<int>(sizeof(payload)));
        }

        // Still zero bytes: the deleter honoured the flag and left the buffer
        // unflushed, which is only possible if the AVIOContext survived.
        QVERIFY(fs::file_size(path) == 0);

        // And the survivor is a usable, live AVIOContext -- the caller closes
        // it and the bytes land, closing the loop with no leak.
        avio_closep(&owned);
        QVERIFY(owned == nullptr);
        QVERIFY(fs::file_size(path) > 0);
    }
};

int runTestRecorder(int argc, char** argv) {
    TestRecorder test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_RecordingPipeline.moc"
