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

#ifndef EDIDCONFIGPAGE_H
#define EDIDCONFIGPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QProgressBar>
#include <QByteArray>
#include <QString>

class VideoHid;
namespace edid { class EdidSettingsManager; }

class EdidConfigPage : public QWidget
{
    Q_OBJECT

public:
    explicit EdidConfigPage(QWidget *parent = nullptr);
    ~EdidConfigPage();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onDisplayNameCheckChanged(bool checked);
    void onApplyButtonClicked();
    void onCancelReadingClicked();
    void onReadButtonClicked();

    // Firmware operation signal handlers
    void onFirmwareReadProgress(int percent);
    void onFirmwareReadFinished(bool success);
    void onFirmwareReadError(const QString &errorMessage);
    void onFirmwareWriteFinished(bool success);

private:
    // ---- UI widgets ----
    QGroupBox *deviceStatusGroup;
    QLabel *deviceStatusIconLabel;
    QLabel *deviceStatusLabel;
    QPushButton *readButton;

    QGroupBox *displayNameGroup;
    QLabel *currentNameLabel;
    QCheckBox *displayNameCheckBox;
    QLineEdit *displayNameLineEdit;
    QPushButton *applyButton;

    QGroupBox *progressGroup;
    QProgressBar *progressBar;
    QLabel *progressLabel;
    QPushButton *cancelReadingButton;

    QLabel *infoLabel;

    // ---- EDID read/apply orchestration (shared with the dialog and MCP) ----
    edid::EdidSettingsManager *m_edidManager;
    QByteArray m_pendingFirmwareData;
    bool m_operationFinished;
    bool m_updateMode;

    // ---- UI construction ----
    void setupUI();
    void buildDeviceStatusSection();
    void buildDisplayNameSection();
    void buildProgressSection();
    void buildButtonSection();
    void connectUiSignals();

    // ---- Device status ----
    void updateDeviceStatus();

    // ---- Firmware/EDID logic (ported from UpdateDisplaySettingsDialog) ----
    void ensureEdidManager();
    void loadCurrentEDIDSettings();
    void processFirmwareReadResult(bool success);
    bool processFirmwareData(const QByteArray &firmwareData);
    bool parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const;
    bool updateDisplayName(const QString &newName);

    // ---- State helpers ----
    void enableApplyButton();
    void setControlsEnabled(bool enabled);
    void setProgressState(bool active, const QString &labelText);
    bool validateAsciiInput(const QString &text, int maxLen, const QString &fieldName, QString &errorMessage) const;
    void shutdownFirmwareOperation();
    void restartPollingDelayed(const QString &reason);
    void showErrorAndRestart(const QString &title, const QString &message, const QString &reason);
    void stopAllDevices();
};

#endif // EDIDCONFIGPAGE_H
