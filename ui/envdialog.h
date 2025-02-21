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
    static bool isDriverInstalled();

protected:
    void closeEvent(QCloseEvent *event) override;
    void accept() override;
    void reject() override;

private:
    Ui::EnvironmentSetupDialog *ui;

    // Add the new method for driver installation
    #ifdef _WIN32
    void installDriverForWindows();
    #endif
    void createInstallDialog(); // New method for creating the install dialog
    void extractDriverFiles(); // Declaration for extracting driver files
    void copyCommands(); // Declaration for copying commands

    // Static command content
    static const QString driverCommands;
    static const QString groupCommands;
    static const QString udevCommands;
};
#endif