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
    void installDriver(); // Declaration of the new method
    void extractDriverFiles(); // Add this line to declare the new method
};

#endif // DRIVERDIALOG_H