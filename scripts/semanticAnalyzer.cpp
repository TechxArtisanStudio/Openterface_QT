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
#include <QLoggingCategory>
#include <QString>
#include <QThread>
#include "KeyboardMouse.h"
#include "global.h"


Q_LOGGING_CATEGORY(log_script, "opf.scripts")

SemanticAnalyzer::SemanticAnalyzer(MouseManager* mouseManager, KeyboardMouse* keyboardMouse, QObject* parent)
    : QObject(parent), mouseManager(mouseManager), keyboardMouse(keyboardMouse) {
    if (!mouseManager) {
        qCDebug(log_script) << "MouseManager is not initialized!";
    }
}

void SemanticAnalyzer::analyzeTree(std::shared_ptr<ASTNode> tree) {
    if (!tree) {
        qCDebug(log_script) << "analyzeTree: null tree";
        emit analysisFinished(false);
        return;
    }
    currentTree = std::move(tree);
    bool ok = analyze(currentTree.get());
    emit analysisFinished(ok);
}

bool SemanticAnalyzer::analyze(const ASTNode* node) {
    if (!node) {
        qCDebug(log_script) << "Received null node in analyze method.";
        return false;
    }

    bool analysisSuccess = true;
    switch (node->getType()) {
        case ASTNodeType::StatementList:
            // Process each statement in the list
            for (const auto& child : node->getChildren()) {
                qCDebug(log_script) << "Analyzing child node.";
                if (!analyze(child.get())){
                    analysisSuccess = false;
                }
                // resetParameters(); // Reset after each statement
            }
            break;
            
        case ASTNodeType::CommandStatement:
            qCDebug(log_script) << "Analyzing command statement.";
            emit commandIncrease();
            // Execute all commands directly in SemanticAnalyzer
            {
                const CommandStatementNode* cmd = static_cast<const CommandStatementNode*>(node);
                qCDebug(log_script) << "Command name:" << cmd->getCommandName();
                analyzeCommandStatement(cmd);
            }
            break;
            
        default:
            // Process any child nodes
            for (const auto& child : node->getChildren()) {
                qCDebug(log_script) << "Analyzing default child node.";
                if (!analyze(child.get())){
                    analysisSuccess = false;
                }
            }
            break;
    }
    return analysisSuccess;
}

void SemanticAnalyzer::resetParameters() {
    if (mouseManager) {
        // Reset mouse manager state
        mouseManager->reset();
        qCDebug(log_script) << "Reset parameters for next statement";
    } else {
        qCDebug(log_script) << "MouseManager is not available for reset!";
    }
}

void SemanticAnalyzer::analyzeCommandStatement(const CommandStatementNode* node){
    QString commandName = node->getCommandName();
    
    if(commandName == "Click"){
        analyzeClickStatement(node);
    }
    if(commandName == "MouseMove"){
        analyzeMouseMove(node);
    }
    if(commandName == "Send"){
        analyzeSendStatement(node);
    }
    if(commandName == "Sleep"){
        analyzeSleepStatement(node);
    }
    if(commandName == "SetCapsLockState"){
        analyzeLockState(node, "CapsLock", &KeyboardMouse::getCapsLockState_);
    }
    if(commandName == "SetNumLockState"){
        analyzeLockState(node, "NumLock", &KeyboardMouse::getNumLockState_);
    }
    if(commandName == "SetScrollLockState"){
        analyzeLockState(node, "ScrollLcok", &KeyboardMouse::getScrollLockState_);
    }
    if(commandName == "FullScreenCapture"){
        analyzeFullScreenCapture(node);
    }
    if(commandName == "AreaScreenCapture"){
        analyzeAreaScreenCapture(node);
    }
}

void SemanticAnalyzer::analyzeAreaScreenCapture(const CommandStatementNode* node){
    const auto& options = node->getOptions();
    if (options.empty()){
        qCDebug(log_script) << "No param given";
        return;
    }
    QString path;
    QString tmpTxt;
    QStringList numTmp;
    qCDebug(log_script) << "Capturing area img";
    for (const auto& token : options){
        if (token != "\"") tmpTxt.append(QString::fromStdString(token));
    }
    path = extractFilePath(tmpTxt);
    QRegularExpressionMatchIterator numMatchs = regex.numberRegex.globalMatch(tmpTxt);
    while(numMatchs.hasNext()){
        QRegularExpressionMatch nummatch = numMatchs.next();
        numTmp.append(nummatch.captured(0));
    }
    std::vector<int> numData;
    for (const QString & num : numTmp){
        bool ok;
        int value = num.toInt(&ok);
        if (ok){
            numData.push_back(value);
        }
    }
    if (numData.size()<4) {
        qCDebug(log_script) << "the param of area rect is x y width height";
        return;
    }
    QRegularExpression regex("\\\\");
    path.replace(regex, "/");
    QRect area = QRect(numData[0], numData[1], numData[2], numData[3]);
    emit captureAreaImg(path, area);
}

