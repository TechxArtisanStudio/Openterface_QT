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
    void showAbout();
private slots:
    void copyToClipboard();

private:
    const QString EMAIL = "info@techxartisan.com";
    const QString TEAM_NAME = "TechxArtisan";
    const QString ADDRESS = "No. 238, Ju De Road, Haizhu District, Guangzhou City, Guangdong Province, China";

    QString getVersionInfoString() const;
    QString getPermissionsStatus() const;
    QString getMicrophonePermissionStatus() const;
    QString getVideoPermissionStatus() const;
    QString getEnvironmentVariables() const;
    QString getEnvironmentVariablesPlainText() const;
};

#endif // VERSIONINFOMANAGER_H
