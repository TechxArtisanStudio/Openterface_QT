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

#ifndef FIRMWAREPAGE_H
#define FIRMWAREPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QGroupBox>
#include <QThread>

class FirmwarePage : public QWidget
{
    Q_OBJECT

public:
    explicit FirmwarePage(QWidget *parent = nullptr);
    ~FirmwarePage();

    void updateVersionDisplay();

signals:
    void firmwareUpdateCompleted();

private slots:
    void onCheckForUpdatesClicked();
    void onBackupFirmwareClicked();
    void onWriteFirmwareClicked();
    void onProgressUpdate(int value);
    void onOperationComplete(bool success);
    void onCancelClicked();
    void onLatestVersionFetched();

private:
    enum OperationType { None, Update, Backup, Write };

    // UI components
    QLabel *versionLabel;
    QLabel *latestVersionLabel;
    QPushButton *updateButton;
    QPushButton *backupButton;
    QPushButton *writeButton;
    QProgressBar *progressBar;
    QLabel *statusLabel;
    QPushButton *cancelButton;

    // State
    OperationType currentOperation;
    QThread *workerThread;

    void setupUI();
    void startOperation(OperationType type);
    void finishOperation(bool success);
    void fetchLatestVersionAsync();
    QByteArray readBinFileToByteArray(const QString &filePath);
    QString selectFirmwareFile();
    QString selectSavePath();
};

#endif // FIRMWAREPAGE_H