void SemanticAnalyzer::analyzeFullScreenCapture(const CommandStatementNode* node){
    const auto& options = node->getOptions();
    QString path;
    QString tmpTxt;
    if (options.empty()){
        qCDebug(log_script) << "No path given";
        QString path = "";
        emit captureImg(path);
        return;
    }
    for (const auto& token : options){
        if (token != "\"") tmpTxt.append(QString::fromStdString(token));
    }
    path = extractFilePath(tmpTxt);
    QRegularExpression regex("\\\\");
    path.replace(regex, "/");
    emit captureImg(path);
}

QString SemanticAnalyzer::extractFilePath(const QString& originText){
    QRegularExpression regex(R"(([a-zA-Z]:[\\\/][^\s]+|\/[^\s]+))");
    QRegularExpressionMatch match = regex.match(originText);
    if (match.hasMatch()){
        return match.captured(0);
    }
    return QString();
}

void SemanticAnalyzer::analyzeLockState(const CommandStatementNode* node, const QString& keyName, bool (KeyboardMouse::*getStateFunc)()){
    const auto& options = node->getOptions();
    std::array<uint8_t, 6> general = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (options.empty()){
        qCDebug(log_script) << "Please enter parameters.";
        return;
    }
    QString tmpKeys;
    for (const auto& token : options){
        if (token != "\"") tmpKeys.append(QString::fromStdString(token));
    }
    tmpKeys.remove(' ');
    qCDebug(log_script) << tmpKeys;
    if (regex.onRegex.match(tmpKeys).hasMatch()){
        qCDebug(log_script) << keyName << " on";
        keyboardMouse->updateNumCapsScrollLockState();
        if (!(keyboardMouse->*getStateFunc)()){
            general[0] = keydata.value(keyName);
            keyPacket pack(general);
            keyboardMouse->addKeyPacket(pack);
            keyboardMouse->dataSend();
        }
    }
    if (regex.offRegex.match(tmpKeys).hasMatch()){
        qCDebug(log_script) << keyName << " off";
        keyboardMouse->updateNumCapsScrollLockState();
        if ((keyboardMouse->*getStateFunc)()) {
            general[0] = keydata.value(keyName);
            keyPacket pack(general);
            keyboardMouse->addKeyPacket(pack);
            keyboardMouse->dataSend();
        }
    }
}

void SemanticAnalyzer::analyzeSleepStatement(const CommandStatementNode* node){
    const auto& options = node->getOptions();

    if (options.empty()){
        qCDebug(log_script) << "No sleep time set";
        return;
    }
    for (const auto& token : options){
        // Assuming the first option is the sleep time in milliseconds
        bool ok;
        int sleepTime = QString::fromStdString(token).toInt(&ok);
        
        if (!ok || sleepTime < 0) {
            continue; // Exit if the sleep time is invalid
        }else{
            qCDebug(log_script) << "Sleeping for" << sleepTime << "milliseconds";
            QThread::msleep(sleepTime); // Introduce the delay
        }
    }
}

