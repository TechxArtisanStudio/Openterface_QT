#include "settingdialog.h"
#include "ui_settingdialog.h"
#include "global.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

settingDialog::settingDialog( QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::settingDialog)
    , settingTree(new QTreeWidget(this))
{
    ui->setupUi(this);
    createSettingTree();
    createLayout();
    setWindowTitle(tr("Setting"));
}

settingDialog::~settingDialog()
{
    delete ui;
}

void settingDialog::createSettingTree(){
    qDebug()<< "creating setting Tree";
    settingTree->setColumnCount(1);
    // qDebug()<< "end create";
    settingTree->setHeaderLabels(QStringList(tr("Setting")));
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);
    settingTree->setRootIsDecorated(false);
    QStringList names = {"log", "video", "audio"};
    for (const QString &name : names){
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
}   // createSettingTree

void settingDialog::createLayout() {
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(settingTree);
    setLayout(mainLayout);

}   // createLayout
