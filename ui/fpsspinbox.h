#ifndef FPSSPINBOX_H
#define FPSSPINBOX_H

#include <QSpinBox>
#include <QCameraFormat>
#include <set>

class FpsSpinBox : public QSpinBox
{
    Q_OBJECT

public:
    explicit FpsSpinBox(QWidget *parent = nullptr);
    void setValidValues(const std::set<int> &values);

protected:
    void stepBy(int steps) override;

private:
    std::set<int> validValues;

};
#endif // FPSSPINBOX_H
