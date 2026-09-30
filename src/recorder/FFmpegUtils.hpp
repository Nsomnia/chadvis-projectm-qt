#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <memory>
#include <string>

namespace vc {

// RAII Deleters for FFmpeg types
struct AVFrameDeleter { void operator()(AVFrame* f) const { if (f) av_frame_free(&f); } };
struct AVPacketDeleter { void operator()(AVPacket* p) const { if (p) av_packet_free(&p); } };
struct AVCodecContextDeleter { void operator()(AVCodecContext* c) const { if (c) avcodec_free_context(&c); } };
struct SwsContextDeleter { void operator()(SwsContext* s) const { if (s) sws_freeContext(s); } };
struct SwrContextDeleter { void operator()(SwrContext* s) const { if (s) swr_free(&s); } };

// ── AVFormatContext: two deleters, not one ──────────────────────────────────
// The single shared AVFormatContextDeleter did the *output* half and read
// `c->oformat->flags` on the way to decide whether to close `pb`. `oformat` is
// a field only a muxer sets, so the moment a demuxer context reached that
// deleter it dereferenced null and crashed. The split lives in the pointer
// types (AVFormatContextInPtr / AVFormatContextOutPtr) so that handing a reader
// to the writer is a compile error rather than a segfault.
//
//   input  -- avformat_close_input() runs the demuxer's read_close hook, frees
//             the context, and only then closes the AVIOContext it opened.
//             Calling avformat_free_context() on a demuxer leaks that
//             AVIOContext. Conversely avformat_close_input() is meaningless on
//             a context we allocated ourselves: no iformat, no owned pb.
//   output -- avformat_free_context() does not touch `pb` at all; libavformat
//             documents the caller as its owner for muxing (AVFormatContext::pb
//             -- "the caller must take care of closing / freeing the IO
//             context"), so the output deleter closes it explicitly. The
//             production path is exactly that: avio_open() at
//             VideoRecorderFFmpeg.cpp, guarded by the same AVFMT_NOFILE test.
//
// The opt-out from closing `pb` is AVFMT_FLAG_CUSTOM_IO -- "The caller has
// supplied a custom AVIOContext, don't avio_close() it" -- a field of the
// context itself, so it is always readable. It is *not* oformat->flags, which
// describes the muxer and says nothing about who owns the AVIOContext; the old
// check therefore missed the one case that matters (a caller-owned pb on a
// muxer without AVFMT_NOFILE, e.g. the image2 pattern) while dereferencing a
// pointer that does not exist on the read side.
inline void closeOwnedAVIOContext(AVFormatContext* c) {
    if (c->pb && !(c->flags & AVFMT_FLAG_CUSTOM_IO)) {
        avio_closep(&c->pb);
    }
}

struct AVFormatContextOutDeleter {
    void operator()(AVFormatContext* c) const {
        if (!c) return;
        closeOwnedAVIOContext(c); // must precede the free: c->pb dies with c
        avformat_free_context(c);
    }
};

struct AVFormatContextInDeleter {
    void operator()(AVFormatContext* c) const {
        // By-value parameter, so nulling our own copy *is* the contract.
        if (c) avformat_close_input(&c);
    }
};

// Unique pointer aliases
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
// Output: contexts from avformat_alloc_output_context2() or
// avformat_alloc_context(). Closes a caller-owned pb, then frees.
using AVFormatContextOutPtr = std::unique_ptr<AVFormatContext, AVFormatContextOutDeleter>;
// Input: contexts from avformat_open_input(). Must not be used for a muxer;
// there is deliberately no alias that fits both.
using AVFormatContextInPtr = std::unique_ptr<AVFormatContext, AVFormatContextInDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

/// What one `av_interleaved_write_frame` return code means.
///
/// Lives here rather than in VideoRecorderFFmpeg.hpp because the subtitle
/// post-pass classifies its own writes the same way, and FFmpegUtils.hpp is the
/// one header both can include without either depending on the other.
///
/// Split out of writePacket and made a free function because this
/// classification *is* the behaviour worth testing, and it is the one thing
/// about a muxer that cannot be provoked from a test: libavformat's only
/// reliable way to be handed a bad return is a full filesystem. A pure function
/// over the return code is directly checkable, and it pins the case that matters
/// most -- EAGAIN is not a failure.
enum class WriteOutcome {
  /// >= 0. The packet is in the file.
  Written,
  /// `AVERROR(EAGAIN)`. The interleave buffer is full and the caller is being
  /// asked to come back later. **The packet was not written**, so this is not
  /// success, but it is also not damage: nothing about the file is wrong, the
  /// recorder simply cannot keep up for a moment. Treating it as a failure
  /// would mark healthy recordings as broken under load, which is the exact
  /// inversion this function exists to prevent.
  Backpressure,
  /// Anything else. The muxer refused the packet -- ENOSPC, EIO, EINVAL. The
  /// packets already accepted are interleaved with holes, and the resulting
  /// container opens, plays to the break and then stops, which is worse than a
  /// failed recording because it looks like a good one.
  Failed
};

/// Classify an `av_interleaved_write_frame` return code. See `WriteOutcome`.
constexpr WriteOutcome classifyWriteResult(const int result) {
  return result >= 0 ? WriteOutcome::Written
                     : (result == AVERROR(EAGAIN) ? WriteOutcome::Backpressure
                                                  : WriteOutcome::Failed);
}

// Helper for error messages
inline std::string ffmpegError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

} // namespace vc
