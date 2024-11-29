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
#include "../scripts/Lexer.h"
#include "../scripts/Parser.h"
#include "../scripts/semanticAnalyzer.h"
#include "../target/MouseManager.h"

class ScriptTool : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptTool(QWidget *parent = nullptr);
    ~ScriptTool();

private slots:
    void selectFile();
    void runScript();

private:
    QLineEdit *filePathEdit;
    QPushButton *selectButton;
    QPushButton *runButton;
    QTextEdit *scriptEdit;
    QFile currentFile;
    Lexer lexer;
    std::vector<Token> tokens;
    QString fileContents;
    std::unique_ptr<MouseManager> mouseManager;
    std::unique_ptr<SemanticAnalyzer> semanticAnalyzer;
    void processAST(ASTNode *node);
};

#endif // SCRIPTTOOL_H

