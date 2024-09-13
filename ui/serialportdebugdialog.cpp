#include "serialportdebugdialog.h"
#include "serial/SerialPortManager.h"
#include "ui/globalsetting.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QByteArray>
#include <QCoreApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QCloseEvent>  // 添加头文件

SerialPortDebugDialog::SerialPortDebugDialog(QWidget *parent)
    : QDialog(parent)
    , textEdit(new QTextEdit(this))
    , debugButtonWidget(new QWidget(this))
    , filterCheckboxWidget(new QWidget(this))
{
    setWindowTitle(tr("Serial Port Debug"));
    resize(640, 480);
    createDebugButtonWidget();
    createFilterCheckBox();
    SerialPortManager* serialPortManager = &SerialPortManager::getInstance();
    if (serialPortManager){
        connect(serialPortManager, &SerialPortManager::dataSent,
                    this, &SerialPortDebugDialog::getSentDataAndInsertText);

        connect(serialPortManager, &SerialPortManager::dataReceived,
                        this, &SerialPortDebugDialog::getRecvDataAndInsertText);
    }
    
    createLayout();
    loadSettings();
}

SerialPortDebugDialog::~SerialPortDebugDialog()
{
    delete this;
}


void SerialPortDebugDialog::createFilterCheckBox(){
    QCheckBox *ChipInfoFilter = new QCheckBox("Chip info filter");  //81
    QCheckBox *keyboardPressFilter = new QCheckBox("keyboard filter");  //82
    QCheckBox *mideaKeyboardFilter = new QCheckBox("media keyboard filter");    //83
    QCheckBox *mouseMoveABSFilter = new QCheckBox("Mouse absolute filter"); //84
    QCheckBox *mouseMoveRELFilter = new QCheckBox("mouse relative filter"); //85
    QCheckBox *HIDFilter = new QCheckBox("HID filter"); //87
    ChipInfoFilter->setObjectName("ChipInfoFilter");
    keyboardPressFilter->setObjectName("keyboardPressFilter");
    mideaKeyboardFilter->setObjectName("mideaKeyboardFilter");
    mouseMoveABSFilter->setObjectName("mouseMoveABSFilter");
    mouseMoveRELFilter->setObjectName("mouseMoveRELFilter");
    HIDFilter->setObjectName("HIDFilter");

    QGridLayout *gridLayout = new QGridLayout(filterCheckboxWidget);
    gridLayout->addWidget(ChipInfoFilter, 0,0, Qt::AlignLeft);
    gridLayout->addWidget(keyboardPressFilter, 0,1, Qt::AlignLeft);
    gridLayout->addWidget(mideaKeyboardFilter, 0,2, Qt::AlignLeft);
    gridLayout->addWidget(mouseMoveABSFilter, 1,0, Qt::AlignLeft);
    gridLayout->addWidget(mouseMoveRELFilter, 1,1, Qt::AlignLeft);
    gridLayout->addWidget(HIDFilter, 1,2, Qt::AlignLeft);
    
}

void SerialPortDebugDialog::createDebugButtonWidget(){
    QPushButton *clearButton = new QPushButton("Clear");
    QPushButton *closeButton = new QPushButton("Close");
    closeButton->setFixedSize(90,30);
    clearButton->setFixedSize(90,30);
    QHBoxLayout *debugButtonLayout = new QHBoxLayout(debugButtonWidget);
    debugButtonLayout->addStretch();
    debugButtonLayout->addWidget(clearButton);
    debugButtonLayout->addWidget(closeButton);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(clearButton, &QPushButton::clicked, textEdit, &QTextEdit::clear);
}

void SerialPortDebugDialog::createLayout(){
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(filterCheckboxWidget);
    mainLayout->addWidget(textEdit);
    mainLayout->addWidget(debugButtonWidget);
    setLayout(mainLayout);
}

void SerialPortDebugDialog::saveSettings(){
    QCheckBox *ChipInfoFilter = filterCheckboxWidget->findChild<QCheckBox *>("ChipInfoFilter");
    QCheckBox *keyboardPressFilter = filterCheckboxWidget->findChild<QCheckBox *>("keyboardPressFilter");
    QCheckBox *mideaKeyboardFilter = filterCheckboxWidget->findChild<QCheckBox *>("mideaKeyboardFilter");
    QCheckBox *mouseMoveABSFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveABSFilter");
    QCheckBox *mouseMoveRELFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveRELFilter");
    QCheckBox *HIDFilter = filterCheckboxWidget->findChild<QCheckBox *>("HIDFilter");
    bool Chipinfo = ChipInfoFilter->isChecked();
    bool keyboardPress = keyboardPressFilter->isChecked();
    bool mideaKeyboard = mideaKeyboardFilter->isChecked();
    bool mouseMoveABS = mouseMoveABSFilter->isChecked();
    bool mouseMoveREL = mouseMoveRELFilter->isChecked();
    bool HID = HIDFilter->isChecked();
    GlobalSetting::instance().setFilterSettings(Chipinfo, keyboardPress, mideaKeyboard, mouseMoveABS, mouseMoveREL, HID);
}

