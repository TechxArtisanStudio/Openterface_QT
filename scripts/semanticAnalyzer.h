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


#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H


#include "target/MouseManager.h"
// #include "target/KeyboardManager.h"
#include "KeyboardMouse.h"
#include <memory>
#include <QPoint>
#include <QString>

class SemanticAnalyzer {
public:
    SemanticAnalyzer(MouseManager* mouseManager, KeyboardMouse* keyboardMouse);
    void analyze(const ASTNode* node);

private:
    MouseManager* mouseManager;
    // KeyboardManager* keyboardManager;
    KeyboardMouse* keyboardMouse;
    void analyzeCommandStetement(const CommandStatementNode* node);
    void analyzeClickStatement(const CommandStatementNode* node);
    void analyzeSendStatement(const CommandStatementNode* node);
    QPoint parseCoordinates(const std::vector<std::string>& options);
    int parseMouseButton(const std::vector<std::string>& options);
    void resetParameters();
    void extractKeyFromBrace(const QString& tmpKeys, int& i, std::array<uint8_t, 6>& general, int genral_index = 0);
    void analyzeSleepStatement(const CommandStatementNode* node);
    void analyzeCapsLockState(const CommandStatementNode* node);
    void analyzeNumLockState(const CommandStatementNode* node);
    void analyzeScrollLockState(const CommandStatementNode* node);
    
};

#endif // SEMANTIC_ANALYZER_H
