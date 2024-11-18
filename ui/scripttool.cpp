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

ScriptTool::ScriptTool(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Bash Bunny Script Tool"));
    setFixedSize(640, 480);  // Set fixed size to 640x480

    // Create widgets
    filePathEdit = new QLineEdit(this);
    filePathEdit->setPlaceholderText(tr("Select payload.txt file..."));
    filePathEdit->setReadOnly(true);

    selectButton = new QPushButton(tr("Browse"), this);
    runButton = new QPushButton(tr("Run Script"), this);
    runButton->setEnabled(false);

    // Create text edit
    scriptEdit = new QTextEdit(this);
    scriptEdit->setReadOnly(true);
    scriptEdit->setFont(QFont("Courier", 10));  // Use monospace font
    scriptEdit->setLineWrapMode(QTextEdit::NoWrap);  // Disable line wrapping

    // Create layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout *fileLayout = new QHBoxLayout();
    fileLayout->addWidget(filePathEdit);
    fileLayout->addWidget(selectButton);
    
    mainLayout->addLayout(fileLayout);
    mainLayout->addWidget(scriptEdit);  // Add text edit to layout
    mainLayout->addWidget(runButton);

    // Connect signals and slots
    connect(selectButton, &QPushButton::clicked, this, &ScriptTool::selectFile);
    connect(runButton, &QPushButton::clicked, this, &ScriptTool::runScript);
}

ScriptTool::~ScriptTool()
{
}

void ScriptTool::selectFile()
{
    // Get the application directory path
    QString appPath = QCoreApplication::applicationDirPath();
    
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Select Payload File"),
        appPath,  // Set the default directory to application path
        tr("Text Files (*.txt);;All Files (*)"));

    if (!filePath.isEmpty()) {
        filePathEdit->setText(filePath);
        runButton->setEnabled(true);

        // Load and display file contents
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            scriptEdit->setText(in.readAll());
            file.close();
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

    // TODO: Implement the script execution logic here
    // This is where you would add the code to process and execute the payload.txt file
    QMessageBox::information(this, tr("Script Execution"), 
        tr("Script execution will be implemented here.\nSelected file: %1").arg(filePath));
}

