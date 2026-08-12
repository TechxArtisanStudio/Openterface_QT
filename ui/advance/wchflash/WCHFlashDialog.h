#pragma once

#include <QDialog>
#include <QStringList>

class QPushButton;
class QComboBox;
class QLabel;
class QProgressBar;
class QTextEdit;
class QThread;
class WCHFlashWorker;

// ---------------------------------------------------------------------------
// WCHFlashDialog
//
// Dialog for WCH ISP firmware flashing.
// Layout (top-to-bottom):
//   [Scan Devices] [Device combo] [Connect] [Disconnect]
//   --- Chip Info ---
//   [Firmware: path label] [Browse]
//   [Flash]
//   [Progress bar]
//   [Log text area]
//   [Close]
// ---------------------------------------------------------------------------
class WCHFlashDialog : public QDialog {
    Q_OBJECT

public:
    explicit WCHFlashDialog(QWidget* parent = nullptr);
    ~WCHFlashDialog() override;

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
    QPushButton* m_closeBtn      = nullptr;

    // Worker thread
    QThread*         m_thread = nullptr;
    WCHFlashWorker*  m_worker = nullptr;

    QString m_firmwarePath;
    bool    m_connected = false;
    bool    m_busy      = false;
};
