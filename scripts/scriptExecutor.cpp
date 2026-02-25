#include "scriptExecutor.h"
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_scriptexec, "opf.ui.scriptexec")

ScriptExecutor::ScriptExecutor(QObject* parent)
    : QObject(parent)
{
    qCDebug(log_scriptexec) << "ScriptExecutor initialized as signal router";
}