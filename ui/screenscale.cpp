#include "screenscale.h"
#include "ui/globalsetting.h"
#include <QDebug>

ScreenScale::ScreenScale(QWidget *parent) : QDialog(parent)
{
    // Set dialog title
    setWindowTitle(tr("Screen Aspect Ratio"));

    // Initialize widgets
    ratioComboBox = new QComboBox(this);
    okButton = new QPushButton("OK", this);
    cancelButton = new QPushButton("Cancel", this);

    // Populate combo box with common screen aspect ratios
    ratioComboBox->addItem("16:9");
    ratioComboBox->addItem("4:3");
    ratioComboBox->addItem("16:10");
    ratioComboBox->addItem("5:3");
    ratioComboBox->addItem("5:4");
    ratioComboBox->addItem("21:9");
    ratioComboBox->addItem("9:16");
    ratioComboBox->addItem("9:19.5");
    ratioComboBox->addItem("9:20");
    ratioComboBox->addItem("9:21");

    setFixedSize(200, 150);
    // Set up layout
    layout = new QVBoxLayout(this);
    layout->addWidget(ratioComboBox);
    layoutBtn = new QHBoxLayout(this);
    layoutBtn->addWidget(okButton);
    layoutBtn->addWidget(cancelButton);
    layout->addLayout(layoutBtn);

    double savedRatio = GlobalSetting::instance().getScreenRatio();
    QString savedRatioStr = converseRatio(savedRatio);
    int index = ratioComboBox->findText(savedRatioStr);
    if (index != -1) ratioComboBox->setCurrentIndex(index);

    // Connect buttons to slots
    connect(okButton, &QPushButton::clicked, this, &ScreenScale::onOkClicked);
    connect(cancelButton, &QPushButton::clicked, this, &ScreenScale::onCancelClicked);

    // Set layout to dialog
    setLayout(layout);
}

ScreenScale::~ScreenScale()
{
    // Qt handles widget deletion via parent-child hierarchy
}

QString ScreenScale::getSelectedRatio() const
{
    return ratioComboBox->currentText();
}

double ScreenScale::converseRatio(QString ratio){
    QStringList parts = ratio.split(":");
    if (parts.size() == 2) {
        bool ok1, ok2;
        float num1 = parts[0].toFloat(&ok1);
        float num2 = parts[1].toFloat(&ok2);  
        double result = static_cast<double>(num1) / num2;
        return result;
    }
}

QString ScreenScale::converseRatio(double ratio) {
    if (qFuzzyCompare(ratio, 16.0/9.0)) return "16:9";
    if (qFuzzyCompare(ratio, 4.0/3.0)) return "4:3";
    if (qFuzzyCompare(ratio, 16.0/10.0)) return "16:10";
    if (qFuzzyCompare(ratio, 5.0/3.0)) return "5:3";
    if (qFuzzyCompare(ratio, 5.0/4.0)) return "5:4";
    if (qFuzzyCompare(ratio, 21.0/9.0)) return "21:9";
    if (qFuzzyCompare(ratio, 9.0/16.0)) return "9:16";
    if (qFuzzyCompare(ratio, 9.0/19.5)) return "9:19.5";
    if (qFuzzyCompare(ratio, 9.0/20.0)) return "9:20";
    if (qFuzzyCompare(ratio, 9.0/21.0)) return "9:21";
    return "16:9";
}

void ScreenScale::onOkClicked()
{
    // Emit signal with selected ratio
    QString selectedRatio = getSelectedRatio();
    // GlobalSetting::instance().setScreenRatio(selectedRatio);
    qDebug() << "ScreenScale::onOkClicked" << selectedRatio;
    double ratio = converseRatio(selectedRatio);
    emit screenRatio(ratio);
    qDebug() << "ScreenScale::onOkClicked" << ratio;
    accept(); // Close dialog with QDialog::Accepted status
}

void ScreenScale::onCancelClicked()
{
    reject(); // Close dialog with QDialog::Rejected status
}