void SerialPortDebugDialog::loadSettings(){
    QSettings settings("Techxartisan", "Openterface");
    QCheckBox *ChipInfoFilter = filterCheckboxWidget->findChild<QCheckBox *>("ChipInfoFilter");
    QCheckBox *keyboardPressFilter = filterCheckboxWidget->findChild<QCheckBox *>("keyboardPressFilter");
    QCheckBox *mideaKeyboardFilter = filterCheckboxWidget->findChild<QCheckBox *>("mideaKeyboardFilter");
    QCheckBox *mouseMoveABSFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveABSFilter");
    QCheckBox *mouseMoveRELFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveRELFilter");
    QCheckBox *HIDFilter = filterCheckboxWidget->findChild<QCheckBox *>("HIDFilter");
    ChipInfoFilter->setChecked(settings.value("filter/Chipinfo", true).toBool());
    keyboardPressFilter->setChecked(settings.value("filter/keyboardPress", true).toBool());
    mideaKeyboardFilter->setChecked(settings.value("filter/mideaKeyboard", true).toBool());
    mouseMoveABSFilter->setChecked(settings.value("filter/mouseMoveABS", true).toBool());
    mouseMoveRELFilter->setChecked(settings.value("filter/mouseMoveREL", true).toBool());
    HIDFilter->setChecked(settings.value("filter/HID", true).toBool());
}

void SerialPortDebugDialog::getRecvDataAndInsertText(const QByteArray &data){
    QString command_type = "";
    QCheckBox *ChipInfoFilter = filterCheckboxWidget->findChild<QCheckBox *>("ChipInfoFilter");
    QCheckBox *keyboardPressFilter = filterCheckboxWidget->findChild<QCheckBox *>("keyboardPressFilter");
    QCheckBox *mideaKeyboardFilter = filterCheckboxWidget->findChild<QCheckBox *>("mideaKeyboardFilter");
    QCheckBox *mouseMoveABSFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveABSFilter");
    QCheckBox *mouseMoveRELFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveRELFilter");
    QCheckBox *HIDFilter = filterCheckboxWidget->findChild<QCheckBox *>("HIDFilter");
    bool Chipinfo = ChipInfoFilter->isChecked();
    bool keyboardPress = keyboardPressFilter->isChecked();
    bool mideaKeyboard = mideaKeyboardFilter->isChecked();
    bool mouseMoveABS = mouseMoveABSFilter->isChecked();
    bool mouseMoveREL = mouseMoveRELFilter->isChecked();
    bool HID = HIDFilter->isChecked();

    if (data.size() >= 4){
        GlobalSetting::instance().setFilterSettings(Chipinfo, keyboardPress, mideaKeyboard, mouseMoveABS, mouseMoveREL, HID);
        unsigned char fourthByte = static_cast<unsigned char>(data[3]);
        qDebug() << "fourthByte: " << fourthByte;
        bool shouldShow = false;
        switch (fourthByte)
        {
        case 0x81:
            command_type = "Chip Info ";
            if (Chipinfo) shouldShow = true;
            break;
        case 0x82:
            command_type = "Keyboard press ";
            if (keyboardPress) shouldShow = true;
            break;
        case 0x83:
            command_type = "Midea keyboard ";
            if (mideaKeyboard) shouldShow = true;
            break;
        case 0x84:
            command_type = "Mouse absolutly move ";
            if (mouseMoveABS) shouldShow = true;
            break;
        case 0x85:
            command_type = "Mouse relative move ";
            if (mouseMoveREL) shouldShow = true;
            break;
        case 0x87:
            command_type = "HID MSG ";
            if (HID) shouldShow = true;
            break;
        default:
            command_type = "Unknown ";
            break;
        }
        if (shouldShow){
            QString dataString = data.toHex().toUpper();
            dataString = formatHexData(dataString);
            dataString = QDateTime::currentDateTime().toString("MM-dd hh:mm:ss.zzz") + " " + command_type + " << " + dataString + "\n";
            textEdit->insertPlainText(dataString);
        }
    }

}

void SerialPortDebugDialog::getSentDataAndInsertText(const QByteArray &data){
    // qDebug() << "send data ->> " << data;
    QString dataString = data.toHex().toUpper();
    dataString = formatHexData(dataString);
    dataString = ">> " + dataString + "\n";
    textEdit->insertPlainText(dataString);

    QTextCursor cursor = textEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    textEdit->setTextCursor(cursor);
    textEdit->ensureCursorVisible();
}


QString SerialPortDebugDialog::formatHexData(QString hexString){
    QString spacedHexString;
    for (int i = 0; i < hexString.length(); i += 2) {
        if (!spacedHexString.isEmpty()) {
            spacedHexString += " ";
        }
        spacedHexString += hexString.mid(i, 2);
    }
    return spacedHexString;
}