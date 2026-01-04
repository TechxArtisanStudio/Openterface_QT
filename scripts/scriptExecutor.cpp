#include "scriptExecutor.h"
#include <QDebug>

Q_LOGGING_CATEGORY(log_scriptexec, "opf.ui.scriptexec")

ScriptExecutor::ScriptExecutor(QObject* parent)
    : QObject(parent)
{
}

bool ScriptExecutor::executeCommand(const ASTNode* node) {
    if (!node) {
        qCDebug(log_scriptexec) << "executeCommand: null node";
        return false;
    }
    if (node->getType() != ASTNodeType::CommandStatement) {
        qCDebug(log_scriptexec) << "executeCommand: not a command node";
        return true;
    }

    const CommandStatementNode* cmdNode = static_cast<const CommandStatementNode*>(node);
    QString commandName = cmdNode->getCommandName();
    const auto& options = cmdNode->getOptions();

    qCDebug(log_scriptexec) << "executeCommand on main thread for command:" << commandName;

    auto parseCoordinates = [&](const std::vector<std::string>& opts) -> QPoint {
        int x = 0, y = 0;
        bool foundComma = false;
        bool beforeComma = true;
        bool okX = false, okY = false;
        for (const auto& token : opts) {
            if (token == ",") {
                foundComma = true;
                beforeComma = false;
                continue;
            }
            bool ok = false;
            int value = QString::fromStdString(token).toInt(&ok);
            if (ok) {
                if (beforeComma) { x = value; okX = true; } else { y = value; okY = true; }
            }
        }
        if (!foundComma || (!okX && !okY)) {
            return QPoint(0, 0);
        }
        return QPoint(x, y);
    };

    auto parseMouseButton = [&](const std::vector<std::string>& opts) -> int {
        int mouseButton = Qt::LeftButton;
        for (const auto& option : opts) {
            QString opt = QString::fromStdString(option).toLower();
            if (opt == "right" || opt == "r") { mouseButton = Qt::RightButton; break; }
            else if (opt == "middle" || opt == "m") { mouseButton = Qt::MiddleButton; break; }
        }
        return mouseButton;
    };

    if (commandName == "Click") {
        QPoint coords = parseCoordinates(options);
        int mouseButton = parseMouseButton(options);
        qCDebug(log_scriptexec) << "Executing click at:" << coords.x() << coords.y() << "button:" << mouseButton;
        try {
            if (mouseManager) mouseManager->handleAbsoluteMouseAction(coords.x(), coords.y(), mouseButton, 0);
            else qCDebug(log_scriptexec) << "No mouseManager available";
            return true;
        } catch (const std::exception& e) {
            qCDebug(log_scriptexec) << "Exception in handleAbsoluteMouseAction:" << e.what();
            return false;
        } catch (...) {
            qCDebug(log_scriptexec) << "Unknown exception in handleAbsoluteMouseAction";
            return false;
        }
    }

    if (commandName == "FullScreenCapture") {
        QString path;
        QString tmpTxt;
        if (options.empty()) {
            qCDebug(log_scriptexec) << "No path given for FullScreenCapture";
            emit captureImg("");
            return true;
        }
        for (const auto& token : options) if (token != "\"") tmpTxt.append(QString::fromStdString(token));
        QRegularExpression regexPath(R"(([a-zA-Z]:[\\/][^\s]+|\/[^\s]+))");
        QRegularExpressionMatch m = regexPath.match(tmpTxt);
        if (m.hasMatch()) {
            path = m.captured(0);
            QRegularExpression backslash("\\\\");
            path.replace(backslash, "/");
        }
        emit captureImg(path);
        return true;
    }

    if (commandName == "AreaScreenCapture") {
        QString path;
        QString tmpTxt;
        QStringList nums;
        for (const auto& token : options) if (token != "\"") tmpTxt.append(QString::fromStdString(token));
        QRegularExpression numRe(R"((-?\d+))");
        QRegularExpressionMatchIterator it = numRe.globalMatch(tmpTxt);
        while (it.hasNext()) nums.append(it.next().captured(0));
        std::vector<int> numData;
        for (const QString& s : nums) { bool ok; int v = s.toInt(&ok); if (ok) numData.push_back(v); }
        if (numData.size() < 4) { qCDebug(log_scriptexec) << "Invalid area params"; return false; }
        QRegularExpression backslash("\\\\"); tmpTxt.replace(backslash, "/");
        QRect area(numData[0], numData[1], numData[2], numData[3]);
        emit captureAreaImg("", area);
        return true;
    }

    if (commandName == "SetCapsLockState" || commandName == "SetNumLockState" || commandName == "SetScrollLockState") {
        QString keyName = (commandName == "SetCapsLockState") ? "CapsLock" : (commandName == "SetNumLockState" ? "NumLock" : "ScrollLcok");
        QString tmpKeys;
        for (const auto& token : options) if (token != "\"") tmpKeys.append(QString::fromStdString(token));
        tmpKeys.remove(' ');
        if (regex.onRegex.match(tmpKeys).hasMatch()) {
            if (keyboardMouse) {
                keyboardMouse->updateNumCapsScrollLockState();
                bool current = (commandName == "SetCapsLockState") ? keyboardMouse->getCapsLockState_() : (commandName == "SetNumLockState" ? keyboardMouse->getNumLockState_() : keyboardMouse->getScrollLockState_());
                if (!current) {
                    std::array<uint8_t, 6> general = {0x00,0x00,0x00,0x00,0x00,0x00};
                    general[0] = keydata.value(keyName);
                    keyPacket pack(general);
                    keyboardMouse->addKeyPacket(pack);
                    keyboardMouse->dataSend();
                }
            }
            return true;
        }
        if (regex.offRegex.match(tmpKeys).hasMatch()) {
            if (keyboardMouse) {
                keyboardMouse->updateNumCapsScrollLockState();
                bool current = (commandName == "SetCapsLockState") ? keyboardMouse->getCapsLockState_() : (commandName == "SetNumLockState" ? keyboardMouse->getNumLockState_() : keyboardMouse->getScrollLockState_());
                if (current) {
                    std::array<uint8_t, 6> general = {0x00,0x00,0x00,0x00,0x00,0x00};
                    general[0] = keydata.value(keyName);
                    keyPacket pack(general);
                    keyboardMouse->addKeyPacket(pack);
                    keyboardMouse->dataSend();
                }
            }
            return true;
        }
        return false;
    }

    if (commandName == "Send") {
        QString tmpKeys;
        bool append = false;
        for (const auto& token : options) {
            if (token == "\"") append = true;
            if (append) tmpKeys.append(QString::fromStdString(token));
        }
        tmpKeys.replace(QRegularExpression("^\"|\"$"), "");
        qCDebug(log_scriptexec) << "Processing keys:" << tmpKeys;

        int pos = 0;
        while (pos < tmpKeys.length()) {
            std::array<uint8_t, 6> general = {0x00,0x00,0x00,0x00,0x00,0x00};
            uint8_t control = 0x00;

            // Check brace key first
            QRegularExpressionMatch braceMatch = regex.braceKeyRegex.match(tmpKeys, pos);
            if (braceMatch.hasMatch() && braceMatch.capturedStart() == pos) {
                QString keyName = braceMatch.captured(1);
                if (keydata.contains(keyName)) {
                    general[0] = keydata.value(keyName);
                    keyPacket pack(general, control);
                    if (keyboardMouse) keyboardMouse->addKeyPacket(pack);
                } else {
                    qCDebug(log_scriptexec) << "Send: unsupported brace key:" << keyName;
                }
                pos = braceMatch.capturedEnd();
                continue;
            }

            // Handle simple char
            QChar ch = tmpKeys[pos];
            if (ch.isUpper()) control = 0x02;
            QString chStr(ch);
            if (keydata.contains(chStr)) {
                general[0] = keydata.value(chStr);
                keyPacket pack(general, control);
                if (keyboardMouse) keyboardMouse->addKeyPacket(pack);
            } else {
                qCDebug(log_scriptexec) << "Send: unsupported char:" << ch;
            }
            pos++;
        }
        if (keyboardMouse) keyboardMouse->dataSend();
        return true;
    }

    qCDebug(log_scriptexec) << "executeCommand: unsupported command:" << commandName;
    return false;
}