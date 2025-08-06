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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QScrollArea>
#include <QSet>
#include <QProgressBar>

// Forward declarations
class VideoHid;
class FirmwareReader;
class FirmwareWriter;
class MainWindow;

// Structure to hold resolution information
struct ResolutionInfo {
    QString description;      // e.g., "1920x1080 @ 60Hz"
    int width;
    int height;
    int refreshRate;
    quint8 vic;              // Video Identification Code (for CEA-861)
    bool isStandardTiming;   // true if from standard timings, false if from extension block
    bool isEnabled;          // current state in EDID
    bool userSelected;       // user's selection in the UI
    
    ResolutionInfo() : width(0), height(0), refreshRate(0), vic(0), 
                      isStandardTiming(false), isEnabled(false), userSelected(false) {}
    
    ResolutionInfo(const QString& desc, int w, int h, int rate, quint8 v = 0, bool isStd = false) 
        : description(desc), width(w), height(h), refreshRate(rate), vic(v), 
          isStandardTiming(isStd), isEnabled(false), userSelected(false) {}
};

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
    
    // Firmware reading slots
    void onFirmwareReadProgress(int percent);
    void onFirmwareReadFinished(bool success);
    void onFirmwareReadError(const QString& errorMessage);
    void onCancelReadingClicked();

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
    
    // Resolution Table Group
    QGroupBox *resolutionGroup;
    QTableWidget *resolutionTable;
    QPushButton *selectAllButton;
    QPushButton *selectNoneButton;
    QPushButton *selectDefaultButton;
    
    QPushButton *updateButton;
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
    
    // Progress dialog for firmware operations (kept for compatibility)
    QProgressDialog *progressDialog;
    
    // Threading for firmware reading
    QThread *firmwareReaderThread;
    FirmwareReader *firmwareReader;
    bool m_cleanupInProgress;  // Flag to prevent double cleanup
    
    // Resolution data
    QList<ResolutionInfo> availableResolutions;
    
    // EDID and firmware processing
    QString getCurrentDisplayName();
    QString getCurrentSerialNumber();
    void loadCurrentEDIDSettings();
    void parseEDIDDescriptors(const QByteArray &edidBlock, QString &displayName, QString &serialNumber);
    void logSupportedResolutions(const QByteArray &edidBlock);
    void parseEDIDExtensionBlocks(const QByteArray &firmwareData, int baseBlockOffset);
    void parseCEA861ExtensionBlock(const QByteArray &block, int blockNumber);
    void parseVideoTimingExtensionBlock(const QByteArray &block, int blockNumber);
    void parseVideoDataBlock(const QByteArray &vdbData);
    QString getVICResolution(quint8 vic);
    bool updateDisplaySettings(const QString &newName, const QString &newSerial);
    void stopAllDevices();
    void hideMainWindow();
    QByteArray processEDIDDisplaySettings(const QByteArray &firmwareData, const QString &newName, const QString &newSerial);
    quint8 calculateEDIDChecksum(const QByteArray &edidBlock);
    quint16 calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID);
    quint16 calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &modifiedFirmware);
    int findEDIDBlock0(const QByteArray &firmwareData);
    void updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName);
    void updateEDIDSerialNumber(QByteArray &edidBlock, const QString &newSerial);
    void showEDIDDescriptors(const QByteArray &edidBlock);
    void showFirmwareHexDump(const QByteArray &firmwareData, int startOffset = 0, int length = -1);
    
    // Resolution management
    void setupResolutionTable();
    void populateResolutionTable();
    void updateResolutionTableFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData, int baseOffset);
    void addResolutionToList(const QString& description, int width, int height, int refreshRate, 
                           quint8 vic = 0, bool isStandardTiming = false, bool isEnabled = false);
    void parseStandardTimingsForResolutions(const QByteArray &edidBlock);
    void parseDetailedTimingDescriptorsForResolutions(const QByteArray &edidBlock);
    void parseExtensionBlocksForResolutions(const QByteArray &firmwareData, int baseOffset);
    void parseCEA861ExtensionBlockForResolutions(const QByteArray &block, int blockNumber);
    void parseVideoDataBlockForResolutions(const QByteArray &dataBlockCollection);
    ResolutionInfo getVICResolutionInfo(quint8 vic);
    QList<ResolutionInfo> getSelectedResolutions() const;
    void applyResolutionChangesToEDID(QByteArray &edidBlock, const QByteArray &firmwareData);
    void updateExtensionBlockResolutions(QByteArray &firmwareData, int edidOffset);
    bool updateCEA861ExtensionBlockResolutions(QByteArray &block, const QSet<quint8> &enabledVICs, const QSet<quint8> &disabledVICs);
    bool hasResolutionChanges() const;
    
    // Helper methods
    void setupUI();
    void enableUpdateButton();
    void cleanupFirmwareReaderThread();
};

#endif // UPDATEDISPLAYSETTINGSDIALOG_H
