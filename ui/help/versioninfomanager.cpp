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
#include <QCheckBox>
#include <QLayout>
#include <QWidget>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QDialog>
#include <QLabel>
#include <QDialogButtonBox>
#include <QToolTip>

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

#include <QDateTime>
#include "../globalsetting.h"

void VersionInfoManager::checkForUpdates(bool force)
{
    GlobalSetting &gs = GlobalSetting::instance();

    if (!force) {
        if (gs.getUpdateNeverRemind()) {
            qDebug() << "Update check skipped: user chose 'never remind'";
            return;
        }
        qint64 last = gs.getUpdateLastChecked();
        qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 THIRTY_DAYS = 30LL * 24 * 3600;
        if (last > 0 && (now - last) < THIRTY_DAYS) {
            qDebug() << "Update check skipped: last checked" << (now - last) << "seconds ago";
            return;
        }
    }

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
    GlobalSetting &gs = GlobalSetting::instance();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject releaseInfo = doc.object();

        QString latestVersion = releaseInfo["tag_name"].toString();
        QString currentVersion = QApplication::applicationVersion();
        QString htmlUrl = releaseInfo["html_url"].toString();
        QString releaseBody = releaseInfo["body"].toString();
        QString releaseName = releaseInfo["name"].toString();

        latestVersion.remove(QRegularExpression("^v"));
        currentVersion.remove(QRegularExpression("^v"));

        QList<int> latestSegments = QVersionNumber::fromString(latestVersion).normalized().segments();
        QList<int> currentSegments = QVersionNumber::fromString(currentVersion).normalized().segments();

        while (latestSegments.size() > 3) latestSegments.removeLast();
        while (currentSegments.size() > 3) currentSegments.removeLast();

        QVersionNumber latest = QVersionNumber(latestSegments);
        QVersionNumber current = QVersionNumber(currentSegments);

        /* use a custom dialog so the visual order is strictly: message -> checkboxes (vertical) -> buttons */
        QDialog dlg;
        dlg.setWindowTitle(tr("Openterface Mini KVM"));
        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        
        // Get system colors for consistent theming
        QColor base = dlg.palette().color(QPalette::Window);
        QColor textColor(255 - base.red(), 255 - base.green(), 255 - base.blue());

        if (latest != current) {
            // message label
            QString messageText = tr("A new version is available!\nCurrent version: %1\nLatest version: %2\n").arg(currentVersion).arg(latestVersion);
            
            // Add release title if available
            if (!releaseName.isEmpty()) {
                messageText += "\n" + tr("Release: %1").arg(releaseName) + "\n";
            }
            
            // Add first part of release notes (first 200 characters)
            if (!releaseBody.isEmpty()) {
                QString releasePreview = releaseBody.left(200);
                if (releaseBody.length() > 200) {
                    releasePreview += "...";
                }
                // Remove markdown formatting for plain text display
                releasePreview = releasePreview.remove(QRegularExpression("[#*`_\\[\\]()]"));
                messageText += "\n" + tr("What's new:") + "\n" + releasePreview;
            }
            
            QLabel *label = new QLabel(messageText, &dlg);
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            dlgLayout->addWidget(label, 0, Qt::AlignTop);

            // show release page URL (clickable) + copy button so user can jump/download
            QString releaseUrl = htmlUrl;
            if (releaseUrl.isEmpty()) {
                // fallback to the releases web page for this repository
                releaseUrl = QStringLiteral("https://github.com/TechxArtisanStudio/Openterface_QT/releases");
            }

            // concise, user-friendly link text
            QLabel *linkLabel = new QLabel(&dlg);
            QString linkHtml = QString("<a href=\"%1\" style=\"color:%2; text-decoration: underline;\">%3</a>")
                                   .arg(releaseUrl).arg(textColor.name()).arg(tr("Go to download new version"));
            linkLabel->setText(linkHtml);
            linkLabel->setTextFormat(Qt::RichText);
            linkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
            linkLabel->setOpenExternalLinks(true);
            linkLabel->setToolTip(releaseUrl);
            linkLabel->setAlignment(Qt::AlignHCenter);
            linkLabel->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            // inline CSS on the anchor ensures the link text color is respected regardless of style/theme

            // copy button placed on the next line (centered) for cleaner layout
            QPushButton *copyUrlBtn = new QPushButton(tr("Copy URL"), &dlg);
            copyUrlBtn->setToolTip(tr("Copy release page URL to clipboard"));
            copyUrlBtn->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            connect(copyUrlBtn, &QPushButton::clicked, &dlg, [releaseUrl, copyUrlBtn]() {
                QClipboard *cb = QApplication::clipboard();
                cb->setText(releaseUrl, QClipboard::Clipboard);
#if defined(Q_OS_WIN)
                cb->setText(releaseUrl, QClipboard::Selection);
#endif
                QToolTip::showText(copyUrlBtn->mapToGlobal(QPoint(copyUrlBtn->width()/2, copyUrlBtn->height()/2)),
                                   QObject::tr("Copied to clipboard"), copyUrlBtn, QRect(), 1200);
            });

            QWidget *linkRow = new QWidget(&dlg);
            QVBoxLayout *linkLayout = new QVBoxLayout(linkRow);
            linkLayout->setContentsMargins(12, 6, 12, 0);
            linkLayout->setSpacing(6);
            linkLayout->addWidget(linkLabel, 0, Qt::AlignHCenter);
            linkLayout->addWidget(copyUrlBtn, 0, Qt::AlignHCenter);
            linkRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            dlgLayout->addWidget(linkRow, 0, Qt::AlignHCenter);

            // reminder controls
            QCheckBox *remindCheck = new QCheckBox(tr("Remind me in 1 month"), &dlg);
            QCheckBox *neverCheck = new QCheckBox(tr("Never remind me"), &dlg);
            remindCheck->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            neverCheck->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            connect(neverCheck, &QCheckBox::toggled, remindCheck, [remindCheck](bool on){ remindCheck->setChecked(false); remindCheck->setEnabled(!on); });
            connect(remindCheck, &QCheckBox::toggled, neverCheck, [neverCheck](bool on){ if (on) neverCheck->setChecked(false); });

            QWidget *checkboxContainer = new QWidget(&dlg);
            QVBoxLayout *vbox = new QVBoxLayout(checkboxContainer);
            vbox->setContentsMargins(12, 8, 12, 4);
            vbox->setSpacing(6);
            vbox->addWidget(remindCheck);
            vbox->addWidget(neverCheck);
            checkboxContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            dlgLayout->addWidget(checkboxContainer, 0, Qt::AlignHCenter);

            // buttons
            QDialogButtonBox *buttonBox = new QDialogButtonBox(&dlg);
            QPushButton *updateBtn = new QPushButton(tr("Update"), buttonBox);
            buttonBox->addButton(updateBtn, QDialogButtonBox::AcceptRole);
            buttonBox->addButton(QDialogButtonBox::Cancel);

            bool updateRequested = false;
            connect(updateBtn, &QPushButton::clicked, &dlg, [&updateRequested, &dlg](){ updateRequested = true; dlg.accept(); });
            connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

            dlgLayout->addWidget(buttonBox);

            // exec and persist choices only when user confirms
            if (dlg.exec() == QDialog::Accepted) {
                if (neverCheck->isChecked()) {
                    gs.setUpdateNeverRemind(true);
                } else {
                    gs.setUpdateNeverRemind(false);
                    if (remindCheck->isChecked()) {
                        gs.setUpdateLastChecked(now); // remind in 30 days
                    } else {
                        gs.setUpdateLastChecked(now); // record check time
                    }
                }

                if (updateRequested) {
                    openGitHubReleasePage(htmlUrl);
                }
            } else {
                // user canceled: record check time to avoid immediate re-prompt
                gs.setUpdateLastChecked(now);
            }
        } else {
            // up-to-date flow: message -> checkboxes -> OK
            QString upToDateText = tr("You are using the latest version — Current version: %1").arg(currentVersion);
            QLabel *label = new QLabel(upToDateText, &dlg);
            label->setWordWrap(true);
            label->setTextFormat(Qt::PlainText);
            label->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            dlgLayout->addWidget(label, 0, Qt::AlignTop);

            QCheckBox *remindCheck = new QCheckBox(tr("Remind me in 1 month"), &dlg);
            QCheckBox *neverCheck = new QCheckBox(tr("Never remind me"), &dlg);
            remindCheck->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            neverCheck->setStyleSheet(QString("color: %1;").arg(textColor.name()));
            connect(neverCheck, &QCheckBox::toggled, remindCheck, [remindCheck](bool on){ remindCheck->setChecked(false); remindCheck->setEnabled(!on); });
            connect(remindCheck, &QCheckBox::toggled, neverCheck, [neverCheck](bool on){ if (on) neverCheck->setChecked(false); });

            QWidget *checkboxContainer = new QWidget(&dlg);
            QVBoxLayout *vbox = new QVBoxLayout(checkboxContainer);
            vbox->setContentsMargins(12, 8, 12, 4);
            vbox->setSpacing(6);
            vbox->addWidget(remindCheck);
            vbox->addWidget(neverCheck);
            checkboxContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            dlgLayout->addWidget(checkboxContainer, 0, Qt::AlignHCenter);

            QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
            connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            dlgLayout->addWidget(buttonBox);

            // record check time now (throttle) and apply preference only if user confirms
            gs.setUpdateLastChecked(now);
            if (dlg.exec() == QDialog::Accepted) {
                if (neverCheck->isChecked()) {
                    gs.setUpdateNeverRemind(true);
                } else {
                    gs.setUpdateNeverRemind(false);
                    if (remindCheck->isChecked()) {
                        gs.setUpdateLastChecked(now); // remind in 30 days
                    }
                }
            }
        }
    } else {
        qDebug() << "Update check failed:" << reply->errorString();
        // record the failed check to avoid tight retry loops
        gs.setUpdateLastChecked(QDateTime::currentSecsSinceEpoch());
    }
}

void VersionInfoManager::openGitHubReleasePage(const QString &releaseUrl)
{
    QDesktopServices::openUrl(QUrl(releaseUrl));
}
