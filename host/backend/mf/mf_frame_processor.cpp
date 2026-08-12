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
    swsContext_ = sws_getContext(
        width_, height_, srcFormat,
        width_, height_, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsContext_) {
        qCCritical(log_multimedia_backend) << "Failed to create sws context for"
                                           << width_ << "x" << height_ << inputFormat_;
        return false;
    }

    rgbBufferSize_ = width_ * height_ * 3; // RGB24
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
    if (!data || !swsContext_ || dataSize <= 0) {
        return QImage();
    }

#ifdef HAVE_FFMPEG
    const uint8_t* srcSlice[1] = { data };
    int srcStride[1] = { 0 };

    // Calculate source stride based on format
    if (inputFormat_ == "NV12") {
        srcStride[0] = width_; // NV12 stride is width for Y plane
    } else if (inputFormat_ == "YUY2" || inputFormat_ == "YUYV") {
        srcStride[0] = width_ * 2; // YUY2 is 2 bytes per pixel
    } else if (inputFormat_ == "RGB24") {
        srcStride[0] = width_ * 3;
    } else if (inputFormat_ == "RGB32") {
        srcStride[0] = width_ * 4;
    } else {
        srcStride[0] = width_; // Default
    }

    uint8_t* dstSlice[1] = { rgbBuffer_ };
    int dstStride[1] = { width_ * 3 };

    sws_scale(
        swsContext_,
        srcSlice, srcStride,
        0, height_,
        dstSlice, dstStride
    );

    return QImage(rgbBuffer_, width_, height_, dstStride[0], QImage::Format_RGB888).copy();
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
