#ifndef VERSION_H
#define VERSION_H

#include <string>
#include <fstream>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDebug>

inline QString getAppVersion() {
    QString version = "0.0.0"; // Default version
    QFile file("version.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        version = in.readLine().trimmed();
        file.close();
    } else {
        qWarning() << "Unable to read version.txt. Using default version.";
    }
    return version;
}

#define APP_VERSION getAppVersion()

#endif // VERSION_H