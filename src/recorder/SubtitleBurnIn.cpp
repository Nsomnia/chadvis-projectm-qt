// Version: 1.0.0
// Description: Optional post-pass: burn a subtitle track into a finished file.
//
// See SubtitleBurnIn.hpp for why libavfilter is optional and why there are two
// gates. This file is ALWAYS compiled. With CHADVIS_HAS_AVFILTER it holds the
// real filter-graph implementation; without it, this same translation unit holds
// the stub that makes burnInUnavailableReason() explain the absence and returns
// an error from every entry point. One file, two bodies, one set of symbols
// either way -- so an optional dependency can never become a link error of the
// whole application on a stripped distro, which is the entire point of the gate.
//
// The corollary is load-bearing: do NOT gate this file in cmake/Sources.cmake on
// CHADVIS_HAS_AVFILTER. Doing so once removed the very translation unit that
// holds the stub, leaving vc::burnIn* undefined and turning every reference into
// a link failure. The macro in cmake/TargetSetup.cmake is the only gate.

#include "SubtitleBurnIn.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/Logger.hpp"
#include "recorder/FFmpegUtils.hpp"

#ifndef CHADVIS_HAS_AVFILTER
// Normalised to 0 so the two branches are symmetric and survive someone
// enabling -Wundef, rather than relying on #if treating an undefined name as
// zero.
#define CHADVIS_HAS_AVFILTER 0
#endif

#if CHADVIS_HAS_AVFILTER
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}
#endif

namespace vc {

// ── Compiled out ─────────────────────────────────────────────────────────────
//
// The whole feature, in three functions, when there is no libavfilter. The
// recorder, the Suno client and the player are all unaffected: nothing in them
// references anything below.

#if !CHADVIS_HAS_AVFILTER

bool burnInAvailable() { return false; }

std::string burnInUnavailableReason() {
  return "This build has no subtitle burn-in: it was configured without "
         "libavfilter (CMake option CHADVIS_POSTPROCESS). Recording and "
         "playback are unaffected; install libavfilter and reconfigure to "
         "enable burn-in.";
}

Result<void> burnInSubtitles(const BurnInOptions&) {
  return Result<void>::err(burnInUnavailableReason());
}

#else

// ── Compiled in ──────────────────────────────────────────────────────────────

namespace {

/// The filter that renders. `ass` is the .ass-specific one and is preferred;
/// `subtitles` is the general filter, accepted as a fallback so a build shipping
/// only that one still works. Both are libass-backed and take the same options,
/// so one argument string serves either.
///
/// Measured on this machine (FFmpeg 9.0.1, libavfilter 12.1.101): both present.
/// Measured because "the library links" is not "the filter exists" -- a
/// libavfilter built without libass has neither, and that is a runtime fact no
/// configure-time check can see.
const char* findRenderFilter() {
  static const char* const kCandidates[] = {"ass", "subtitles"};
  for (const char* name : kCandidates) {
    if (avfilter_get_by_name(name)) {
      return name;
    }
  }
  return nullptr;
}

/// Escape a path for a libavfilter option value.
///
/// The filter argument parser splits on `:` and consumes `\`, so an unescaped
/// Windows path (`C:\Users\...`) reads as option separators and the graph fails
/// to configure with a message naming neither the path nor the cause. The
/// backslash is escaped too, and before the character, because the escape
/// character is itself consumed by the parser.
std::string escapeFilterValue(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    if (ch == '\\' || ch == ':' || ch == '\'') {
      escaped += '\\';
    }
    escaped += ch;
  }
  return escaped;
}

/// The filter's arguments. `original_size` is what libass scales the script's
/// PlayResX/PlayResY against, so omitting it lands the text at a size the author
/// did not choose.
std::string renderFilterArgs(const BurnInOptions& options, const int width,
                             const int height) {
  std::string args =
      "filename=" + escapeFilterValue(options.subtitleFile.string());
  if (width > 0 && height > 0) {
    args += ":original_size=" + std::to_string(width) + "x" +
            std::to_string(height);
  }
  if (!options.fontsDir.empty()) {
    // A *directory* added to the font search path, which is the filter's only
    // font option. There is no per-filter font override: which face renders and
    // how large it is come from the script's [V4+ Styles] row, which is where the
    // karaoke writer put them deliberately. So "change the font and re-run" is
    // an edit to the document plus a re-run of this pass -- not a parameter here.
    args += ":fontsdir=" + escapeFilterValue(options.fontsDir.string());
  }
  return args;
}

/// An output video stream with its encoder, or an audio stream that is a plain
/// copy with no encoder behind it. Bundled because both are used identically at
/// every call site.
struct OutputStream {
  AVStream* stream{nullptr};
  AVCodecContext* codecCtx{nullptr};
};

/// The filter graph, owned. Deliberately a hand-written destructor rather than a
/// unique_ptr: AVFilterGraph* is a C type and the deleter is one line, so a
/// wrapper type would be a third name for it.
struct FilterChain {
  AVFilterGraph* graph{nullptr};
  AVFilterContext* source{nullptr};
  AVFilterContext* sink{nullptr};

