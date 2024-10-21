#ifndef VERSIONINFOMANAGER_H
#define VERSIONINFOMANAGER_H

#include <QObject>
#include <QString>
#include <QMessageBox>

class VersionInfoManager : public QObject
{
    Q_OBJECT

public:
    explicit VersionInfoManager(QObject *parent = nullptr);

    void showVersionInfo();

private slots:
    void copyToClipboard();

private:
    QString getVersionInfoString() const;
    QString getPermissionsStatus() const;
    QString getMicrophonePermissionStatus() const;
    QString getVideoPermissionStatus() const;
    QString getEnvironmentVariables() const;
    QString getEnvironmentVariablesPlainText() const;
};

#endif // VERSIONINFOMANAGER_H
