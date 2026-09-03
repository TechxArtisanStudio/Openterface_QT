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

#include "SharedToolExecutor.h"
#include "host/cameramanager.h"
#include "host/HostManager.h"
#include "server/mcp/screenAnalyzer.h"
#include "target/MouseManager.h"
#include "log/opflogging.h"

#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QLoggingCategory>

OPF_LOGGING_CATEGORY(log_shared_tool, "opf.shared.tool")

SharedToolExecutor::SharedToolExecutor(QObject *parent)
    : QObject(parent)
    , m_screenAnalyzer(new ScreenAnalyzer())
{
}

SharedToolExecutor &SharedToolExecutor::instance()
{
    static SharedToolExecutor inst;
    return inst;
}

void SharedToolExecutor::setCameraManager(CameraManager *cam)
{
    m_cameraManager = cam;
}

// ==========================================================================
// detect_cursor
// ==========================================================================

QJsonObject SharedToolExecutor::detectCursor(const QJsonObject &args)
{
    if (!m_cameraManager) {
        return QJsonObject{{"error", "CameraManager not initialized"}};
    }
    if (!m_screenAnalyzer) {
        return QJsonObject{{"error", "ScreenAnalyzer not initialized"}};
    }

    // Parse parameters
    int samples = args.value("samples").toInt(5);
    int intervalMs = args.value("interval_ms").toInt(350);
    samples = qBound(3, samples, 8);
    intervalMs = qBound(200, intervalMs, 1000);

    qCDebug(log_shared_tool) << "detectCursor: sampling" << samples << "frames at"
                             << intervalMs << "ms intervals";

    // Capture frames at regular intervals
    QList<QImage> frames;
    frames.reserve(samples);

    for (int i = 0; i < samples; ++i) {
        QImage frame = m_cameraManager->getLatestOriginalFrame();
        if (frame.isNull()) {
            return QJsonObject{{"error", QString("No frame available from camera (got null frame at sample %1)").arg(i)}};
        }
        frames.append(frame);

        if (i < samples - 1) {
            QThread::msleep(intervalMs);
        }
    }

    qCDebug(log_shared_tool) << "detectCursor: captured" << frames.size() << "frames, analyzing";

    // Run cursor detection
    CursorDetectionResult result = m_screenAnalyzer->detectCursorFromFrames(frames);

    // Build result object
    QJsonObject response;
    response["detected"] = result.detected;
    response["status"] = result.status;
    response["confidence"] = static_cast<double>(result.confidence);
    response["description"] = result.description;

    if (result.detected && !result.position.isNull()) {
        QJsonObject position;
        position["pixel_x"] = result.position.x() + result.position.width() / 2;
        position["pixel_y"] = result.position.y() + result.position.height() / 2;
        position["mcp_x"] = result.mcpX;
        position["mcp_y"] = result.mcpY;
        position["width"] = result.position.width();
        position["height"] = result.position.height();
        response["cursor_position"] = position;
    }

    // Include individual signal details
    QJsonObject signalDetails;
    signalDetails["cursor_blink"] = result.cursorBlinkDetected;
    signalDetails["screen_stable"] = result.screenStable;
    signalDetails["total_change_ratio"] = static_cast<double>(result.totalChangeRatio);
    signalDetails["prompt_detected"] = result.promptDetected;
    if (result.promptDetected && !result.promptText.isEmpty()) {
        signalDetails["prompt_text"] = result.promptText;
    }
    response["signals"] = signalDetails;

    response["frames_analyzed"] = frames.size();
    response["total_duration_ms"] = (frames.size() - 1) * intervalMs;

    return response;
}

// ==========================================================================
// run_command_and_wait
// ==========================================================================

