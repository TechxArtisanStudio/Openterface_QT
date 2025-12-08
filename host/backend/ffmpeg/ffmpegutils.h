// Helper utilities for managing FFmpeg AVFrame/AVPacket instances with RAII
#pragma once
#ifdef HAVE_FFMPEG
#if defined(__cplusplus)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
}
#endif // __cplusplus

#include <memory>

struct AvFrameDeleter {
    void operator()(AVFrame* frame) const {
        av_frame_free(&frame);
    }
};

struct AvPacketDeleter {
    void operator()(AVPacket* pkt) const {
        av_packet_free(&pkt);
    }
};

using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

inline AvFramePtr make_av_frame() {
    return AvFramePtr(av_frame_alloc());
}

inline AvPacketPtr make_av_packet() {
    return AvPacketPtr(av_packet_alloc());
}
#else
// If FFmpeg isn't available, provide dummy forward declarations and types to allow headers to compile
struct AVFrame;
struct AVPacket;
using AvFramePtr = AVFrame*;
using AvPacketPtr = AVPacket*;
inline AvFramePtr make_av_frame() { return static_cast<AvFramePtr>(nullptr); }
inline AvPacketPtr make_av_packet() { return static_cast<AvPacketPtr>(nullptr); }
#endif

// Cross-build helpers to get raw AV pointers and safely reset wrappers
#ifdef HAVE_FFMPEG
#define AV_FRAME_RAW(x) ((x).get())
#define AV_PACKET_RAW(x) ((x).get())
#define AV_FRAME_RESET(x) ((x).reset())
#define AV_PACKET_RESET(x) ((x).reset())
#else
#define AV_FRAME_RAW(x) (x)
#define AV_PACKET_RAW(x) (x)
#define AV_FRAME_RESET(x) do { if (x) { av_frame_free(&x); x = nullptr; } } while(0)
#define AV_PACKET_RESET(x) do { if (x) { av_packet_free(&x); x = nullptr; } } while(0)
#endif
