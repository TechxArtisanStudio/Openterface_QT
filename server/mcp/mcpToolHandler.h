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

#ifndef MCP_TOOL_HANDLER_H
#define MCP_TOOL_HANDLER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QThread>

class CameraManager;
class ScriptRunner;
class ScriptExecutor;
class ASTNode;

class McpToolHandler : public QObject {
    Q_OBJECT

public:
    explicit McpToolHandler(QObject *parent = nullptr);

    // Inject dependencies (same pattern as TcpServer::setCameraManager)
    void setCameraManager(CameraManager* cam);
    void setScriptRunner(ScriptRunner* runner);
    void setScriptExecutor(ScriptExecutor* executor);

    /** Return all tool definitions for "tools/list". */
    QJsonArray listTools() const;

    /** Execute a tool call for "tools/call". Returns {"content": [...]}. */
    QJsonObject callTool(const QString& name, const QJsonObject& arguments);

signals:
    /** Emitted when a script needs to be executed via ScriptRunner. */
    void syntaxTreeReady(std::shared_ptr<ASTNode> syntaxTree);

private:
    CameraManager* m_cameraManager = nullptr;
    ScriptRunner* m_scriptRunner = nullptr;
    ScriptExecutor* m_scriptExecutor = nullptr;

    // --- Individual tool implementations ---
    QJsonObject toolMouseMoveAbsolute(const QJsonObject& args);
    QJsonObject toolMouseClick(const QJsonObject& args);
    QJsonObject toolMouseMoveRelative(const QJsonObject& args);
    QJsonObject toolMouseScroll(const QJsonObject& args);
    QJsonObject toolKeyboardPressKey(const QJsonObject& args);
    QJsonObject toolKeyboardTypeText(const QJsonObject& args);
    QJsonObject toolKeyboardEnterKey();
    QJsonObject toolKeyboardSendKeys(const QJsonObject& args);
    QJsonObject toolKeyboardFunctionKey(const QJsonObject& args);
    QJsonObject toolKeyboardCtrlAltDel(const QJsonObject& args);
    QJsonObject toolKeyboardSetLayout(const QJsonObject& args);
    QJsonObject toolCaptureScreen(const QJsonObject& args);
    QJsonObject toolCaptureLastImage(const QJsonObject& args);
    QJsonObject toolExecuteScript(const QJsonObject& args);
    QJsonObject toolSystemStatus(const QJsonObject& args);

    // --- Helpers ---
    static QJsonObject textResult(const QString& text);
    static QJsonObject errorResult(const QString& message);
    static QJsonObject imageResult(const QByteArray& base64Data, const QString& mimeType = "image/jpeg");

    static int parseMouseButton(const QString& button);
};

#endif // MCP_TOOL_HANDLER_H