void SharedToolExecutor::typeText(const QString &text)
{
    HostManager &hm = HostManager::getInstance();
    static const QString shiftChars = "!@#$%^&*()_+{}|:\"<>?~";

    for (QChar ch : text) {
        int keyCode = ch.unicode();
        int modifiers = 0;

        if (ch == '\n' || ch == '\r') {
            keyCode = Qt::Key_Return;
        } else if (ch == '\t') {
            keyCode = Qt::Key_Tab;
        } else if (ch >= 'a' && ch <= 'z') {
            keyCode = Qt::Key_A + (ch.toLower().unicode() - 'a');
        } else if (ch >= 'A' && ch <= 'Z') {
            keyCode = Qt::Key_A + (ch.toUpper().unicode() - 'A');
            modifiers = Qt::ShiftModifier;
        } else if (shiftChars.contains(ch)) {
            modifiers = Qt::ShiftModifier;
        }

        hm.handleKeyboardAction(keyCode, modifiers, true);
        QCoreApplication::processEvents();
        QThread::msleep(50);

        hm.handleKeyboardAction(keyCode, modifiers, false);
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }

    QThread::msleep(100);
}

QJsonObject SharedToolExecutor::runCommandAndWait(const QJsonObject &args)
{
    if (!m_cameraManager) {
        return QJsonObject{{"error", "CameraManager not initialized"}};
    }
    if (!m_screenAnalyzer) {
        return QJsonObject{{"error", "ScreenAnalyzer not initialized"}};
    }

    // Parse parameters
    QString command = args.value("command").toString();
    int maxWaitMs = args.value("max_wait_ms").toInt(30000);
    int pollIntervalMs = args.value("poll_interval_ms").toInt(2000);
    int initialDelayMs = args.value("initial_delay_ms").toInt(1500);
    int samples = args.value("samples").toInt(5);
    int detectIntervalMs = args.value("detect_interval_ms").toInt(350);

    // Validate
    if (command.isEmpty()) {
        return QJsonObject{{"error", "command is required"}};
    }

    maxWaitMs = qBound(5000, maxWaitMs, 120000);
    pollIntervalMs = qBound(500, pollIntervalMs, 10000);
    initialDelayMs = qBound(0, initialDelayMs, 10000);
    samples = qBound(3, samples, 8);
    detectIntervalMs = qBound(200, detectIntervalMs, 1000);

    qCDebug(log_shared_tool) << "runCommandAndWait: command='" << command
                             << "' maxWait=" << maxWaitMs << "ms";

    // Append newline if not present
    if (!command.endsWith('\n') && !command.endsWith('\r')) {
        command += '\n';
    }

    // Type the command
    typeText(command);

    qCDebug(log_shared_tool) << "runCommandAndWait: typed" << command.length()
                             << "chars, waiting" << initialDelayMs << "ms for output";

    // Wait for output to start
    QThread::msleep(initialDelayMs);

    // Two-phase polling
    QElapsedTimer timer;
    timer.start();
    int pollCount = 0;
    CursorDetectionResult lastResult;
    lastResult.status = "unknown";
    bool sawOutput = false;
    bool success = false;

    while (timer.elapsed() < maxWaitMs) {
        // Capture frames
        QList<QImage> frames;
        frames.reserve(samples);
        bool captureOk = true;

        for (int i = 0; i < samples; ++i) {
            QImage frame = m_cameraManager->getLatestOriginalFrame();
            if (frame.isNull()) {
                captureOk = false;
                break;
            }
            frames.append(frame);
            if (i < samples - 1) {
                QThread::msleep(detectIntervalMs);
            }
        }

        if (!captureOk || frames.size() < 3) {
            return QJsonObject{{"error", "Failed to capture frames during polling"}};
        }

        // Run detection
        lastResult = m_screenAnalyzer->detectCursorFromFrames(frames);
        pollCount++;

        qCDebug(log_shared_tool) << "runCommandAndWait: poll" << pollCount
                                 << "status:" << lastResult.status
                                 << "sawOutput:" << sawOutput
                                 << "elapsed:" << timer.elapsed() << "ms";

        if (!sawOutput) {
            // Phase 1: Waiting for command to start producing output
            if (lastResult.status == "outputting") {
                sawOutput = true;
                qCDebug(log_shared_tool) << "runCommandAndWait: output detected, waiting for idle";
            } else if ((lastResult.status == "idle" || lastResult.status == "likely_idle")
                       && timer.elapsed() > initialDelayMs + 500) {
                // Fast command — finished before we caught outputting
                sawOutput = true;
                success = true;
                break;
            }
        } else {
            // Phase 2: We saw output, now waiting for idle
            if (lastResult.status == "idle" || lastResult.status == "likely_idle") {
                success = true;
                break;
            }
        }

        QThread::msleep(pollIntervalMs);
    }

    // Build response
    QJsonObject response;
    response["command"] = command.trimmed();
    response["wait_time_ms"] = static_cast<qint64>(timer.elapsed());
    response["poll_count"] = pollCount;
    response["saw_output"] = sawOutput;

    if (success) {
        response["success"] = true;
        response["status"] = "idle";

        // Include detection details
        QJsonObject detection;
        detection["detected"] = lastResult.detected;
        detection["confidence"] = static_cast<double>(lastResult.confidence);
        detection["status"] = lastResult.status;
        detection["description"] = lastResult.description;

        if (lastResult.detected && !lastResult.position.isNull()) {
            QJsonObject position;
            position["pixel_x"] = lastResult.position.x() + lastResult.position.width() / 2;
            position["pixel_y"] = lastResult.position.y() + lastResult.position.height() / 2;
            position["mcp_x"] = lastResult.mcpX;
            position["mcp_y"] = lastResult.mcpY;
            position["width"] = lastResult.position.width();
            position["height"] = lastResult.position.height();
            detection["cursor_position"] = position;
        }

        QJsonObject signalDetails;
        signalDetails["cursor_blink"] = lastResult.cursorBlinkDetected;
        signalDetails["screen_stable"] = lastResult.screenStable;
        signalDetails["total_change_ratio"] = static_cast<double>(lastResult.totalChangeRatio);
        signalDetails["prompt_detected"] = lastResult.promptDetected;
        if (lastResult.promptDetected && !lastResult.promptText.isEmpty()) {
            signalDetails["prompt_text"] = lastResult.promptText;
        }
        detection["signals"] = signalDetails;

        response["detection"] = detection;
        response["description"] = QString("Command executed and terminal is idle after %1ms (%2 polls)")
            .arg(timer.elapsed()).arg(pollCount);

        qCDebug(log_shared_tool) << "runCommandAndWait: SUCCESS after" << timer.elapsed()
                                 << "ms," << pollCount << "polls";
    } else {
        // Timeout
        response["success"] = false;
        response["status"] = sawOutput ? "timeout" : "no_output_detected";
        response["last_detection_status"] = lastResult.status;
        response["last_confidence"] = static_cast<double>(lastResult.confidence);
        response["description"] = sawOutput
            ? QString("Timeout after %1ms (%2 polls). Terminal still '%3'.")
                .arg(timer.elapsed()).arg(pollCount).arg(lastResult.status)
            : QString("Timeout after %1ms (%2 polls). Command output was never detected — "
                "command may not have been executed or terminal may be frozen.")
                .arg(timer.elapsed()).arg(pollCount);

        qCWarning(log_shared_tool) << "runCommandAndWait: TIMEOUT after" << timer.elapsed()
                                   << "ms," << pollCount << "polls, sawOutput:" << sawOutput
                                   << "last status:" << lastResult.status;
    }

    return response;
}

