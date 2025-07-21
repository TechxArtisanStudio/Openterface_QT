/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/ 

#include "scripttool.h"
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QShortcut>
#include <QDebug>
#include <thread>
#include "scripts/Lexer.h"
#include "scripts/Parser.h"
// #include "../scripts/semanticAnalyzer.h"
#include "scripts/KeyboardMouse.h"
#include <QColor>

Q_DECLARE_LOGGING_CATEGORY(log_script)

ScriptTool::ScriptTool(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Autohotkey Script Tool"));
    setFixedSize(640, 480);

    filePathEdit = new QLineEdit(this);
    filePathEdit->setPlaceholderText(tr("Select autohotkey.ahk file..."));
    filePathEdit->setReadOnly(true);

    selectButton = new QPushButton(tr("Browse"), this);
    runButton = new QPushButton(tr("Run Script"), this);
    runButton->setEnabled(false);

    saveButton = new QPushButton(tr("Save Script"), this);
    saveButton->setEnabled(false);

    scriptEdit = new ScriptEditor(this);
    scriptEdit->setReadOnly(true);
    scriptEdit->setFont(QFont("Courier", 10));
    scriptEdit->setLineWrapMode(QTextEdit::NoWrap);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout *fileLayout = new QHBoxLayout();
    fileLayout->addWidget(filePathEdit);
    fileLayout->addWidget(selectButton);
    
    // Add the cancel button
    QPushButton *cancelButton = new QPushButton(tr("Cancel"), this);
    
    // Arrange buttons horizontally
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(selectButton);
    buttonLayout->addWidget(runButton);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(fileLayout);
    mainLayout->addWidget(scriptEdit);
    mainLayout->addLayout(buttonLayout);

    connect(selectButton, &QPushButton::clicked, this, &ScriptTool::selectFile);
    connect(runButton, &QPushButton::clicked, this, &ScriptTool::runScript);
    connect(saveButton, &QPushButton::clicked, this, &ScriptTool::saveScript);
    connect(cancelButton, &QPushButton::clicked, this, &ScriptTool::close);
    commandLine = 1;
}

ScriptTool::~ScriptTool()
{
}

void ScriptTool::selectFile()
{
    QString appPath = QCoreApplication::applicationDirPath();
    
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Select autohotkey File"),
        appPath,
        tr("Autohotkey Files (*.ahk);;All Files (*)"));

    if (!filePath.isEmpty()) {
        filePathEdit->setText(filePath);
        runButton->setEnabled(true);

        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            fileContents = in.readAll();
            file.close();
            lexer.setSource(fileContents.toStdString());
            tokens = lexer.tokenize();
            

            // qDebug(log_script) << "Token Type:" << static_cast<int>(token.type) << "Value:" << tokenText;
            
            // scriptEdit->setText(styledText);
            scriptEdit->clear();
            scriptEdit->setReadOnly(false);
            // scriptEdit->setText(fileContents);
            highlightTokens(tokens);
            qCDebug(log_script) << "Content after highlightTokens:" << scriptEdit->toPlainText();
            saveButton->setEnabled(true);
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Could not open file for reading."));
        }
    }
}

void ScriptTool::runScript()
{
    QString filePath = filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a payload file first."));
        return;
    }

    lexer.setSource(scriptEdit->toPlainText().toStdString());
    tokens = lexer.tokenize();

    Parser parser(tokens);
    std::shared_ptr<ASTNode> syntaxTree = parser.parse();
    // qDebug(log_script) << "synctaxTree: " << syntaxTree.get();

    // Emit the signal with the syntaxTree
    emit syntaxTreeReady(syntaxTree);

    // QMessageBox::information(this, tr("Script Execution"), 
    //     tr("Script execution will be implemented here.\nSelected file: %1").arg(filePath));
}

void ScriptTool::processAST(ASTNode* node)
{
    if (!node) return;

    
}

void ScriptTool::saveScript()
{
    QString filePath = filePathEdit->text();
    if (!filePath.isEmpty()) {
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << scriptEdit->toPlainText();
            file.close();
            QMessageBox::information(this, tr("Success"), tr("Script saved successfully."));
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Could not save file."));
        }
    }
}

void ScriptTool::highlightTokens(const std::vector<Token>& tokens) {
    scriptEdit->clear();
    QTextCursor cursor = scriptEdit->textCursor(); 
    cursor.beginEditBlock();
    
    for (const auto& token : tokens) {
        QString tokenText = QString::fromStdString(token.value);
        QTextCharFormat format;

        if (tokenText == "\\n") {
            tokenText = "\n"; 
        }

        switch (token.type) {
            case AHKTokenType::KEYWORD:
                format.setForeground(Qt::green);
                break;
            case AHKTokenType::FUNCTION:
                format.setForeground(Qt::blue);
                break;
            case AHKTokenType::VARIABLE:
                format.setForeground(Qt::white);
                break;
            case AHKTokenType::INTEGER:
            case AHKTokenType::FLOAT:
                format.setForeground(QColor("DarkGoldenRod"));
                break;
            case AHKTokenType::COMMAND:
                format.setForeground(QColor("purple"));
                break;
            case AHKTokenType::COMMENT:
                format.setForeground(Qt::gray);
                break;
            default:
                if (QGuiApplication::palette().color(QPalette::Window).lightness() < 128) {
                    format.setForeground(Qt::white);
                } else {
                    format.setForeground(Qt::black);
                }
                break;
        }

        cursor.setCharFormat(format);
        cursor.insertText(tokenText);
    }

    cursor.endEditBlock(); 
    scriptEdit->setTextCursor(cursor); 
    scriptEdit->ensureCursorVisible();
}

void ScriptTool::handleCommandIncrement(){
    // TODO: Implement command increment
    if (lastHighlightedLine != -1) scriptEdit->resetHighlightLine(lastHighlightedLine);

    scriptEdit->highlightLine(commandLine);
    lastHighlightedLine = commandLine;
    qCDebug(log_script) << "Command incremented" << commandLine;
    commandLine += 1;
}

void ScriptTool::resetCommmandLine(bool status){
    qCDebug(log_script) << "Command reset" << "script status: " << status;
    scriptEdit->resetHighlightLine(lastHighlightedLine);
    commandLine = 1;
    int lastHighlightedLine = -1;
}
