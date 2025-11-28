/*
 * External gst-launch runner (QProcess-based)
 */
#ifndef EXTERNALGSTRUNNER_H
#define EXTERNALGSTRUNNER_H

#include <QObject>
#include <QProcess>

class ExternalGstRunner : public QObject
{
    Q_OBJECT

public:
    explicit ExternalGstRunner(QObject* parent = nullptr);
    ~ExternalGstRunner();

    // Start/stop for external gst-launch. start() triggers the process asynchronously
    // and returns true if start was initiated successfully.
    bool start(const QString& pipelineString, const QString& program = QStringLiteral("gst-launch-1.0"));
    // Start using an existing QProcess instance (handler's m_gstProcess) if provided
    bool start(QProcess* processOverride, const QString& pipelineString, const QString& program = QStringLiteral("gst-launch-1.0"));
    void stop();

    bool isRunning() const;

private:
    QProcess* m_process;
signals:
    void started();
    void failed(const QString& error);
    void finished(int exitCode, QProcess::ExitStatus status);
};

#endif // EXTERNALGSTRUNNER_H
