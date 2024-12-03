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
#include "../scripts/Lexer.h"
#include "../scripts/Parser.h"
#include "../scripts/semanticAnalyzer.h"
#include "../target/MouseManager.h"

ScriptTool::ScriptTool(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Bash Bunny Script Tool"));
    setFixedSize(640, 480);

    filePathEdit = new QLineEdit(this);
    filePathEdit->setPlaceholderText(tr("Select payload.txt file..."));
    filePathEdit->setReadOnly(true);

    selectButton = new QPushButton(tr("Browse"), this);
    runButton = new QPushButton(tr("Run Script"), this);
    runButton->setEnabled(false);

    scriptEdit = new QTextEdit(this);
    scriptEdit->setReadOnly(true);
    scriptEdit->setFont(QFont("Courier", 10));
    scriptEdit->setLineWrapMode(QTextEdit::NoWrap);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout *fileLayout = new QHBoxLayout();
    fileLayout->addWidget(filePathEdit);
    fileLayout->addWidget(selectButton);
    
    mainLayout->addLayout(fileLayout);
    mainLayout->addWidget(scriptEdit);
    mainLayout->addWidget(runButton);

    connect(selectButton, &QPushButton::clicked, this, &ScriptTool::selectFile);
    connect(runButton, &QPushButton::clicked, this, &ScriptTool::runScript);

    mouseManager = std::make_unique<MouseManager>();
    semanticAnalyzer = std::make_unique<SemanticAnalyzer>(mouseManager.get());
}

ScriptTool::~ScriptTool()
{
}

void ScriptTool::selectFile()
{
    QString appPath = QCoreApplication::applicationDirPath();
    
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Select Payload File"),
        appPath,
        tr("Text Files (*.ahk);;All Files (*)"));

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
            QString styledText;
            for (const auto& token : tokens) {
                QString tokenText = QString::fromStdString(token.value);
                QString color;
                if (tokenText == "\\n")
                tokenText = tokenText.replace("\\n", "<br>");
                switch (token.type) {
                    case AHKTokenType::KEYWORD:
                        color = "green";
                        break;
                    case AHKTokenType::FUNCTION:
                        color = "blue";
                        break;
                    case AHKTokenType::VARIABLE:
                        color = "white";
                        break;
                    case AHKTokenType::INTEGER:
                    case AHKTokenType::FLOAT:
                        color = "DarkGoldenRod";
                        break;
                    case AHKTokenType::COMMAND:
                        color = "purple";
                        break;
                    case AHKTokenType::COMMENT:
                        color = "grey";
                        break;
                    default:
                        color = "black"; // Default color for unrecognized tokens
                        break;
                }
                styledText += QString("<span style='color:%1;'>%2</span>").arg(color, tokenText);
                qDebug() << "Token Type:" << static_cast<int>(token.type) << "Value:" << tokenText;
            }
            scriptEdit->setText(styledText);
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

    // Use the syntax tree as needed
    // For example, you can traverse it or print it for debugging
    Parser parser(tokens);
    std::unique_ptr<ASTNode> syntaxTree = parser.parse();
    qDebug() << "synctaxTree: " << syntaxTree.get();
    // Process the AST
    processAST(syntaxTree.get());

    QMessageBox::information(this, tr("Script Execution"), 
        tr("Script execution will be implemented here.\nSelected file: %1").arg(filePath));
}

void ScriptTool::processAST(ASTNode* node)
{
    if (!node) return;

    // Use the semantic analyzer to process the AST
    semanticAnalyzer->analyze(node);
}

