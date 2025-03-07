#ifndef FIRMWAREUPDATEDIALOG_H
#define FIRMWAREUPDATEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QApplication>
#include "video/videohid.h"

class FirmwareUpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirmwareUpdateDialog(QWidget *parent = nullptr);
    ~FirmwareUpdateDialog() override;
    
    bool startUpdate();

private slots:
    void updateProgress(int value);
    void updateComplete(bool success);
    void onCloseButtonClicked();
    void onProgressTimerTimeout();

signals:
    void updateFinished(bool success);

private:
    QLabel *statusLabel;
    QProgressBar *progressBar;
    QPushButton *closeButton;
    bool updateResult;
    QTimer *progressTimer;
};

#endif // FIRMWAREUPDATEDIALOG_H