  FilterChain() = default;
  FilterChain(const FilterChain&) = delete;
  FilterChain& operator=(const FilterChain&) = delete;
  ~FilterChain() {
    if (graph) {
      avfilter_graph_free(&graph);
    }
  }
};

Result<void> validate(const BurnInOptions& options) {
  if (options.inputVideo.empty()) {
    return Result<void>::err("Burn-in needs an input file");
  }
  if (options.subtitleFile.empty()) {
    // Not pedantry: a burn-in with no subtitle document renders exactly the
    // input, and reporting that as a success is indistinguishable from having
    // done the work.
    return Result<void>::err("Burn-in needs a subtitle file");
  }
  if (options.outputVideo.empty()) {
    return Result<void>::err("Burn-in needs an output file");
  }
  if (!fs::exists(options.inputVideo)) {
    return Result<void>::err("No such input file: " +
                             options.inputVideo.string());
  }
  if (!fs::exists(options.subtitleFile)) {
    return Result<void>::err("No such subtitle file: " +
                             options.subtitleFile.string());
  }
  if (fs::exists(options.outputVideo)) {
    return Result<void>::err(
        "Refusing to overwrite " + options.outputVideo.string() +
        ". A post-pass is meant to be re-runnable, so give the output a name "
        "that says which setting produced it rather than letting one render "
        "clobber another");
  }
  return Result<void>::ok();
}

} // namespace

bool burnInAvailable() { return findRenderFilter() != nullptr; }

std::string burnInUnavailableReason() {
  if (findRenderFilter()) {
    return {};
  }
  // This code is only compiled when libavfilter is present, so the library is
  // here and the missing piece is the filter -- which means the libavfilter in
  // use was built without libass. Same symptom from the user's side as a build
  // configured without libavfilter at all, so the reason has to say which.
  return "This build has libavfilter but it carries no `ass` or `subtitles` "
         "filter, so there is nothing to render subtitles with: the libavfilter "
         "in use was built without libass. Recording and playback are "
         "unaffected.";
}

