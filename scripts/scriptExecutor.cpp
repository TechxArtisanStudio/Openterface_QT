#include "scriptExecutor.h"
#include <QDebug>

Q_LOGGING_CATEGORY(log_scriptexec, "opf.ui.scriptexec")

ScriptExecutor::ScriptExecutor(QObject* parent)
    : QObject(parent)
{
}

void ScriptExecutor::onCommandData(const QString& commandName, const QStringList& options) {
    qWarning(log_scriptexec) << "onCommandData CALLED for command:" << commandName << "with options:" << options;
    bool result = executeCommand(commandName, options);
    qWarning(log_scriptexec) << "executeCommand returned:" << result;
}

bool ScriptExecutor::executeCommand(const QString& commandName, const QStringList& options) {
    qWarning(log_scriptexec) << "executeCommand CALLED for command:" << commandName;

    // Convert QStringList to std::vector<std::string> for compatibility with rest of code
    std::vector<std::string> stdOptions;
    for (const QString& opt : options) {
        stdOptions.push_back(opt.toStdString());
    }

    auto parseCoordinates = [&](const std::vector<std::string>& opts) -> QPoint {
        int x = 0, y = 0;
        bool okX = false, okY = false;
        int coordCount = 0;
        
        for (const auto& token : opts) {
            if (token == ",") continue; // Skip commas
            
            bool ok = false;
            int value = QString::fromStdString(token).toInt(&ok);
            if (ok && coordCount < 2) {
                if (coordCount == 0) { x = value; okX = true; }
                else if (coordCount == 1) { y = value; okY = true; }
                coordCount++;
            }
        }
        
        // Require both coordinates to be parsed
        if (!okX || !okY) {
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

    if (commandName == "MouseMove") {
        QPoint coords = parseCoordinates(stdOptions);
        qCDebug(log_scriptexec) << "Executing mouse move to:" << coords.x() << coords.y();
        try {
            if (mouseManager) mouseManager->handleAbsoluteMouseAction(coords.x(), coords.y(), 0, 0);
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

    if (commandName == "Click") {
        QPoint coords = parseCoordinates(stdOptions);
        int mouseButton = parseMouseButton(stdOptions);
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
        if (stdOptions.empty()) {
            qCDebug(log_scriptexec) << "No path given for FullScreenCapture";
            emit captureImg("");
            return true;
        }
        for (const auto& token : stdOptions) if (token != "\"") tmpTxt.append(QString::fromStdString(token));
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
        for (const auto& token : stdOptions) if (token != "\"") tmpTxt.append(QString::fromStdString(token));
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
        for (const auto& token : stdOptions) if (token != "\"") tmpKeys.append(QString::fromStdString(token));
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
        qWarning(log_scriptexec) << "Send command options count:" << stdOptions.size();
        for (size_t i = 0; i < stdOptions.size(); i++) {
            qWarning(log_scriptexec) << "  Option" << i << ":" << QString::fromStdString(stdOptions[i]);
        }
        
        QString tmpKeys;
        bool append = false;
        for (const auto& token : stdOptions) {
            if (token == "\"") append = true;
            if (append) tmpKeys.append(QString::fromStdString(token));
        }
        tmpKeys.replace(QRegularExpression("^\"|\"$"), "");
        qWarning(log_scriptexec) << "Processing keys:" << tmpKeys << "length:" << tmpKeys.length();
        qWarning(log_scriptexec) << "tmpKeys hex dump:";
        for (int i = 0; i < tmpKeys.length(); i++) {
            qWarning(log_scriptexec) << "  [" << i << "]:" << tmpKeys[i] << "code:" << (int)tmpKeys[i].unicode();
        }

        if (!keyboardMouse) {
            qCDebug(log_scriptexec) << "Send: keyboardMouse is null";
            return false;
        }

        int pos = 0;
        int packetCount = 0;
        const int MAX_PACKETS = 50;  // Reasonable limit to prevent buffer overflow
        
        qWarning(log_scriptexec) << "Starting packet generation loop, tmpKeys length:" << tmpKeys.length();
        
        while (pos < tmpKeys.length() && packetCount < MAX_PACKETS) {
            qWarning(log_scriptexec) << "--- Loop iteration: pos=" << pos << "packetCount=" << packetCount;
            std::array<uint8_t, 6> general = {0x00,0x00,0x00,0x00,0x00,0x00};
            uint8_t control = 0x00;

            // Check brace key first
            QRegularExpressionMatch braceMatch = regex.braceKeyRegex.match(tmpKeys, pos);
            if (braceMatch.hasMatch() && braceMatch.capturedStart() == pos) {
                QString keyName = braceMatch.captured(1);
                qWarning(log_scriptexec) << "Found brace key:" << keyName;
                // Normalize key name: capitalize first letter for proper keydata lookup
                if (!keyName.isEmpty()) {
                    keyName = keyName.at(0).toUpper() + keyName.mid(1).toLower();
                }
                if (keydata.contains(keyName)) {
                    general[0] = keydata.value(keyName);
                    keyPacket pack(general, control);
                    keyboardMouse->addKeyPacket(pack);
                    packetCount++;
                    qWarning(log_scriptexec) << "Added brace key press packet:" << keyName << "code:" << (int)general[0];
                    
                    // Send key release
                    std::array<uint8_t, 6> release = {0x00,0x00,0x00,0x00,0x00,0x00};
                    keyPacket releasePack(release, 0x00);
                    keyboardMouse->addKeyPacket(releasePack);
                    packetCount++;
                    qWarning(log_scriptexec) << "Added brace key release packet";
                } else {
                    qWarning(log_scriptexec) << "Send: unsupported brace key:" << keyName;
                    return false;
                }
                pos = braceMatch.capturedEnd();
                qWarning(log_scriptexec) << "After brace key: pos=" << pos;
                continue;
            }

            // Handle simple char
            QChar ch = tmpKeys[pos];
            if (ch.isUpper()) control = 0x02;
            QString chStr(ch);
            qWarning(log_scriptexec) << "Processing simple char at pos" << pos <<  ":" << ch << "keydata contains?" << keydata.contains(chStr);
            
            // Check if character is in keydata before using it
            if (keydata.contains(chStr)) {
                general[0] = keydata.value(chStr);
                keyPacket pack(general, control);
                keyboardMouse->addKeyPacket(pack);
                packetCount++;
                qWarning(log_scriptexec) << "Added char press packet:" << ch << "keycode:" << (int)general[0];
                
                // Send key release
                std::array<uint8_t, 6> release = {0x00,0x00,0x00,0x00,0x00,0x00};
                keyPacket releasePack(release, 0x00);
                keyboardMouse->addKeyPacket(releasePack);
                packetCount++;
                qWarning(log_scriptexec) << "Added char release packet";
            } else {
                qWarning(log_scriptexec) << "Send: unsupported char:" << ch;
                return false;  // Return error for unsupported character
            }
            pos++;
        }
        
        if (packetCount >= MAX_PACKETS) {
            qWarning(log_scriptexec) << "Send: packet count exceeded limit";
            return false;
        }

        qWarning(log_scriptexec) << "Send: sending" << packetCount << "packets";
        keyboardMouse->dataSend();
        return true;
    }

    qCDebug(log_scriptexec) << "executeCommand: unsupported command:" << commandName;
    return false;
}