#ifndef SCREENSCALE_H
#define SCREENSCALE_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>

class ScreenScale : public QDialog
{
    Q_OBJECT

public:
    explicit ScreenScale(QWidget *parent = nullptr);
    ~ScreenScale();

    QString getSelectedRatio() const;

signals:
    void screenRatio(double ratio);

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    QComboBox *ratioComboBox;
    QPushButton *okButton;
    QPushButton *cancelButton;
    QVBoxLayout *layout;
    QHBoxLayout *layoutBtn;
    double converseRatio(QString ratio);
    QString converseRatio(double ratio);
};

#endif // SCREENSCALE_H
