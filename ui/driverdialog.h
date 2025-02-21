#ifndef DRIVERDIALOG_H
#define DRIVERDIALOG_H

#include <QDialog>
#include <QCloseEvent>

namespace Ui {
class DriverDialog;
}

class DriverDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DriverDialog(QWidget *parent = nullptr);
    ~DriverDialog();
    
    // New static method to check if the CH340 driver is installed
    static bool isDriverInstalled();

protected:
    void closeEvent(QCloseEvent *event) override;
    void accept() override;
    void reject() override;

private:
    Ui::DriverDialog *ui;

    // Add the new method for driver installation
    #ifdef _WIN32
    void installDriverForWindows();
    #endif
    void createInstallDialog(); // New method for creating the install dialog
    void extractDriverFiles(); // Declaration for extracting driver files
    void copyCommands(); // Declaration for copying commands
};

#endif // DRIVERDIALOG_H