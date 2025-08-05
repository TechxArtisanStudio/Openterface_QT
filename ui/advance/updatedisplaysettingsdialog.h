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

#ifndef UPDATEDISPLAYSETTINGSDIALOG_H
#define UPDATEDISPLAYSETTINGSDIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QProgressDialog>
#include <QThread>
#include <QByteArray>
#include <QGroupBox>
#include <QCheckBox>

// Forward declarations
class VideoHid;
class FirmwareReader;
class FirmwareWriter;
class MainWindow;

class UpdateDisplaySettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateDisplaySettingsDialog(QWidget *parent = nullptr);
    ~UpdateDisplaySettingsDialog();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void accept() override;
    void reject() override;
    void onUpdateButtonClicked();
    void onCancelButtonClicked();
    void onDisplayNameCheckChanged(bool checked);
    void onSerialNumberCheckChanged(bool checked);

private:
    // UI components
    QLabel *titleLabel;
    
    // Display Name Group
    QGroupBox *displayNameGroup;
    QCheckBox *displayNameCheckBox;
    QLineEdit *displayNameLineEdit;
    
    // Serial Number Group
    QGroupBox *serialNumberGroup;
    QCheckBox *serialNumberCheckBox;
    QLineEdit *serialNumberLineEdit;
    
    QPushButton *updateButton;
    QPushButton *cancelButton;
    
    // Layout
    QVBoxLayout *mainLayout;
    QVBoxLayout *displayNameLayout;
    QVBoxLayout *serialNumberLayout;
    QHBoxLayout *buttonLayout;
    
    // Progress dialog for firmware operations
    QProgressDialog *progressDialog;
    
    // EDID and firmware processing
    QString getCurrentDisplayName();
    QString getCurrentSerialNumber();
    bool updateDisplaySettings(const QString &newName, const QString &newSerial);
    void stopAllDevices();
    void hideMainWindow();
    QByteArray processEDIDDisplaySettings(const QByteArray &firmwareData, const QString &newName, const QString &newSerial);
    quint8 calculateEDIDChecksum(const QByteArray &edidBlock);
    quint16 calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID);
    int findEDIDBlock0(const QByteArray &firmwareData);
    void updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName);
    void updateEDIDSerialNumber(QByteArray &edidBlock, const QString &newSerial);
    void showEDIDDescriptors(const QByteArray &edidBlock);
    void showFirmwareHexDump(const QByteArray &firmwareData, int startOffset = 0, int length = -1);
    
    // Helper methods
    void setupUI();
    void enableUpdateButton();
};

#endif // UPDATEDISPLAYSETTINGSDIALOG_H
