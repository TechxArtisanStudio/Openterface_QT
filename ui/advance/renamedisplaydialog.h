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

#ifndef RENAMEDISPLAYDIALOG_H
#define RENAMEDISPLAYDIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QThread>
#include <QByteArray>

// Forward declarations
class VideoHid;
class FirmwareReader;
class FirmwareWriter;
class MainWindow;

class RenameDisplayDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RenameDisplayDialog(QWidget *parent = nullptr);
    ~RenameDisplayDialog();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void accept() override;
    void reject() override;
    void onUpdateButtonClicked();
    void onCancelButtonClicked();

private:
    // UI components
    QLabel *titleLabel;
    QLineEdit *displayNameLineEdit;
    QPushButton *updateButton;
    QPushButton *cancelButton;
    
    // Layout
    QVBoxLayout *mainLayout;
    QHBoxLayout *buttonLayout;
    
    // Progress dialog for firmware operations
    QProgressDialog *progressDialog;
    
    // EDID and firmware processing
    QString getCurrentDisplayName();
    bool updateDisplayName(const QString &newName);
    void stopAllDevices();
    void hideMainWindow();
    QByteArray processEDIDDisplayName(const QByteArray &firmwareData, const QString &newName);
    quint8 calculateEDIDChecksum(const QByteArray &edidBlock);
    quint16 calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID);
    int findEDIDBlock0(const QByteArray &firmwareData);
    void updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName);
    void showEDIDDescriptors(const QByteArray &edidBlock);
    void showFirmwareHexDump(const QByteArray &firmwareData, int startOffset = 0, int length = -1);
};

#endif // RENAMEDISPLAYDIALOG_H
