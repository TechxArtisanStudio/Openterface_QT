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
#include <QThread>
#include <QByteArray>
#include <QGroupBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QScrollArea>
#include <QSet>
#include <QProgressBar>
#include <functional>
#include "edid/resolutionmodel.h"
#include "edid/edidprocessor.h"

// Forward declarations
class VideoHid;
class FirmwareOperationManager;
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
    
    // Resolution table slots
    void onSelectAllResolutions();
    void onSelectNoneResolutions();
    void onSelectDefaultResolutions();
    void onResolutionItemChanged(QTableWidgetItem* item);
    void setAllResolutionSelection(bool enable);
    
    // Firmware reading slots
    void onFirmwareReadProgress(int percent);
    void onFirmwareReadFinished(bool success);
    void onFirmwareReadError(const QString& errorMessage);
    void onCancelReadingClicked();
    void onBackupFirmwareClicked();

private:
    // UI components
    // Display Name Group
    QGroupBox *displayNameGroup;
    QCheckBox *displayNameCheckBox;
    QLineEdit *displayNameLineEdit;
    
    // Serial Number Group
    QGroupBox *serialNumberGroup;
    QCheckBox *serialNumberCheckBox;
    QLineEdit *serialNumberLineEdit;
    
    // Resolution Table Group
    QGroupBox *resolutionGroup;
    QTableWidget *resolutionTable;
    QPushButton *selectAllButton;
    QPushButton *selectNoneButton;
    QPushButton *selectDefaultButton;
    
    QPushButton *updateButton;
    QPushButton *backupButton;
    QPushButton *cancelButton;
    
    // Progress components for firmware reading
    QGroupBox *progressGroup;
    QProgressBar *progressBar;
    QLabel *progressLabel;
    QPushButton *cancelReadingButton;
    
    // Layout
    QVBoxLayout *mainLayout;
    QVBoxLayout *displayNameLayout;
    QVBoxLayout *serialNumberLayout;
    QHBoxLayout *buttonLayout;
    
    // Firmware operation manager
    class FirmwareOperationManager *firmwareOperationManager;
    QString m_tempFirmwarePath;
    bool m_operationFinished;  // Flag to avoid cancel handling after success/quit
    bool m_updateMode;        // Indicates read is part of update flow

    // Resolution data
    ResolutionModel resolutionModel;
    QByteArray m_pendingFirmwareData;
    
    // EDID and firmware processing
    void loadCurrentEDIDSettings();
    bool updateDisplaySettings(const QString &newName, const QString &newSerial);
    void restartPollingDelayed(const QString &reason);
    void showErrorAndRestart(const QString &title, const QString &message, const QString &reason);
    void stopAllDevices();
    void hideMainWindow();
    
    // Resolution helpers
    void setupResolutionTable();
    void updateExtensionBlockResolutions(QByteArray &firmwareData, int edidOffset);
    bool updateCEA861ExtensionBlockResolutions(QByteArray &block, const QSet<quint8> &enabledVICs, const QSet<quint8> &disabledVICs);
    
    // Helpers for resolution state
    void populateResolutionTableFromModel();
    void readResolutionFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData);
    QList<ResolutionInfo> getSelectedResolutions() const;
    bool hasResolutionChanges() const;
    
    // Helper methods
    void setupUI();
    void ensureFirmwareOperationManager();
    QGroupBox* buildSettingsSection(QCheckBox *&checkBox, QLineEdit *&lineEdit, QVBoxLayout *&sectionLayout,
                                    const QString &title, const QString &checkboxText, const QString &placeholderText);
    void buildProgressSection();
    void buildButtonSection();
    void connectUiSignals();
    void enableUpdateButton();
    void setDialogControlsEnabled(bool enabled);
    void setProgressState(bool active, const QString &labelText);
    bool validateAsciiInput(const QString &text, int maxLen, const QString &fieldName, QString &errorMessage) const;
    bool collectUpdateChanges(QString &newName, QString &newSerial, QStringList &changesSummary) const;
    void shutdownFirmwareOperation(bool closeDialog = false);
    bool processAndWriteFirmware();

    bool processFirmwareData(const QByteArray &firmwareData);
    void processFirmwareReadResult(bool success);
    bool parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const;
};

#endif // UPDATEDISPLAYSETTINGSDIALOG_H
