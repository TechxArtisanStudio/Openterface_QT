#ifndef MF_FRAME_PROCESSOR_H
#define MF_FRAME_PROCESSOR_H

#include <QImage>
#include <QSize>
#include <QString>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}
#endif

class MfFrameProcessor {
public:
    MfFrameProcessor();
    ~MfFrameProcessor();

    bool initialize(int width, int height, const QString& inputFormat);
    QImage processFrame(const uchar* data, int dataSize);
    void cleanup();

private:
    int width_;
    int height_;
    QString inputFormat_;
    SwsContext* swsContext_;
    uchar* rgbBuffer_;
    int rgbBufferSize_;
};

#endif // MF_FRAME_PROCESSOR_H