// ==========================================================================
// screen_to_markdown
// ==========================================================================

QJsonObject SharedToolExecutor::screenToMarkdown(const QJsonObject &args)
{
    if (!m_cameraManager) {
        return QJsonObject{{"error", "CameraManager not initialized"}};
    }
    if (!m_screenAnalyzer) {
        return QJsonObject{{"error", "ScreenAnalyzer not initialized"}};
    }
    if (!m_screenAnalyzer->isAvailable()) {
        return QJsonObject{{"error", "OCR engine not available. Tesseract may not be installed."}};
    }

    // Get detail level parameter
    QString detailLevel = args.value("detail_level").toString("detailed");
    if (detailLevel != "basic" && detailLevel != "detailed") {
        detailLevel = "detailed";
    }

    // Get analysis mode
    const QString modeStr = args.value("mode").toString("general").toLower();
    const AnalysisMode mode = (modeStr == "terminal") ? AnalysisMode::Terminal
                                                      : AnalysisMode::General;

    // Get the current frame
    QImage frame = m_cameraManager->getLatestOriginalFrame();
    if (frame.isNull()) {
        return QJsonObject{{"error", "No frame available from camera"}};
    }

    qCDebug(log_shared_tool) << "screenToMarkdown: analyzing screen with detail level:" << detailLevel
                             << "mode:" << modeStr;

    // Analyze the screen
    ScreenAnalysis analysis = m_screenAnalyzer->analyzeScreen(frame, detailLevel, mode);

    // Build result
    QJsonObject response;
    response["markdown"] = analysis.markdownOutput;
    response["detail_level"] = detailLevel;
    response["mode"] = modeStr;

    return response;
}

