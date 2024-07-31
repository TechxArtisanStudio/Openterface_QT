#include "settingdialog.h"
#include "ui_settingdialog.h"
#include "global.h"

#include <QApplication>


settingDialog::settingDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::settingDialog)
{
    ui->setupUi(this);
}

settingDialog::~settingDialog()
{
    delete ui;
}

void settingDialog::init(){
    // find all the widget
    
    // just show the setting
}

void settingDialog::clickbtn()
{
    qDebug() << "click btn";
    QWidget *logwidget = findChild<QWidget*>("logWidget");
    if (logwidget){
        qDebug() << "logwidget has been hidden";
        logwidget->hide();
    }
}

