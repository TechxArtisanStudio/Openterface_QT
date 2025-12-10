/*
 * ICaptureFrameReader - Interface for frame reading
 * Allows CaptureThread to work with different handler types polymorphically
 */

#ifndef ICAPTURE_FRAME_READER_H
#define ICAPTURE_FRAME_READER_H

/**
 * @brief Interface for capture frame reading
 * Allows CaptureThread to work with different handler types (FFmpegBackendHandler, FFmpegCaptureManager)
 */
class ICaptureFrameReader
{
public:
    virtual ~ICaptureFrameReader() = default;
    virtual bool readFrame() = 0;
};

#endif // ICAPTURE_FRAME_READER_H