void SemanticAnalyzer::analyzeSendStatement(const CommandStatementNode* node) {
    const auto& options = node->getOptions();
    
    if (options.empty()) {
        qCDebug(log_script) << "No keys provided for Send command";
        return;
    }

    if (!keyboardMouse) {
        qCDebug(log_script) << "Send: keyboardMouse is null";
        return;
    }

    // Build the key string from options
    QString tmpKeys;
    bool append = false;
    for (const auto& token : options) {
        if (token == "\"") append = true;
        if (append) tmpKeys.append(QString::fromStdString(token));
    }
    tmpKeys.replace(QRegularExpression("^\"|\"$"), "");
    qCDebug(log_script) << "Processing keys:" << tmpKeys;

    int pos = 0;
    int packetCount = 0;
    const int MAX_PACKETS = 50;
    
    while (pos < tmpKeys.length() && packetCount < MAX_PACKETS) {
        std::array<uint8_t, 6> general = {0x00,0x00,0x00,0x00,0x00,0x00};
        uint8_t control = 0x00;

        // Check brace key first (e.g., {Enter}, {Tab})
        QRegularExpressionMatch braceMatch = regex.braceKeyRegex.match(tmpKeys, pos);
        if (braceMatch.hasMatch() && braceMatch.capturedStart() == pos) {
            QString keyName = braceMatch.captured(1);
            
            // Check if this is a Click command
            if (keyName.startsWith("Click", Qt::CaseInsensitive)) {
                // Extract coordinates from "Click x, y"
                QRegularExpression clickRegex(R"(Click\s+(\d+)\s*,\s*(\d+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch clickMatch = clickRegex.match(keyName);
                
                if (clickMatch.hasMatch() && mouseManager) {
                    int x = clickMatch.captured(1).toInt();
                    int y = clickMatch.captured(2).toInt();
                    qCDebug(log_script) << "Send: executing click at:" << x << "," << y;
                    
                    // Send any pending keyboard packets first
                    if (packetCount > 0) {
                        qCDebug(log_script) << "Send: sending" << packetCount << "packets before click";
                        keyboardMouse->dataSend();
                        packetCount = 0;
                    }
                    
                    // Perform mouse click: press, wait, release
                    mouseManager->handleAbsoluteMouseAction(x, y, Qt::LeftButton, 0);  // Press
                    QThread::msleep(5);  // Wait 5ms
                    mouseManager->handleAbsoluteMouseAction(x, y, 0, 0);  // Release
                } else {
                    qCDebug(log_script) << "Send: invalid Click format or mouseManager null:" << keyName;
                }
                pos = braceMatch.capturedEnd();
                continue;
            }
            
            // Normalize key name: capitalize first letter
            if (!keyName.isEmpty()) {
                keyName = keyName.at(0).toUpper() + keyName.mid(1).toLower();
            }
            if (keydata.contains(keyName)) {
                general[0] = keydata.value(keyName);
                keyPacket pack(general, control);
                keyboardMouse->addKeyPacket(pack);
                packetCount++;
                qCDebug(log_script) << "Added brace key press:" << keyName;
                
                // Send key release
                std::array<uint8_t, 6> release = {0x00,0x00,0x00,0x00,0x00,0x00};
                keyPacket releasePack(release, 0x00);
                keyboardMouse->addKeyPacket(releasePack);
                packetCount++;
            } else {
                qCDebug(log_script) << "Send: unsupported brace key:" << keyName;
                return;
            }
            pos = braceMatch.capturedEnd();
            continue;
        }

        // Handle simple character
        QChar ch = tmpKeys[pos];
        if (ch.isUpper()) control = 0x02;  // Shift modifier
        QString chStr(ch);
        
        if (keydata.contains(chStr)) {
            general[0] = keydata.value(chStr);
            keyPacket pack(general, control);
            keyboardMouse->addKeyPacket(pack);
            packetCount++;
            qCDebug(log_script) << "Added char press:" << ch;
            
            // Send key release
            std::array<uint8_t, 6> release = {0x00,0x00,0x00,0x00,0x00,0x00};
            keyPacket releasePack(release, 0x00);
            keyboardMouse->addKeyPacket(releasePack);
            packetCount++;
        } else {
            qCDebug(log_script) << "Send: unsupported char:" << ch;
            return;
        }
        pos++;
    }
    
    if (packetCount >= MAX_PACKETS) {
        qCDebug(log_script) << "Send: packet count exceeded limit";
        return;
    }

    qCDebug(log_script) << "Send: sending" << packetCount << "packets";
    keyboardMouse->dataSend();
}

void SemanticAnalyzer::analyzeClickStatement(const CommandStatementNode* node) {
    const auto& options = node->getOptions();
    if (options.empty()) {
        qCDebug(log_script) << "No coordinates provided for Click command";
        return;
    }
    
    if (!mouseManager) {
        qCDebug(log_script) << "Error: MouseManager is not initialized, cannot process Click command";
        return;
    }
    
    // for(const auto& token : options){
        
    // }
    // Parse coordinates and mouse button from options
    QPoint coords = parseCoordinates(options);
    int mouseButton = parseMouseButton(options);  // This will be fresh for each statement

    qCDebug(log_script) << "Executing click at:" << coords.x() << "," << coords.y() 
             << "with button:" << mouseButton;

    try {
        // Perform mouse click: press, wait, release
        mouseManager->handleAbsoluteMouseAction(coords.x(), coords.y(), mouseButton, 0);  // Press
        QThread::msleep(50);  // Wait 50ms
        mouseManager->handleAbsoluteMouseAction(coords.x(), coords.y(), 0, 0);  // Release
    } catch (const std::exception& e) {
        qCDebug(log_script) << "Exception caught in handleAbsoluteMouseAction:" << e.what();
    } catch (...) {
        qCDebug(log_script) << "Unknown exception caught in handleAbsoluteMouseAction.";
    }
}

QPoint SemanticAnalyzer::parseCoordinates(const std::vector<std::string>& options) {
    if (options.empty()) {
        qCDebug(log_script) << "No coordinate components";
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
        qCDebug(log_script) << "Invalid coordinate format, using defaults";
        return QPoint(0, 0);
    }
    
    qCDebug(log_script) << "Parsed coordinates:" << x << "," << y;
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
    
    // qCDebug(log_script) << "Parsed mouse button:" << mouseButton << "from options:" << options;

    return mouseButton;
}

void SemanticAnalyzer::analyzeMouseMove(const CommandStatementNode* node) {
    const auto& options = node->getOptions();
    if (options.empty()) {
        qCDebug(log_script) << "No coordinates provided for Move command";
        return;
    }
    
    if (!mouseManager) {
        qCDebug(log_script) << "Error: MouseManager is not initialized, cannot process Move command";
        return;
    }
    
    // Parse coordinates from options
    QPoint coords = parseCoordinates(options);

    qCDebug(log_script) << "Executing move to:" << coords.x() << "," << coords.y();

    try {
        // Move without clicking (mouseButton = 0)
        mouseManager->handleAbsoluteMouseAction(coords.x(), coords.y(), 0, 0);
    } catch (const std::exception& e) {
        qCDebug(log_script) << "Exception caught in handleAbsoluteMouseAction:" << e.what();
    } catch (...) {
        qCDebug(log_script) << "Unknown exception caught in handleAbsoluteMouseAction.";
    }
}

MouseParams SemanticAnalyzer::parserClickParam(const QString& command) {
    // match the number param
    QStringList numTmp;
    bool relative = false;
    QString button;
    QString downOrUp;
    MouseParams params = {0x02, 0x00, 0x00, {}}; // Default to absolute mode

    QRegularExpressionMatch relativeMatch = regex.relativeRegex.match(command);
    if(relativeMatch.hasMatch()){
        relative = true;
        params.mode = 0x01; // Relative mode
        qCDebug(log_script) << "Matched relative:" << relative;
    }

    QRegularExpressionMatchIterator numMatchs = regex.numberRegex.globalMatch(command);
    while(numMatchs.hasNext()){
        QRegularExpressionMatch nummatch = numMatchs.next();
        numTmp.append(nummatch.captured(0));
    }
    qCDebug(log_script) << "Matched numbers:" << numTmp;
    
    // check the "" content
    QRegularExpressionMatch buttonMatch = regex.buttonRegex.match(command);
    if(buttonMatch.hasMatch()){
        button = buttonMatch.captured(0);
        qCDebug(log_script) << "Matched button:" << button;
    }
    QRegularExpressionMatch downUpMatch = regex.downUpRegex.match(command);
    if(downUpMatch.hasMatch()){
        downOrUp = downUpMatch.captured(0);
        qCDebug(log_script) << "Matched downOrUp:" << downOrUp;
    }
    
    // Convert numbers to integers
    std::vector<int> numData;
    for (const QString & num : numTmp){
        bool ok;
        int value = num.toInt(&ok);
        if (ok){
            numData.push_back(value);
        }
    }
    
    // Set mouse button based on parsed button string
    if (button.toLower().startsWith('r')) {
        params.mouseButton = 0x02; // Right button
    } else if (button.toLower().startsWith('m')) {
        params.mouseButton = 0x04; // Middle button
    } else {
        params.mouseButton = 0x01; // Left button (default)
    }

    // Set coordinates
    if (numData.size() >= 2) {
        if (relative) {
            // Relative coordinates are single bytes
            params.coord.rel.x = static_cast<uint8_t>(std::min(std::max(numData[0], -128), 127) & 0xFF);
            params.coord.rel.y = static_cast<uint8_t>(std::min(std::max(numData[1], -128), 127) & 0xFF);
            qCDebug(log_script) << "rel coordinates: " << (int)params.coord.rel.x << ", " << (int)params.coord.rel.y;
        } else {
            // Absolute coordinates are 2 bytes each
            int x = (numData[0] * 4096) / GlobalVar::instance().getInputWidth();
            int y = (numData[1] * 4096) / GlobalVar::instance().getInputHeight();
            
            params.coord.abs.x[0] = static_cast<uint8_t>(x & 0xFF);
            params.coord.abs.x[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
            params.coord.abs.y[0] = static_cast<uint8_t>(y & 0xFF);
            params.coord.abs.y[1] = static_cast<uint8_t>((y >> 8) & 0xFF);
            qCDebug(log_script) << "abs coordinates: " << x << " " << GlobalVar::instance().getInputWidth() 
                             << ", " << y << " " << GlobalVar::instance().getInputHeight();
        }
    }

    qCDebug(log_script) << "mouse mode" << params.mode << "mouse button" << params.mouseButton;
    return params;

    // Create and add the key packet
    // keyPacket pack(Mode, _mouseButton, 0x00, coord); // Last param 0x00 is mouseRollWheel
    // qCDebug(log_script) << "after key packet";
    // keyboardMouse->addKeyPacket(pack);
}
