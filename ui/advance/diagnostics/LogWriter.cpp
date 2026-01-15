#include "LogWriter.h"
#include <QFile>
#include <QTextStream>

LogWriter::LogWriter(const QString& filePath, QObject* parent)
    : QObject(parent), m_filePath(filePath) {}

void LogWriter::writeLog(const QString& message) {
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << message << "\n";
        file.close();
    }
}