#include "settingdialog.h"
#include "ui_settingdialog.h"
#include "global.h"
#include "imagesettings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
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
    , buttonWidget(new QWidget(this))
{
    ui->setupUi(this);
    createSettingTree();
    createPages();
    createButtons();
    createLayout();
    setWindowTitle(tr("Preferences"));

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
    settingTree->setHeaderLabels(QStringList(tr("general")));
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);
    
    settingTree->setMaximumSize(QSize(120, 1000));
    settingTree->setRootIsDecorated(false);
    QStringList names = {"log", "video", "audio"};
    for (const QString &name : names) {     // add item to setting tree
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
}

void settingDialog::createPages() {
    // Create pages for each setting
    QWidget *logPage = new QWidget();
    QWidget *videoPage = new QWidget();
    QWidget *audioPage = new QWidget();
    
    // create checkbox for log
    QWidget *logCheckbox = new QWidget();
    QCheckBox *coreCheckBox = new QCheckBox("core", logCheckbox);
    QCheckBox *serialCheckBox = new QCheckBox("serial", logCheckbox);
    QCheckBox *uiCheckBox = new QCheckBox("ui", logCheckbox);
    QCheckBox *hostCheckBox = new QCheckBox("host",logCheckbox);
    QHBoxLayout *logCheckboxLayout = new QHBoxLayout(logCheckbox);
    logCheckboxLayout->addWidget(coreCheckBox);
    logCheckboxLayout->addWidget(serialCheckBox);
    logCheckboxLayout->addWidget(uiCheckBox);
    logCheckboxLayout->addWidget(hostCheckBox);


    // Create labels for each page
    QLabel *logLabel = new QLabel("log general", logPage);
    QLabel *videoLabel = new QLabel("video general", videoPage);
    QLabel *audioLabel = new QLabel("audio general", audioPage);
    

    // Create layouts for each page and add labels to them
    QVBoxLayout *logLayout = new QVBoxLayout(logPage);
    logLayout->addWidget(logLabel);
    logLayout->addWidget(logCheckbox);
    logLayout->addLayout(logCheckboxLayout);
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

void settingDialog::createButtons(){
    QPushButton *okButton = new QPushButton("ok");
    QPushButton *applyButton = new QPushButton("apply");
    QPushButton *cancelButton = new QPushButton("cencel");

    okButton->setFixedSize(80, 30);
    applyButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);

    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);
}

void settingDialog::createLayout() {

    QHBoxLayout *selectLayout = new QHBoxLayout;
    selectLayout->addWidget(settingTree);
    selectLayout->addWidget(stackedWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(selectLayout);
    mainLayout->addWidget(buttonWidget);
    
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
