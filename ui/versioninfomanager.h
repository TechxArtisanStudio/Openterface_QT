#ifndef VERSIONINFOMANAGER_H
#define VERSIONINFOMANAGER_H

#include <QObject>
#include <QString>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class VersionInfoManager : public QObject
{
    Q_OBJECT

public:
    explicit VersionInfoManager(QObject *parent = nullptr);
    ~VersionInfoManager();

    void showVersionInfo();
    void showAbout();
    void checkForUpdates();

private slots:
    void copyToClipboard();
    void handleUpdateCheckResponse(QNetworkReply *reply);

private:
    const QString EMAIL = "info@techxartisan.com";
    const QString TEAM_NAME = "TechxArtisan";
    const QString ADDRESS = "No. 238, Ju De Road, Haizhu District, Guangzhou City, Guangdong Province, China";
    const QString GITHUB_REPO_API = "https://api.github.com/repos/TechxArtisan/Openterface_QT/releases/latest";

    QNetworkAccessManager *networkManager;

    QString getVersionInfoString() const;
    QString getPermissionsStatus() const;
    QString getMicrophonePermissionStatus() const;
    QString getVideoPermissionStatus() const;
    QString getEnvironmentVariables() const;
    QString getEnvironmentVariablesPlainText() const;
    void openGitHubReleasePage(const QString &releaseUrl);
};

#endif // VERSIONINFOMANAGER_H
