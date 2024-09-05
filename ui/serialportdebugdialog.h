#ifndef SERIALPORTDEBUGDIALOG_H
#define SERIALPORTDEBUGDIALOG_H

#include <QDialog>
#include <QTextEdit>

namespace Ui {
class serialPortDebugDialog;
}

class serialPortDebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit serialPortDebugDialog(QWidget *parent = nullptr);
    ~serialPortDebugDialog();

private:
    Ui::serialPortDebugDialog *ui;
    QTextEdit *textEdit;
};

#endif // SERIALPORTDEBUGDIALOG_H
