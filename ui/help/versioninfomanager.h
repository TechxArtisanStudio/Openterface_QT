/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

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
