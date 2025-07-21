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

#ifndef SCRIPTTOOL_H
#define SCRIPTTOOL_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QTextEdit>
#include <QThread>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include "scripts/Lexer.h"
#include "scripts/Parser.h"
#include "scripts/semanticAnalyzer.h"
#include "target/MouseManager.h"
#include "scripts/scriptEditor.h"

class ScriptTool : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptTool(QWidget *parent = nullptr);
    ~ScriptTool();

signals:
    void syntaxTreeReady(std::shared_ptr<ASTNode> syntaxTree);

public slots:
    void handleCommandIncrement();
    void resetCommmandLine(bool status);

private slots:
    void selectFile();
    void runScript();
    void saveScript();
    
private:
    QLineEdit *filePathEdit;
    QPushButton *selectButton;
    QPushButton *runButton;
    QPushButton *saveButton;
    QPushButton *cancelButton;
    ScriptEditor *scriptEdit;
    QFile currentFile;
    Lexer lexer;
    std::vector<Token> tokens;
    QString fileContents;
    int commandLine;
    int lastHighlightedLine = -1;
    void processAST(ASTNode *node);
    void highlightTokens(const std::vector<Token>& tokens);
};

#endif // SCRIPTTOOL_H

