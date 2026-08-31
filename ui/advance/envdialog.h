#ifndef ENVIRONMENTSETUPDIALOG_H
#define ENVIRONMENTSETUPDIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include <libusb-1.0/libusb.h>
#include "video/videohid.h"

namespace Ui {
class EnvironmentSetupDialog;
}

class EnvironmentSetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EnvironmentSetupDialog(QWidget *parent = nullptr);
    ~EnvironmentSetupDialog();
    
    // Static method to check environment setup (firmware, permissions, etc.)
    static bool checkEnvironmentSetup();

    static bool autoEnvironmentCheck();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void accept() override;
    void reject() override;
    void copyCommands();
    void openHelpLink();

private:
    Ui::EnvironmentSetupDialog *ui;
    static const QString helpUrl;

    static const QString tickHtml;
    static const QString crossHtml;
    static QString latestFirewareDescription;
    static bool isDevicePlugged;
    static FirmwareResult latestFirmware;

#ifdef __linux__
    static bool checkHidPermission();
    static bool checkVideoPermission();
    static bool checkBrlttyRunning();
    static bool detectDevices(const std::vector<std::pair<uint16_t, uint16_t>>& devices);
    static bool checkPermissions(const std::vector<std::pair<uint16_t, uint16_t>>& devices, bool isSerial);

    static const QString groupCommands;
    static const QString udevCommands;
    static const QString brlttyCommands;

    static bool isInRightUserGroup;
    static bool isHidPermission;
    static bool isVideoPermission;
    static bool isSerialPermission;
    static bool isBrlttyRunning;

    QString buildCommands();
#endif
};

#endif // ENVIRONMENTSETUPDIALOG_H