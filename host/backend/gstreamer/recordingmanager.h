/*
 * RecordingManager - encapsulates recording logic previously inside GStreamerBackendHandler
 */
#ifndef RECORDINGMANAGER_H
#define RECORDINGMANAGER_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QProcess>
#include <QFileInfo>
#include <QDir>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif

class RecordingManager : public QObject
{
    Q_OBJECT

public:
    explicit RecordingManager(QObject* parent = nullptr);
    ~RecordingManager();

    // Primary recording API. The pipeline pointer is the main pipeline owned by the handler.
    bool startRecording(GstElement* mainPipeline, const QString& outputPath, const QString& format, int videoBitrate);
    bool stopRecording();
    void pauseRecording();
    void resumeRecording();

    bool isRecording() const;
    QString getCurrentRecordingPath() const;
    qint64 getRecordingDuration() const;

    // Configure recording behavior
    void setRecordingConfig(const QString& codec, const QString& format, int bitrate);

signals:
    void recordingStarted(const QString& path);
    void recordingStopped();
    void recordingPaused();
    void recordingResumed();
    void recordingError(const QString& error);

public slots:
    void handleRecordingProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleRecordingProcessError(QProcess::ProcessError error);

private:
    // Internal state
    GstElement* m_mainPipeline;

    // Recording pipeline / branch elements
#ifdef HAVE_GSTREAMER
    GstElement* m_recordingPipeline;
    GstElement* m_recordingTee;
    GstElement* m_recordingValve;
    GstElement* m_recordingSink;
    GstElement* m_recordingQueue;
    GstElement* m_recordingEncoder;
    GstElement* m_recordingVideoConvert;
    GstElement* m_recordingMuxer;
    GstElement* m_recordingFileSink;
    GstElement* m_recordingAppSink;
    GstPad* m_recordingTeeSrcPad;
#endif

    bool m_recordingActive;
    bool m_recordingPaused;
    QString m_recordingOutputPath;
    int m_recordingVideoBitrate;
    qint64 m_recordingStartTime;
    qint64 m_recordingPausedTime;
    qint64 m_totalPausedDuration;
    int m_recordingFrameNumber;

    // Helpers
    bool initializeValveBasedRecording(const QString& format);
    bool initializeFrameBasedRecording(const QString& format);
    // Fallbacks and helpers for recording branch creation
    bool createSeparateRecordingPipeline(const QString& outputPath, const QString& format, int videoBitrate);
    bool initializeDirectFilesinkRecording(const QString& outputPath, const QString& format);
    // FFmpeg process and appsink integration for frame-based recording
    QProcess* m_recordingProcess = nullptr;
    QTimer* m_frameCaptureTimer = nullptr;
    QString m_recordingFormat;
    bool createRecordingBranch(const QString& outputPath, const QString& format, int videoBitrate);
    void removeRecordingBranch();
    QString computeAdjustedOutputPath(const QString& outputPath, bool directMJPEG);
};

#endif // RECORDINGMANAGER_H
