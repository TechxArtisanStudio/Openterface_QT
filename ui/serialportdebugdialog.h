#ifndef SERIALPORTDEBUGDIALOG_H
#define SERIALPORTDEBUGDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QWidget>
#include <QByteArray>
#include <QString>
namespace Ui {
class SerialPortDebugDialog;
}

class SerialPortDebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SerialPortDebugDialog(QWidget *parent = nullptr);
    ~SerialPortDebugDialog();

private slots:
    void getRecvDataAndInsertText(const QByteArray &data);
    void getSentDataAndInsertText(const QByteArray &data);

private:
    Ui::SerialPortDebugDialog *ui;
    QTextEdit *textEdit;
    QWidget *debugButtonWidget;
    QWidget *filterCheckboxWidget;
    void createDebugButtonWidget();
    void createFilterCheckBox();
    
    void createLayout();
    QString formatHexData(QString hexString);
};

#endif // SERIALPORTDEBUGDIALOG_H
