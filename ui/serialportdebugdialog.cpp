#include "serialportdebugdialog.h"
#include "ui_serialportdebugdialog.h"

serialPortDebugDialog::serialPortDebugDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::serialPortDebugDialog)
    , textEdit(new QTextEdit(this))
{
    ui->setupUi(this);
    setWindowTitle(tr("Serial Port Debug"));
    textEdit->setGeometry(QRect(QPoint(10, 20), QSize(300, 200)));
}

serialPortDebugDialog::~serialPortDebugDialog()
{
    delete ui;
    
}


