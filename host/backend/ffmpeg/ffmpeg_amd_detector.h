#ifndef FFMPEG_AMD_DETECTOR_H
#define FFMPEG_AMD_DETECTOR_H

#include <QString>

class FFmpegAmdDetector {
public:
    static bool isAmdIntegratedGpuDetected();
    static QString getAmdGpuInfo();
};

#endif // FFMPEG_AMD_DETECTOR_H
