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
#include <QStackedWidget>
#include <QDebug>

settingDialog::settingDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::settingDialog)
    , settingTree(new QTreeWidget(this))
    , stackedWidget(new QStackedWidget(this))
{
    ui->setupUi(this);
    createSettingTree();
    createPages();
    createLayout();
    setWindowTitle(tr("Setting"));

    // Connect the tree widget's currentItemChanged signal to a slot
    connect(settingTree, &QTreeWidget::currentItemChanged, this, &settingDialog::changePage);
}

settingDialog::~settingDialog()
{
    delete ui;
    // Ensure all dynamically allocated memory is freed
    qDeleteAll(settingTree->invisibleRootItem()->takeChildren());
}

void settingDialog::createSettingTree() {
    // qDebug() << "creating setting Tree";
    settingTree->setColumnCount(1);
    settingTree->setHeaderLabels(QStringList(tr("Setting")));
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);
    
    settingTree->setMaximumSize(QSize(120, 1000));
    settingTree->setRootIsDecorated(false);
    QStringList names = {"log", "video", "audio"};
    for (const QString &name : names) {
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
}

void settingDialog::createPages() {
    // Create pages for each setting
    QWidget *logPage = new QWidget();
    QWidget *videoPage = new QWidget();
    QWidget *audioPage = new QWidget();
    

    // Create labels for each page
    QLabel *logLabel = new QLabel(" log setting", logPage);
    QLabel *videoLabel = new QLabel(" video setting", videoPage);   
    QLabel *audioLabel = new QLabel(" audio setting", audioPage);

    // Create layouts for each page and add labels to them
    QVBoxLayout *logLayout = new QVBoxLayout(logPage);
    logLayout->addWidget(logLabel);
    logLayout->addStretch();

    QVBoxLayout *videoLayout = new QVBoxLayout(videoPage);
    videoLayout->addWidget(videoLabel);
    videoLayout->addStretch(); 

    QVBoxLayout *audioLayout = new QVBoxLayout(audioPage);
    audioLayout->addWidget(audioLabel);
    audioLayout->addStretch();

    // Add pages to the stacked widget
    stackedWidget->addWidget(logPage);
    stackedWidget->addWidget(videoPage);
    stackedWidget->addWidget(audioPage);
}

void settingDialog::createLayout() {
    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(settingTree);
    mainLayout->addWidget(stackedWidget);

    setLayout(mainLayout);
}

void settingDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    if (!current)
        current = previous;

    // Switch page based on the selected item
    QString itemText = current->text(0);
    qDebug() << "Selected item:" << itemText;
    if (itemText == "log") {
        stackedWidget->setCurrentIndex(0);
    } else if (itemText == "video") {
        stackedWidget->setCurrentIndex(1);
    } else if (itemText == "audio") {
        stackedWidget->setCurrentIndex(2);
    }
}