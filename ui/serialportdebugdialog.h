#ifndef SERIALPORTDEBUGDIALOG_H
#define SERIALPORTDEBUGDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QWidget>
#include <QByteArray>
#include <QString>
namespace Ui {
class serialPortDebugDialog;
}

class serialPortDebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit serialPortDebugDialog(QWidget *parent = nullptr);
    ~serialPortDebugDialog();

private slots:
    void getRecvDataAndInsertText(const QByteArray &data);
    void getSentDataAndInsertText(const QByteArray &data);

private:
    Ui::serialPortDebugDialog *ui;
    QTextEdit *textEdit;
    QWidget *debugButtonWidget;

    void createDebugButtonWidget();
    
    void createLayout();
    QString formatHexData(QString hexString);
};

#endif // SERIALPORTDEBUGDIALOG_H
