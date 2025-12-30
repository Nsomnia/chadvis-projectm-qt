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
struct AVFormatContextDeleter { 
    void operator()(AVFormatContext* c) const { 
        if (c) {
            if (c->pb && !(c->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&c->pb);
            }
            avformat_free_context(c); 
        }
    } 
};
struct SwsContextDeleter { void operator()(SwsContext* s) const { if (s) sws_freeContext(s); } };
struct SwrContextDeleter { void operator()(SwrContext* s) const { if (s) swr_free(&s); } };

// Unique pointer aliases
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

// Helper for error messages
inline std::string ffmpegError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

} // namespace vc
