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


#include "semanticAnalyzer.h"
#include <stdexcept>
#include <QDebug>
#include <QString>

SemanticAnalyzer::SemanticAnalyzer(MouseManager* mouseManager) 
    : mouseManager(mouseManager) {}

void SemanticAnalyzer::analyze(const ASTNode* node) {
    if (!node) return;
    
    switch (node->getType()) {
        case ASTNodeType::StatementList:
            // Process each statement in the list
            for (const auto& child : node->getChildren()) {
                analyze(child.get());
                resetParameters(); // Reset after each statement
            }
            break;
            
        case ASTNodeType::ClickStatement:
            analyzeClickStatement(static_cast<const ClickStatementNode*>(node));
            qDebug() << "click statement";
            break;
            
        default:
            // Process any child nodes
            for (const auto& child : node->getChildren()) {
                analyze(child.get());
            }
            break;
    }
}

void SemanticAnalyzer::resetParameters() {
    // Reset mouse manager state
    mouseManager->reset();
    qDebug() << "Reset parameters for next statement";
}

void SemanticAnalyzer::analyzeClickStatement(const ClickStatementNode* node) {
    const auto& options = node->getOptions();
    if (options.empty()) {
        qDebug() << "No coordinates provided for Click command";
        return;
    }
    
    // Parse coordinates and mouse button from options
    QPoint coords = parseCoordinates(options);
    int mouseButton = parseMouseButton(options);  // This will be fresh for each statement

    qDebug() << "Executing click at:" << coords.x() << "," << coords.y() 
             << "with button:" << mouseButton;

    // Execute the mouse action
    mouseManager->handleAbsoluteMouseAction(coords.x(), coords.y(), mouseButton, 0);
}

QPoint SemanticAnalyzer::parseCoordinates(const std::vector<std::string>& options) {
    if (options.empty()) {
        qDebug() << "No coordinate components";
        return QPoint(0, 0);
    }
    
    int x = 0, y = 0;
    bool foundComma = false;
    bool beforeComma = true;
    bool okX = false, okY = false;
    
    for (const auto& token : options) {
        if (token == ",") {
            foundComma = true;
            beforeComma = false;
            continue;
        }
        
        // Try to parse the token as a number
        bool ok = false;
        int value = QString::fromStdString(token).toInt(&ok);
        if (ok) {
            if (beforeComma) {
                x = value;
                okX = true;
            } else {
                y = value;
                okY = true;
            }
        }
    }
    
    if (!foundComma || (!okX && !okY)) {
        qDebug() << "Invalid coordinate format, using defaults";
        return QPoint(0, 0);
    }
    
    qDebug() << "Parsed coordinates:" << x << "," << y;
    return QPoint(x, y);
}

int SemanticAnalyzer::parseMouseButton(const std::vector<std::string>& options) {
    // Default to left click for each new statement
    int mouseButton = Qt::LeftButton;
    
    // Look for button specification in options
    for (const auto& option : options) {
        QString opt = QString::fromStdString(option).toLower();  // Convert to lowercase for case-insensitive comparison
        if (opt == "right" || opt == "r") {
            mouseButton = Qt::RightButton;
            break;  // Exit once we find a button specification
        } else if (opt == "middle" || opt == "m") {
            mouseButton = Qt::MiddleButton;
            break;  // Exit once we find a button specification
        }
    }
    
    qDebug() << "Parsed mouse button:" << mouseButton << "from options:" << options;
    return mouseButton;
}
