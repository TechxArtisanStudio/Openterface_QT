#include "gstreamer/externalgstrunner.h"
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_gst_runner_external, "opf.backend.gstreamer.runner.external")

ExternalGstRunner::ExternalGstRunner(QObject* parent)
    : QObject(parent), m_process(nullptr)
{
    m_process = new QProcess(this);
}

ExternalGstRunner::~ExternalGstRunner()
{
    if (m_process) {
        stop();
        m_process->deleteLater();
        m_process = nullptr;
    }
}

bool ExternalGstRunner::start(const QString& pipelineString, const QString& program)
{
    if (!m_process) {
        qCWarning(log_gst_runner_external) << "No QProcess available for external runner";
        return false;
    }

    if (isRunning()) {
        qCWarning(log_gst_runner_external) << "External GST process already running";
        return true;
    }

    // Split pipeline string into arguments
    QStringList arguments = pipelineString.split(' ', Qt::SkipEmptyParts);

    qCDebug(log_gst_runner_external) << "Starting external gst process (async):" << program << arguments.join(' ');
    m_process->start(program, arguments);

    // Connect signals for async notifications
    connect(m_process, &QProcess::started, this, &ExternalGstRunner::started, Qt::UniqueConnection);
    connect(m_process, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, [this](QProcess::ProcessError e){
                Q_UNUSED(e)
                QString err = m_process ? m_process->errorString() : QStringLiteral("Unknown process error");
                qCWarning(log_gst_runner_external) << "External process error:" << err;
                emit failed(err);
            }, Qt::UniqueConnection);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ExternalGstRunner::finished, Qt::UniqueConnection);

    return true; // started initiated (async)
}

bool ExternalGstRunner::start(QProcess* processOverride, const QString& pipelineString, const QString& program)
{
    if (processOverride) {
        // Use the provided process instance
        if (processOverride->state() == QProcess::Running) {
            qCWarning(log_gst_runner_external) << "Provided QProcess already running";
            return true;
        }

        QStringList arguments = pipelineString.split(' ', Qt::SkipEmptyParts);
        qCDebug(log_gst_runner_external) << "Starting external gst process (external QProcess, async):" << program << arguments.join(' ');
        processOverride->start(program, arguments);

        // Connect to provided process signals
        connect(processOverride, &QProcess::started, this, &ExternalGstRunner::started, Qt::UniqueConnection);
        connect(processOverride, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, [this, processOverride](QProcess::ProcessError){
                QString err = processOverride ? processOverride->errorString() : QStringLiteral("Unknown process error");
                qCWarning(log_gst_runner_external) << "External process (provided) error:" << err;
                emit failed(err);
            }, Qt::UniqueConnection);
        connect(processOverride, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ExternalGstRunner::finished, Qt::UniqueConnection);

        return true;
    }

    // Fallback to internal process
    return start(pipelineString, program);
}

void ExternalGstRunner::stop()
{
    if (!m_process) return;
    if (m_process->state() == QProcess::Running) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            qCDebug(log_gst_runner_external) << "External gst process killed";
        } else {
            qCDebug(log_gst_runner_external) << "External gst process terminated gracefully";
        }
    }
}

bool ExternalGstRunner::isRunning() const
{
    return (m_process && m_process->state() == QProcess::Running);
}
