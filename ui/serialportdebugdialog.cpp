#include "serialportdebugdialog.h"
#include "ui_serialportdebugdialog.h"
#include "serial/SerialPortManager.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QByteArray>
#include <QCoreApplication>

serialPortDebugDialog::serialPortDebugDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::serialPortDebugDialog)
    , textEdit(new QTextEdit(this))
    , debugButtonWidget(new QWidget(this))
{
    ui->setupUi(this);
    setWindowTitle(tr("Serial Port Debug"));

    createDebugButtonWidget();

    SerialPortManager* serialPortManager = &SerialPortManager::getInstance();
    if (serialPortManager){
        connect(serialPortManager, &SerialPortManager::dataSent,
                    this, &serialPortDebugDialog::getSentDataAndInsertText);

        connect(serialPortManager, &SerialPortManager::dataReceived,
                        this, &serialPortDebugDialog::getRecvDataAndInsertText);
    }
    
    createLayout();
}

serialPortDebugDialog::~serialPortDebugDialog()
{
    delete ui;
}


void serialPortDebugDialog::createDebugButtonWidget(){
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

void serialPortDebugDialog::createLayout(){
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(textEdit);
    mainLayout->addWidget(debugButtonWidget);
    setLayout(mainLayout);
}

void serialPortDebugDialog::getRecvDataAndInsertText(const QByteArray &data){
    qDebug() << "recv data <<- " << data;
    QString dataString = data.toHex().toUpper();
    dataString = formatHexData(dataString);
    dataString = "<< " + dataString + "\n";
    textEdit->insertPlainText(dataString);

    QTextCursor cursor = textEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    textEdit->setTextCursor(cursor);
    textEdit->ensureCursorVisible();
}

void serialPortDebugDialog::getSentDataAndInsertText(const QByteArray &data){
    qDebug() << "send data ->> " << data;
    QString dataString = data.toHex().toUpper();
    dataString = formatHexData(dataString);
    dataString = ">> " + dataString + "\n";
    textEdit->insertPlainText(dataString);

    QTextCursor cursor = textEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    textEdit->setTextCursor(cursor);
    textEdit->ensureCursorVisible();
}


QString serialPortDebugDialog::formatHexData(QString hexString){
    QString spacedHexString;
    for (int i = 0; i < hexString.length(); i += 2) {
        if (!spacedHexString.isEmpty()) {
            spacedHexString += " ";
        }
        spacedHexString += hexString.mid(i, 2);
    }
    return spacedHexString;
}
