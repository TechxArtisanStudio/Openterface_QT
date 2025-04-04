#ifndef ENVIRONMENTSETUPDIALOG_H
#define ENVIRONMENTSETUPDIALOG_H

#include <QDialog>
#include <QCloseEvent>

namespace Ui {
class EnvironmentSetupDialog;
}

class EnvironmentSetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EnvironmentSetupDialog(QWidget *parent = nullptr);
    ~EnvironmentSetupDialog();
    
    // Static method to check if the CH340 driver is installed
    static bool checkEnvironmentSetup();

    static bool autoEnvironmentCheck();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void accept() override;
    void reject() override;
    void extractDriverFiles();
    void copyCommands();
    void openHelpLink();
#ifdef _WIN32
    void installDriverForWindows();
#endif

private:
    Ui::EnvironmentSetupDialog *ui;
    static bool checkDriverInstalled();
    static const QString helpUrl;

    static bool isDriverInstalled;
    static const QString tickHtml;
    static const QString crossHtml;
    bool isDevicePlugged;
    
#ifdef __linux__
    static bool checkInRightUserGroup(); 
    static bool checkHidPermission();
    static bool checkBrlttyRunning();

    static const QString driverCommands;
    static const QString groupCommands;
    static const QString udevCommands;
    static const QString brlttyCommands;

    static bool isInRightUserGroup;
    static bool isHidPermission;
    static bool isBrlttyRunning;

    QString buildCommands();
#endif
};

#endif // ENVIRONMENTSETUPDIALOG_H