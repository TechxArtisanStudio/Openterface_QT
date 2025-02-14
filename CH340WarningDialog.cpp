#include "CH340WarningDialog.h"
#include "ui_CH340WarningDialog.h"

CH340WarningDialog::CH340WarningDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CH340WarningDialog)
{
    ui->setupUi(this);
}

CH340WarningDialog::~CH340WarningDialog()
{
    delete ui;
} 