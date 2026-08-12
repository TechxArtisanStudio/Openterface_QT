#ifndef FIRMWAREMANAGERDIALOG_H
#define FIRMWAREMANAGERDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QApplication>
#include <QProgressDialog>

class FirmwareManagerDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit FirmwareManagerDialog(QWidget *parent = nullptr);
    ~FirmwareManagerDialog() override;

private slots:
    
    void onReadFromFileClicked();
    void onWriteFirmwareFromFileClick();

private:
    QLabel *versionLabel;

    QPushButton *readLoacalFirmwareBtn;
    QPushButton *writeFirmwareFromFileBtn;
    QProgressDialog *progressDialog;

    QString onSelectPathClicked();
    QString selectFirmware();
    QByteArray readBinFileToByteArray(const QString &filePath);
};

#endif // FIRMWAREMANAGERDIALOG_H