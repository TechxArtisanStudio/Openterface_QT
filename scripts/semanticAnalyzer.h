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
#include <QRegularExpression>
#include <QObject>

struct MouseParams{
    uint8_t mode;
    uint8_t mouseButton;
    uint8_t wheelDelta;
    Coordinate coord;
};
class SemanticAnalyzer : public QObject {
    Q_OBJECT

public:
    SemanticAnalyzer(MouseManager* mouseManager, KeyboardMouse* keyboardMouse, QObject* parent = nullptr);
    void analyze(const ASTNode* node);


signals:
    void captureImg(const QString& path = "");
    void captureAreaImg(const QString& path = "", const QRect& captureArea = QRect());
    
private:
    MouseManager* mouseManager;
    KeyboardMouse* keyboardMouse;
    void analyzeCommandStetement(const CommandStatementNode* node);
    void analyzeClickStatement(const CommandStatementNode* node);
    void analyzeSendStatement(const CommandStatementNode* node);
    QPoint parseCoordinates(const std::vector<std::string>& options);
    int parseMouseButton(const std::vector<std::string>& options);
    void resetParameters();
    
    void analyzeSleepStatement(const CommandStatementNode* node);
    void analyzeMouseMove(const CommandStatementNode* node);
    void analyzeLockState(const CommandStatementNode* node, const QString& keyName, bool (KeyboardMouse::*getStateFunc)());
    void analyzeFullScreenCapture(const CommandStatementNode* node);
    void analyzeAreaScreenCapture(const CommandStatementNode* node);
    QString extractFilePath(const QString& originText);

    
    QRegularExpression onRegex{QString("^(1|True|On)$"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression offRegex{QString("^(0|False|Off)$"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression sendEmbedRegex{QString(R"(\{Click\s*([^}]*)\})"),QRegularExpression::CaseInsensitiveOption};
    QRegularExpression numberRegex{QString(R"(\d+)")};
    QRegularExpression buttonRegex{QString(R"((?<![a-zA-Z])(right|R|middle|M|left|L)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression downUpRegex{QString(R"((?<![a-zA-Z])(down|D|Up|U)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression relativeRegex{QString(R"((?<![a-zA-Z])(rel|relative)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression braceKeyRegex{QString(R"(\{([^}]+)\})"), QRegularExpression::CaseInsensitiveOption};
    QRegularExpression controlKeyRegex{QString(R"(([!^+#])((?:\{[^}]+\}|[^{])+))")};
    
    MouseParams parserClickParam(const QString& command);
};

#endif // SEMANTIC_ANALYZER_H
