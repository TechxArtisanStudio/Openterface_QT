#include "mf_frame_processor.h"

#include <QDebug>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_multimedia_backend)

MfFrameProcessor::MfFrameProcessor()
    : width_(0)
    , height_(0)
    , swsContext_(nullptr)
    , rgbBuffer_(nullptr)
    , rgbBufferSize_(0)
{
}

MfFrameProcessor::~MfFrameProcessor()
{
    cleanup();
}

bool MfFrameProcessor::initialize(int width, int height, const QString& inputFormat)
{
    cleanup();

    width_ = width;
    height_ = height;
    inputFormat_ = inputFormat;

    AVPixelFormat srcFormat;
    if (inputFormat == "NV12") {
        srcFormat = AV_PIX_FMT_NV12;
    } else if (inputFormat == "YUY2" || inputFormat == "YUYV") {
        srcFormat = AV_PIX_FMT_YUYV422;
    } else if (inputFormat == "RGB24") {
        srcFormat = AV_PIX_FMT_RGB24;
    } else if (inputFormat == "RGB32") {
        srcFormat = AV_PIX_FMT_RGB32;
    } else if (inputFormat == "MJPG" || inputFormat == "MJPEG") {
        // MJPEG needs to be decoded first — handled separately
        srcFormat = AV_PIX_FMT_YUV420P;
    } else {
        // Default to NV12 (most common MF output format)
        srcFormat = AV_PIX_FMT_NV12;
        qCWarning(log_multimedia_backend) << "Unknown input format:" << inputFormat
                                          << "— defaulting to NV12";
    }

#ifdef HAVE_FFMPEG
    // Output BGR24 — matches MFVideoFormat_RGB24's byte layout (B, G, R) and
    // QImage::Format_BGR888. Keeping the output in BGR throughout avoids an
    // unnecessary byte-swap pass on the fast path.
    swsContext_ = sws_getContext(
        width_, height_, srcFormat,
        width_, height_, AV_PIX_FMT_BGR24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsContext_) {
        qCCritical(log_multimedia_backend) << "Failed to create sws context for"
                                           << width_ << "x" << height_ << inputFormat_;
        return false;
    }

    rgbBufferSize_ = width_ * height_ * 3; // BGR24
    rgbBuffer_ = new uchar[rgbBufferSize_];
#else
    qCCritical(log_multimedia_backend) << "FFmpeg not available for frame conversion";
    return false;
#endif

    qCInfo(log_multimedia_backend) << "MfFrameProcessor initialized:"
                                   << width_ << "x" << height_
                                   << "input:" << inputFormat_;
    return true;
}

QImage MfFrameProcessor::processFrame(const uchar* data, int dataSize)
{
    if (!data || dataSize <= 0) {
        return QImage();
    }

#ifdef HAVE_FFMPEG
    // Fast path: MF's MFVideoFormat_RGB24 stores bytes in BGR order (B, G, R)
    // per the Windows bitmap convention. QImage::Format_BGR888 matches that
    // layout exactly, so no byte reordering is needed — just copy.
    // (If we used Format_RGB888 here, red and blue would be swapped: red
    // objects would appear purple/cyan.)
    if (inputFormat_ == "RGB24") {
        const int expected = width_ * height_ * 3;
        if (dataSize < expected) {
            qCWarning(log_multimedia_backend) << "RGB24 buffer too small:"
                                              << dataSize << "<" << expected;
            return QImage();
        }
        // Construct QImage over the source buffer, then .copy() into our
        // owned memory so it outlives the MF buffer lock.
        return QImage(data, width_, height_, width_ * 3, QImage::Format_BGR888).copy();
    }

    if (!swsContext_) {
        qCWarning(log_multimedia_backend) << "sws context null for format" << inputFormat_;
        return QImage();
    }

    uint8_t* dstSlice[1] = { rgbBuffer_ };
    int dstStride[1] = { width_ * 3 };

    if (inputFormat_ == "NV12") {
        // NV12 is a 2-plane format:
        //   Plane 0: Y samples,      stride = width, size = width * height
        //   Plane 1: interleaved UV, stride = width, size = width * height/2
        // Total size = width * height * 3/2.
        // The MF sample buffer is contiguous, so the UV plane starts at
        // data + width*height.
        const int ySize = width_ * height_;
        if (dataSize < ySize + ySize / 2) {
            qCWarning(log_multimedia_backend) << "NV12 buffer too small:"
                                              << dataSize << "<" << (ySize + ySize / 2);
            return QImage();
        }
        const uint8_t* srcSlice[2] = { data, data + ySize };
        int srcStride[2] = { width_, width_ };

        sws_scale(swsContext_,
                  srcSlice, srcStride,
                  0, height_,
                  dstSlice, dstStride);
    } else {
        // Single-plane formats (YUY2, RGB32, etc.)
        const uint8_t* srcSlice[1] = { data };
        int srcStride[1] = { 0 };

        if (inputFormat_ == "YUY2" || inputFormat_ == "YUYV") {
            srcStride[0] = width_ * 2;
        } else if (inputFormat_ == "RGB32") {
            srcStride[0] = width_ * 4;
        } else {
            srcStride[0] = width_;
        }

        sws_scale(swsContext_,
                  srcSlice, srcStride,
                  0, height_,
                  dstSlice, dstStride);
    }

    // sws_scale output is BGR24 (matches MFVideoFormat_RGB24 convention)
    return QImage(rgbBuffer_, width_, height_, dstStride[0], QImage::Format_BGR888).copy();
#else
    Q_UNUSED(dataSize);
    return QImage();
#endif
}

void MfFrameProcessor::cleanup()
{
#ifdef HAVE_FFMPEG
    if (swsContext_) {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }
#endif

    delete[] rgbBuffer_;
    rgbBuffer_ = nullptr;
    rgbBufferSize_ = 0;
    width_ = 0;
    height_ = 0;
}
