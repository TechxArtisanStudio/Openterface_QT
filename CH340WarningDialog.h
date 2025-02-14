#ifndef CH340WARNINGDIALOG_H
#define CH340WARNINGDIALOG_H

#include <QDialog>

namespace Ui {
class CH340WarningDialog;
}

class CH340WarningDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CH340WarningDialog(QWidget *parent = nullptr);
    ~CH340WarningDialog();

private:
    Ui::CH340WarningDialog *ui;
};

#endif // CH340WARNINGDIALOG_H 