Result<void> burnInSubtitles(const BurnInOptions& options) {
  // The gate first, reported as a Result rather than asserted: a build without
  // the filter must fail *visibly*, and this is the one place every caller can
  // rely on for that.
  if (const std::string unavailable = burnInUnavailableReason(); !unavailable.empty()) {
    return Result<void>::err(unavailable);
  }
  if (auto invalid = validate(options); !invalid) {
    return invalid;
  }

  // ── input, read-only ──
  AVFormatContext* rawInput = nullptr;
  if (avformat_open_input(&rawInput, options.inputVideo.c_str(), nullptr,
                          nullptr) < 0) {
    return Result<void>::err("Could not open " + options.inputVideo.string());
  }
  AVFormatContextInPtr input(rawInput);
  if (!input) {
    return Result<void>::err("Could not open " + options.inputVideo.string());
  }
  if (avformat_find_stream_info(input.get(), nullptr) < 0) {
    return Result<void>::err("Could not read " + options.inputVideo.string());
  }

  const int videoIndex =
      av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (videoIndex < 0) {
    return Result<void>::err("No video stream in " +
                             options.inputVideo.string());
  }
  const int audioIndex =
      av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  const AVStream* inVideo = input->streams[videoIndex];
  const int width = static_cast<int>(inVideo->codecpar->width);
  const int height = static_cast<int>(inVideo->codecpar->height);
  if (width <= 0 || height <= 0) {
    return Result<void>::err("The input video has no usable dimensions");
  }

  // ── output ──
  // The encoder name defaults to the input's, because a visual edit should not
  // silently change the codec: the same codec at the same settings is what makes
  // the pass a no-op when the subtitle file happens to be empty of cues.
  const char* codecName = options.videoCodec.empty()
                              ? avcodec_get_name(inVideo->codecpar->codec_id)
                              : options.videoCodec.c_str();
  if (codecName == nullptr || *codecName == '\0') {
    return Result<void>::err(
        "The input video codec has no name, so there is nothing to re-encode it "
        "with; set videoCodec explicitly");
  }
  const AVCodec* encoder = avcodec_find_encoder_by_name(codecName);
  if (!encoder) {
    return Result<void>::err("No such video encoder: " +
                             std::string(codecName));
  }

  AVFormatContext* rawOutput = nullptr;
  if (avformat_alloc_output_context2(&rawOutput, nullptr, nullptr,
                                    options.outputVideo.c_str()) < 0) {
    return Result<void>::err("No muxer for " + options.outputVideo.string());
  }
  AVFormatContextOutPtr output(rawOutput);
  if (!output) {
    return Result<void>::err("Failed to allocate an output context");
  }

  // ── video: re-encode, matching the input's parameters ──
  OutputStream outVideo;
  outVideo.stream = avformat_new_stream(output.get(), nullptr);
  if (!outVideo.stream) {
    return Result<void>::err("Failed to create the output video stream");
  }
  // From the input's parameters, not from scratch, so profile, level and
  // extradata match what was recorded. avcodec_copy_params would strip the
  // extradata a re-encode needs to keep the profile.
  if (avcodec_parameters_copy(outVideo.stream->codecpar, inVideo->codecpar) < 0) {
    return Result<void>::err("Failed to copy the input video parameters");
  }
  outVideo.codecCtx = avcodec_alloc_context3(encoder);
  if (!outVideo.codecCtx) {
    return Result<void>::err("Failed to allocate the output video codec");
  }
  // From here the codec context is owned, so every later return frees it.
  AVCodecContextPtr videoEncoder(outVideo.codecCtx);
  outVideo.codecCtx = nullptr;

  if (avcodec_parameters_to_context(videoEncoder.get(), inVideo->codecpar) < 0) {
    return Result<void>::err("Failed to configure the output video codec from "
                             "the input parameters");
  }
  videoEncoder->time_base = inVideo->time_base;

  // CRF is a private encoder option, not an AVCodecContext field, so it goes in
  // through the option dictionary rather than by assignment. Only for the
  // x264/x265 family; for anything else the dictionary would carry an option the
  // encoder does not have, and libavcodec's own advice is to let it go.
  AVDictionary* encoderOptions = nullptr;
  if (options.crf > 0 && (videoEncoder->codec_id == AV_CODEC_ID_H264 ||
                          videoEncoder->codec_id == AV_CODEC_ID_HEVC)) {
    av_dict_set(&encoderOptions, "crf", std::to_string(options.crf).c_str(), 0);
  }
  int ret = avcodec_open2(videoEncoder.get(), encoder, &encoderOptions);
  if (ret < 0) {
    // The leftovers are what the encoder refused and why, which is otherwise
    // invisible from here.
    AVDictionaryEntry* rejected = nullptr;
    std::string reason;
    while ((rejected = av_dict_get(encoderOptions, "", rejected, AV_DICT_IGNORE_SUFFIX))) {
      reason += std::string(" [") + rejected->key + ": " + rejected->value + "]";
    }
    av_dict_free(&encoderOptions);
    return Result<void>::err("Failed to open the output video encoder " +
                             std::string(codecName) + ": " + ffmpegError(ret) +
                             reason);
  }
  av_dict_free(&encoderOptions);
  if (avcodec_parameters_from_context(outVideo.stream->codecpar,
                                      videoEncoder.get()) < 0) {
    return Result<void>::err("Failed to finalise the output video parameters");
  }
  outVideo.stream->time_base = videoEncoder->time_base;

  // ── audio: stream copy ──
  // Copied, never re-encoded. A post-pass exists so a visual edit is cheap, and
  // a three-minute audio re-encode would give that away.
  OutputStream outAudio;
  if (audioIndex >= 0) {
    const AVStream* inAudio = input->streams[audioIndex];
    outAudio.stream = avformat_new_stream(output.get(), nullptr);
    if (!outAudio.stream) {
      return Result<void>::err("Failed to create the output audio stream");
    }
    if (avcodec_parameters_copy(outAudio.stream->codecpar, inAudio->codecpar) < 0) {
      return Result<void>::err("Failed to copy the input audio parameters");
    }
    outAudio.stream->time_base = inAudio->time_base;
  }

  if (!(output->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&output->pb, options.outputVideo.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
      return Result<void>::err("Could not open " +
                               options.outputVideo.string() + ": " +
                               ffmpegError(ret));
    }
  }
  AVDictionary* headerOptions = nullptr;
  ret = avformat_write_header(output.get(), &headerOptions);
  av_dict_free(&headerOptions);
  if (ret < 0) {
    return Result<void>::err("Failed to write the output header: " +
                             ffmpegError(ret));
  }

  // ── the filter graph ──
  FilterChain chain;
  chain.graph = avfilter_graph_alloc();
  if (!chain.graph) {
    return Result<void>::err("Failed to allocate the filter graph");
  }

  const AVRational frameRate = inVideo->avg_frame_rate.num > 0
                                   ? inVideo->avg_frame_rate
                                   : inVideo->r_frame_rate;
  const char* pixelFormat = av_get_pix_fmt_name(
      static_cast<AVPixelFormat>(videoEncoder->pix_fmt));
  if (pixelFormat == nullptr) {
    pixelFormat = "yuv420p";
  }

  // One buffer, declared from the input, with the input's own time base so frame
  // timestamps pass through unrescaled.
  //
  // An *argument string*, not an AVDictionary: avfilter_graph_create_filter's
  // fourth parameter is `const char *args` and its fifth is `void *opaque`, so a
  // dictionary handed over there compiles, links, and fails at runtime with
  // EPERM from inside the filter's init. Measured: a buffer built that way
  // returns "Operation not permitted" on FFmpeg 9.0.1 while the identical
  // `video_size=...:pix_fmt=...` string configures cleanly.
  const char* inputPixelFormat = av_get_pix_fmt_name(
      static_cast<AVPixelFormat>(inVideo->codecpar->format));
  if (inputPixelFormat == nullptr) {
    return Result<void>::err("The input video reports no pixel format, so the "
                             "filter graph cannot be described");
  }
  std::string sourceArgs =
      "video_size=" + std::to_string(width) + "x" + std::to_string(height) +
      ":pix_fmt=" + inputPixelFormat +
      ":time_base=" + std::to_string(inVideo->time_base.num) + "/" +
      std::to_string(inVideo->time_base.den);
  if (frameRate.num > 0 && frameRate.den > 0) {
    sourceArgs += ":frame_rate=" + std::to_string(frameRate.num) + "/" +
                  std::to_string(frameRate.den);
  }
  ret = avfilter_graph_create_filter(&chain.source,
                                     avfilter_get_by_name("buffer"), "in",
                                     sourceArgs.c_str(), nullptr, chain.graph);
  if (ret < 0) {
    return Result<void>::err("Failed to create the filter source with \"" +
                             sourceArgs + "\": " + ffmpegError(ret));
  }

  AVFilterContext* render = nullptr;
  const std::string filterArgs = renderFilterArgs(options, width, height);
  ret = avfilter_graph_create_filter(&render,
                                     avfilter_get_by_name(findRenderFilter()),
                                     "render", filterArgs.c_str(), nullptr,
                                     chain.graph);
  if (ret < 0) {
    return Result<void>::err(
        "Failed to create the subtitle filter with arguments \"" + filterArgs +
        "\": " + ffmpegError(ret));
  }

  // Forces the encoder's pixel format. Without it the graph negotiates whatever
  // the render filter prefers, and the encoder may then refuse a frame it was
  // never configured for -- reported as a confusing send_frame failure rather
  // than as a format mismatch.
  AVFilterContext* format = nullptr;
  ret = avfilter_graph_create_filter(&format, avfilter_get_by_name("format"),
                                     "format", pixelFormat, nullptr,
                                     chain.graph);
  if (ret < 0) {
    return Result<void>::err("Failed to create the format filter: " +
                             ffmpegError(ret));
  }

  ret = avfilter_graph_create_filter(&chain.sink,
                                     avfilter_get_by_name("buffersink"), "out",
                                     nullptr, nullptr, chain.graph);
  if (ret < 0) {
    return Result<void>::err("Failed to create the filter sink: " +
                             ffmpegError(ret));
  }

  // source -> render -> format -> sink. All three links, in that order: an
  // unlinked source is not caught by avfilter_link and surfaces instead as
  // EPERM from avfilter_graph_config with "Output pad ... not connected to any
  // destination", which names neither the missing link nor the file. Measured on
  // FFmpeg 9.0.1.
  ret = avfilter_link(chain.source, 0, render, 0);
  if (ret < 0) {
    return Result<void>::err("Failed to link the filter source: " +
                             ffmpegError(ret));
  }
  ret = avfilter_link(render, 0, format, 0);
  if (ret < 0) {
    return Result<void>::err("Failed to link the subtitle filter: " +
                             ffmpegError(ret));
  }
  ret = avfilter_link(format, 0, chain.sink, 0);
  if (ret < 0) {
    return Result<void>::err("Failed to link the format filter: " +
                             ffmpegError(ret));
  }
  ret = avfilter_graph_config(chain.graph, nullptr);
  if (ret < 0) {
    // libavfilter's own log has already named the reason; this adds the graph
    // description, which is usually where the answer is.
    return Result<void>::err(
        "Failed to configure the filter graph (\"" + filterArgs +
        "\"): " + ffmpegError(ret));
  }

  // ── transcode ──
  AVCodecContextPtr videoDecoder(avcodec_alloc_context3(
      avcodec_find_decoder(inVideo->codecpar->codec_id)));
  if (!videoDecoder) {
    return Result<void>::err("No decoder for the input video codec");
  }
  if (avcodec_parameters_to_context(videoDecoder.get(), inVideo->codecpar) < 0) {
    return Result<void>::err("Failed to configure the input video decoder");
  }
  if (avcodec_open2(videoDecoder.get(),
                    avcodec_find_decoder(inVideo->codecpar->codec_id),
                    nullptr) < 0) {
    return Result<void>::err("Failed to open the input video decoder");
  }

  AVPacketPtr packet(av_packet_alloc());
  AVFramePtr decoded(av_frame_alloc());
  AVFramePtr filtered(av_frame_alloc());
  if (!packet || !decoded || !filtered) {
    return Result<void>::err("Failed to allocate the transcode buffers");
  }

  u64 bytesWritten = 0;
  bool writeFailed = false;
  // Set when the muxer says no for a reason that damages the file, so the caller
  // is not told a truncated output is a finished one. Same discipline, and the
  // same classification, as the recorder.
  const auto writeOutputPacket = [&](AVPacket* out) {
    if (writeFailed) {
      return;
    }
    // Read the length BEFORE the muxer runs. av_interleaved_write_frame takes
    // ownership of the packet and blanks it on success (size -> 0,
    // pts -> AV_NOPTS_VALUE), so reading afterwards yields 0 for every packet
    // that actually reached the file -- which is why this pass logged
    // "wrote <file> (0 bytes)" on every single success. Third instance of this
    // defect; the recorder had the same one in writePacket.
    const int payloadBytes = out->size;
    const int written =
        av_interleaved_write_frame(output.get(), out);
    if (classifyWriteResult(written) == WriteOutcome::Written) {
      bytesWritten += payloadBytes;
      return;
    }
    if (classifyWriteResult(written) == WriteOutcome::Failed) {
      writeFailed = true;
      LOG_ERROR("Burn-in: the muxer refused a packet: {}", ffmpegError(written));
      return;
    }
    LOG_WARN("Burn-in: a packet was deferred by the muxer");
  };

  // No pts rescale here, and that is load-bearing rather than lazy: the graph's
  // source buffer was declared with the *input* video stream's time base and the
  // encoder was given the same one, so a frame leaving the sink is already in
  // the encoder's base. Rescaling by hand would double-apply the factor.
  const auto emitFiltered = [&](AVFrame* frame) -> Result<void> {
    ret = avcodec_send_frame(videoEncoder.get(), frame);
    if (ret < 0) {
      return Result<void>::err("Failed to send a frame to the encoder: " +
                               ffmpegError(ret));
    }
    while (true) {
      ret = avcodec_receive_packet(videoEncoder.get(), packet.get());
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return Result<void>::ok();
      }
      if (ret < 0) {
        return Result<void>::err("Failed to receive an encoded packet: " +
                                 ffmpegError(ret));
      }
      av_packet_rescale_ts(packet.get(), videoEncoder->time_base,
                           outVideo.stream->time_base);
      packet->stream_index = outVideo.stream->index;
      writeOutputPacket(packet.get());
      av_packet_unref(packet.get());
    }
  };

  while (true) {
    ret = av_read_frame(input.get(), packet.get());
    if (ret == AVERROR_EOF) {
      break;
    }
    if (ret < 0) {
      return Result<void>::err("Failed to read " +
                               options.inputVideo.string() + ": " +
                               ffmpegError(ret));
    }

    if (packet->stream_index == videoIndex) {
      if (avcodec_send_packet(videoDecoder.get(), packet.get()) < 0) {
        av_packet_unref(packet.get());
        return Result<void>::err("Failed to send a packet to the decoder");
      }
      while (true) {
        const int decodedRet = avcodec_receive_frame(videoDecoder.get(),
                                                     decoded.get());
        if (decodedRet == AVERROR(EAGAIN) || decodedRet == AVERROR_EOF) {
          break;
        }
        if (decodedRet < 0) {
          av_packet_unref(packet.get());
          return Result<void>::err("Failed to decode a frame: " +
                                   ffmpegError(decodedRet));
        }
        if (av_buffersrc_add_frame_flags(chain.source, decoded.get(),
                                         AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
          av_frame_unref(decoded.get());
          av_packet_unref(packet.get());
          return Result<void>::err("Failed to feed a frame to the filter graph");
        }
        while (true) {
          const int pulled = av_buffersink_get_frame(chain.sink, filtered.get());
          if (pulled == AVERROR(EAGAIN) || pulled == AVERROR_EOF) {
            break;
          }
          if (pulled < 0) {
            av_frame_unref(filtered.get());
            av_packet_unref(packet.get());
            return Result<void>::err("The subtitle filter failed: " +
                                     ffmpegError(pulled));
          }
          if (auto emitted = emitFiltered(filtered.get()); !emitted) {
            av_frame_unref(filtered.get());
            av_packet_unref(packet.get());
            return emitted;
          }
          av_frame_unref(filtered.get());
        }
        av_frame_unref(decoded.get());
      }
      av_packet_unref(packet.get());
      continue;
    }

    if (outAudio.stream && packet->stream_index == audioIndex) {
      // Timestamps are already in the copied stream's time base, so this is a
      // verbatim remux: no rescale, no re-encode.
      packet->stream_index = outAudio.stream->index;
      writeOutputPacket(packet.get());
    }
    av_packet_unref(packet.get());
  }

  // Drain the decoder, then the graph, then the encoder. In that order, because
  // each stage's tail is the next stage's next input.
  if (avcodec_send_packet(videoDecoder.get(), nullptr) >= 0) {
    while (true) {
      const int decodedRet = avcodec_receive_frame(videoDecoder.get(),
                                                   decoded.get());
      if (decodedRet < 0) {
        break;
      }
      if (av_buffersrc_add_frame_flags(chain.source, decoded.get(),
                                       AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_frame_unref(decoded.get());
        return Result<void>::err("Failed to feed a drained frame to the filter");
      }
      av_frame_unref(decoded.get());
    }
  }
  if (av_buffersrc_add_frame(chain.source, nullptr) < 0) {
    return Result<void>::err("Failed to signal end of stream to the filter");
  }
  while (true) {
    const int pulled = av_buffersink_get_frame(chain.sink, filtered.get());
    if (pulled < 0) {
      break;
    }
    if (auto emitted = emitFiltered(filtered.get()); !emitted) {
      return emitted;
    }
    av_frame_unref(filtered.get());
  }
  avcodec_send_frame(videoEncoder.get(), nullptr);
  while (true) {
    ret = avcodec_receive_packet(videoEncoder.get(), packet.get());
    if (ret < 0) {
      break;
    }
    av_packet_rescale_ts(packet.get(), videoEncoder->time_base,
                         outVideo.stream->time_base);
    packet->stream_index = outVideo.stream->index;
    writeOutputPacket(packet.get());
    av_packet_unref(packet.get());
  }
  ret = av_write_trailer(output.get());
  if (ret < 0) {
    return Result<void>::err(
        "Failed to write the output trailer, so the file is not playable: " +
        ffmpegError(ret));
  }
  if (writeFailed) {
    return Result<void>::err(
        "The muxer refused a packet, so the output is incomplete and was left "
        "at " + options.outputVideo.string() +
        " for inspection rather than reported as finished");
  }

  LOG_INFO("Burn-in wrote {} ({} bytes)", options.outputVideo.string(),
           bytesWritten);
  return Result<void>::ok();
}

#endif // CHADVIS_HAS_AVFILTER

} // namespace vc
