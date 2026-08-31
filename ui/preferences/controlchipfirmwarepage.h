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

#ifndef CONTROLCHIPFIRMWAREPAGE_H
#define CONTROLCHIPFIRMWAREPAGE_H

#include <QWidget>
#include <QStringList>

class QPushButton;
class QComboBox;
class QLabel;
class QProgressBar;
class QTextEdit;
class QThread;
class WCHFlashWorker;

// ---------------------------------------------------------------------------
// ControlChipFirmwarePage
//
// Widget for WCH ISP firmware flashing (embedded in AdvancedSettingsDialog).
// Layout (top-to-bottom):
//   [Scan Devices] [Device combo] [Connect] [Disconnect]
//   --- Chip Info ---
//   [Firmware: path label] [Browse]
//   [Flash]
//   [Progress bar]
//   [Log text area]
// ---------------------------------------------------------------------------
class ControlChipFirmwarePage : public QWidget {
    Q_OBJECT

public:
    explicit ControlChipFirmwarePage(QWidget* parent = nullptr);
    ~ControlChipFirmwarePage() override;

private slots:
    // Worker signal handlers
    void onDevicesFound(const QStringList& devices);
    void onDeviceConnected(const QString& chipInfo);
    void onDeviceDisconnected();
    void onProgress(int percent, const QString& message);
    void onFinished(bool success, const QString& message);
    void onLogMessage(const QString& message);

    // UI button handlers
    void onScanClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onBrowseClicked();
    void onFlashClicked();

private:
    void setupUi();
    void setConnectedState(bool connected);
    void updateFlashButton();
    void appendLog(const QString& text);

    // UI elements
    QPushButton* m_scanBtn       = nullptr;
    QComboBox*   m_deviceCombo   = nullptr;
    QPushButton* m_connectBtn    = nullptr;
    QPushButton* m_disconnectBtn = nullptr;
    QLabel*      m_chipInfoLabel = nullptr;
    QLabel*      m_firmwareLabel = nullptr;
    QPushButton* m_browseBtn     = nullptr;
    QPushButton* m_flashBtn      = nullptr;
    QProgressBar* m_progressBar  = nullptr;
    QTextEdit*   m_logEdit       = nullptr;

    // Worker thread
    QThread*         m_thread = nullptr;
    WCHFlashWorker*  m_worker = nullptr;

    QString m_firmwarePath;
    bool    m_connected = false;
    bool    m_busy      = false;
};

#endif // CONTROLCHIPFIRMWAREPAGE_H
