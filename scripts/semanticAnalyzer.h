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
#include "KeyboardMouse.h"
#include <memory>
#include <QPoint>
#include <QString>
#include <QRegularExpression>
#include <QObject>

class SemanticAnalyzer : public QObject {
    Q_OBJECT

public:
    SemanticAnalyzer(MouseManager* mouseManager, KeyboardMouse* keyboardMouse, QObject* parent = nullptr);
    void analyze(const ASTNode* node);

public:
    signals:
        void captureImg(const QString& path = "");

private:
    MouseManager* mouseManager;
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
    void analyzeMouseMove(const CommandStatementNode* node);
    void analyzeLockState(const CommandStatementNode* node, const QString& keyName, bool (KeyboardMouse::*getStateFunc)());
    void analyzeFullScreenCapture(const CommandStatementNode* node);
    void extractClickParameters(const QString& statement);
    void parserClickParam(const QString& command);

    QRegularExpression onRegex{QString("^(1|True|On)$"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression offRegex{QString("^(0|False|Off)$"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression sendEmbedRegex{QString(R"(\{Click\s*([^}]*)\})"),QRegularExpression::CaseInsensitiveOption};
    QRegularExpression numberRegex{QString(R"(\d+)")};
    QRegularExpression buttonRegex{QString(R"((?<![a-zA-Z])(right|R|middle|M|left|L)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression downUpRegex{QString(R"((?<![a-zA-Z])(down|D|Up|U)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression relativeRegex{QString(R"((?<![a-zA-Z])(rel|relative)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression braceKeyRegex{QString(R"(\{([^}]+)\})"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression controlKeyRegex{QString(R"(([!^+#])((?:\{[^}]+\}|[^{])+))")};
};

#endif // SEMANTIC_ANALYZER_H
