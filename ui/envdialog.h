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
    
    // New static method to check if the CH340 driver is installed
    static bool checkEnvironmentSetup();

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

    bool checkDriverInstalled();
#ifdef __unix__
    bool checkInRightUserGroup();
    bool checkHidPermission();
    bool checkBrlttyInstalled();
#endif

    static const QString driverCommands;
    static const QString groupCommands;
    static const QString udevCommands;
    static const QString brlttyCommands;
    static const QString helpUrl; // URL for help documentation

    static bool isDriverInstalled;
    static bool isInRightUserGroup;
    static bool isHidPermission;
    static bool isBrlttyInstalled;
    
    QString buildCommands();
};

#endif // ENVIRONMENTSETUPDIALOG_H