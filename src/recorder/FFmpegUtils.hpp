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

// Helper for error messages
inline std::string ffmpegError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

} // namespace vc
