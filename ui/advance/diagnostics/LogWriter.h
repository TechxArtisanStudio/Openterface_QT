#ifndef LOGWRITER_H
#define LOGWRITER_H

#include <QObject>
#include <QString>

class LogWriter : public QObject {
    Q_OBJECT
public:
    explicit LogWriter(const QString& filePath, QObject* parent = nullptr);

public slots:
    void writeLog(const QString& message);
    void setFilePath(const QString& filePath);

private:
    QString m_filePath;
};

#endif // LOGWRITER_H