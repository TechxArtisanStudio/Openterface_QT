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


#include "versioninfomanager.h"
#include <QApplication>
#include <QClipboard>
#include <QSysInfo>
#include <QPushButton>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QRegularExpression>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QCameraDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QVersionNumber>

VersionInfoManager::VersionInfoManager(QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
{
}

VersionInfoManager::~VersionInfoManager()
{
}

void VersionInfoManager::showAbout()
{
    QString message = QString("<b>Email:</b> %1<br><b>Company:</b> %2<br><b>Address:</b> %3")
        .arg(EMAIL)
        .arg(TEAM_NAME)
        .arg(ADDRESS);
    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("%1").arg(QApplication::applicationName()));
    msgBox.setText(message);
    msgBox.setTextFormat(Qt::RichText);
    msgBox.exec();
}

void VersionInfoManager::showVersionInfo()
{
    QString applicationName = QApplication::applicationName();
    QString message = getVersionInfoString() + "<br><br>" + 
                      getPermissionsStatus() + "<br><br>" + 
                      getEnvironmentVariables();

    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("%1").arg(applicationName));
    msgBox.setText(message);
    msgBox.setTextFormat(Qt::RichText);

    QPushButton *copyButton = msgBox.addButton(tr("Copy"), QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Close);

    connect(copyButton, &QPushButton::clicked, this, &VersionInfoManager::copyToClipboard);

    msgBox.exec();

    if (msgBox.clickedButton() == copyButton) {
        copyToClipboard();
    }
}

void VersionInfoManager::copyToClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString clipboardText = getVersionInfoString().remove(QRegularExpression("<[^>]*>")) + "\n\n" +
                            getPermissionsStatus().remove(QRegularExpression("<[^>]*>")) + "\n\n" +
                            getEnvironmentVariablesPlainText();
    clipboard->setText(clipboardText);
}

QString VersionInfoManager::getVersionInfoString() const
{
    QString applicationVersion = QApplication::applicationVersion();
    QString osVersion = QSysInfo::prettyProductName();

    return QString("<b>App:</b> %1<br><b>OS:</b> %2<br><b>QT:</b> %3")
        .arg(applicationVersion)
        .arg(osVersion)
        .arg(qVersion());
}

QString VersionInfoManager::getPermissionsStatus() const
{
    QString micPermission = getMicrophonePermissionStatus();
    QString videoPermission = getVideoPermissionStatus();

    return QString("<b>Permissions:</b><br>"
                   "<table border='1' cellspacing='0' cellpadding='5'>"
                   "<tr><td>Microphone</td><td>%1</td></tr>"
                   "<tr><td>Video</td><td>%2</td></tr>"
                   "</table>")
        .arg(micPermission)
        .arg(videoPermission);
}

QString VersionInfoManager::getEnvironmentVariables() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString envInfo;
    QTextStream stream(&envInfo);

    QStringList importantVars = {"QT_QPA_PLATFORM", "XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "DISPLAY"};

    stream << "<b>Environment Variables:</b><br>";
    stream << "<table border='1' cellspacing='0' cellpadding='5'>";
    stream << "<tr><th>Variable</th><th>Value</th></tr>";

    for (const QString &var : importantVars) {
        stream << QString("<tr><td>%1</td><td>%2</td></tr>").arg(var, env.value(var, "(not set)"));
    }

    stream << "</table>";

    return envInfo;
}

QString VersionInfoManager::getEnvironmentVariablesPlainText() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString envInfo;
    QTextStream stream(&envInfo);

    QStringList importantVars = {"QT_QPA_PLATFORM", "XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "DISPLAY"};

    stream << "Environment Variables:\n";
    for (const QString &var : importantVars) {
        stream << QString("%1: %2\n").arg(var, env.value(var, "(not set)"));
    }

    return envInfo;
}

QString VersionInfoManager::getMicrophonePermissionStatus() const
{
    QList<QAudioDevice> audioDevices = QMediaDevices::audioInputs();
    return audioDevices.isEmpty() ? "Not available or permission not granted" : "Available";
}

QString VersionInfoManager::getVideoPermissionStatus() const
{
    QList<QCameraDevice> videoDevices = QMediaDevices::videoInputs();
    return videoDevices.isEmpty() ? "Not available or permission not granted" : "Available";
}

void VersionInfoManager::checkForUpdates()
{
    QNetworkRequest request((QUrl(GITHUB_REPO_API)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Openterface_QT Update Checker");
    
    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        handleUpdateCheckResponse(reply);
        reply->deleteLater();
    });
}

void VersionInfoManager::handleUpdateCheckResponse(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject releaseInfo = doc.object();
        
        QString latestVersion = releaseInfo["tag_name"].toString();
        QString currentVersion = QApplication::applicationVersion();
        QString htmlUrl = releaseInfo["html_url"].toString();
        
        // Remove 'v' prefix if present
        latestVersion.remove(QRegularExpression("^v"));
        currentVersion.remove(QRegularExpression("^v"));
        
        // just compare the first two segments
        QList<int> latestSegments = QVersionNumber::fromString(latestVersion).normalized().segments();
        QList<int> currentSegments = QVersionNumber::fromString(currentVersion).normalized().segments();

        while (latestSegments.size() > 3) latestSegments.removeLast();
        while (currentSegments.size() > 3) currentSegments.removeLast();

        QVersionNumber latest = QVersionNumber(latestSegments);
        QVersionNumber current = QVersionNumber(currentSegments);

        qDebug() << "version latest (first two segments): " << latest;
        qDebug() << "version current (first two segments): " << current;
        
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Openterface Mini KVM"));
        
        if (latest != current) {
            msgBox.setText(tr("A new version is available!\nCurrent version: %1\nLatest version: %2\n")
                          .arg(currentVersion)
                          .arg(latestVersion));
            QPushButton *updateButton = msgBox.addButton(tr("Update"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
            
            msgBox.exec();
            
            if (msgBox.clickedButton() == updateButton) {
                openGitHubReleasePage(htmlUrl);
            }
        } else {
            msgBox.setText(tr("You are using the latest version"));
            msgBox.addButton(QMessageBox::Ok);
            msgBox.exec();
        }
    } else {
        QMessageBox::warning(nullptr, 
                           tr("Update Check Failed\n"),
                           tr("Failed to check for updates.\nError: %1\nPlease check your internet connection.\n")
                           .arg(reply->errorString()));
    }
}

void VersionInfoManager::openGitHubReleasePage(const QString &releaseUrl)
{
    QDesktopServices::openUrl(QUrl(releaseUrl));
}
