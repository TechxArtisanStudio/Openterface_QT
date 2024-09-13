#include "serialportdebugdialog.h"
#include "serial/SerialPortManager.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QByteArray>
#include <QCoreApplication>
#include <QCheckBox>
#include <QGridLayout>

SerialPortDebugDialog::SerialPortDebugDialog(QWidget *parent)
    : QDialog(parent)
    , textEdit(new QTextEdit(this))
    , debugButtonWidget(new QWidget(this))
    , filterCheckboxWidget(new QWidget(this))
{
    setWindowTitle(tr("Serial Port Debug"));

    createDebugButtonWidget();
    // createFilterCheckBox();
    SerialPortManager* serialPortManager = &SerialPortManager::getInstance();
    if (serialPortManager){
        connect(serialPortManager, &SerialPortManager::dataSent,
                    this, &SerialPortDebugDialog::getSentDataAndInsertText);

        connect(serialPortManager, &SerialPortManager::dataReceived,
                        this, &SerialPortDebugDialog::getRecvDataAndInsertText);
    }
    
    createLayout();
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
    // mainLayout->addWidget(filterCheckboxWidget);
    mainLayout->addWidget(textEdit);
    mainLayout->addWidget(debugButtonWidget);
    setLayout(mainLayout);
}

void SerialPortDebugDialog::getRecvDataAndInsertText(const QByteArray &data){
    // QCheckBox *ChipInfoFilter = filterCheckboxWidget->findChild<QCheckBox *>("ChipInfoFilter");
    // QCheckBox *keyboardPressFilter = filterCheckboxWidget->findChild<QCheckBox *>("keyboardPressFilter");
    // QCheckBox *mideaKeyboardFilter = filterCheckboxWidget->findChild<QCheckBox *>("mideaKeyboardFilter");
    // QCheckBox *mouseMoveABSFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveABSFilter");
    // QCheckBox *mouseMoveRELFilter = filterCheckboxWidget->findChild<QCheckBox *>("mouseMoveRELFilter");
    // QCheckBox *HIDFilter = filterCheckboxWidget->findChild<QCheckBox *>("HIDFilter");
    // bool Chipinfo = ChipInfoFilter->isChecked();
    // bool keyboardPress = keyboardPressFilter->isChecked();
    // bool mideaKeyboard = mideaKeyboardFilter->isChecked();
    // bool mouseMoveABS = mouseMoveABSFilter->isChecked();
    // bool mouseMoveREL = mouseMoveRELFilter->isChecked();
    // bool HID = HIDFilter->isChecked();
    // char fourthByte = data.at(3);
    // bool shouldShow = false;

    // if ((fourthByte&& Chipinfo == 0x81) || (fourthByte&&keyboardPress==0x82) 
    // || (fourthByte&&mideaKeyboard==0x83) || (fourthByte&&mouseMoveABS==0x84) 
    // || (fourthByte&&mouseMoveREL==0x85) || (fourthByte&&HID == 0x87)){
    //     shouldShow = true;
    // }

    // if (!shouldShow) {
    //     return;
    // }
    
    QString dataString = data.toHex().toUpper();
    dataString = formatHexData(dataString);
    dataString = "<< " + dataString + "\n";
    textEdit->insertPlainText(dataString);

    // QTextCursor cursor = textEdit->textCursor();
    // cursor.movePosition(QTextCursor::End);
    // textEdit->setTextCursor(cursor);
    // textEdit->ensureCursorVisible();
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