// ==========================================================================
// captureFrame
// ==========================================================================

QImage SharedToolExecutor::captureFrame()
{
    if (!m_cameraManager) {
        return QImage(); // null image
    }
    return m_cameraManager->getLatestOriginalFrame();
}

// ==========================================================================
// typeTextCommand
// ==========================================================================

QJsonObject SharedToolExecutor::typeTextCommand(const QJsonObject &args)
{
    QString text = args.value("text").toString();
    if (text.isEmpty()) {
        return QJsonObject{{"error", "text is required"}};
    }

    qCDebug(log_shared_tool) << "typeTextCommand: typing" << text.length() << "chars";

    typeText(text);

    QJsonObject response;
    response["success"] = true;
    response["chars_typed"] = text.length();
    response["text"] = text;
    return response;
}

// ==========================================================================
// mouseMoveAbsolute
// ==========================================================================

QJsonObject SharedToolExecutor::mouseMoveAbsolute(const QJsonObject &args)
{
    int x = args.value("x").toInt();
    int y = args.value("y").toInt();

    // Validate coordinates
    if (x < 0 || x > 4096 || y < 0 || y > 4096) {
        return QJsonObject{{"error", "Coordinates must be in range 0-4096"}};
    }

    MouseManager& mm = HostManager::getInstance().getMouseManager();
    mm.handleAbsoluteMouseAction(x, y, 0, 0);
    QThread::msleep(30);  // Allow CH32V208 to process

    qCDebug(log_shared_tool) << "mouseMoveAbsolute: moved to" << x << "," << y;

    QJsonObject response;
    response["success"] = true;
    response["x"] = x;
    response["y"] = y;
    return response;
}

// ==========================================================================
// mouseClick
// ==========================================================================

QJsonObject SharedToolExecutor::mouseClick(const QJsonObject &args)
{
    int x = args.value("x").toInt();
    int y = args.value("y").toInt();
    QString buttonStr = args.value("button").toString("left");
    int count = args.value("count").toInt(1);

    // Validate coordinates
    if (x < 0 || x > 4096 || y < 0 || y > 4096) {
        return QJsonObject{{"error", "Coordinates must be in range 0-4096"}};
    }

    // Parse button
    int button = 0;
    if (buttonStr == "left") button = 0x01;
    else if (buttonStr == "right") button = 0x02;
    else if (buttonStr == "middle") button = 0x04;
    else return QJsonObject{{"error", QString("Unknown button: %1").arg(buttonStr)}};

    // Validate count
    count = qBound(1, count, 10);

    MouseManager& mm = HostManager::getInstance().getMouseManager();

    for (int i = 0; i < count; ++i) {
        // Press
        mm.handleAbsoluteMouseAction(x, y, button, 0);
        QThread::msleep(50);
        // Release
        mm.handleAbsoluteMouseAction(x, y, 0, 0);
        if (i < count - 1) {
            QThread::msleep(80);  // Delay between clicks
        }
    }

    qCDebug(log_shared_tool) << "mouseClick:" << count << "click(s) at" << x << "," << y
                             << "button:" << buttonStr;

    QJsonObject response;
    response["success"] = true;
    response["x"] = x;
    response["y"] = y;
    response["button"] = buttonStr;
    response["count"] = count;
    return response;
}
