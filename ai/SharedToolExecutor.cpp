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
#include "ai/bios_focus_detector/bios_focus_detector.h"
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
    , m_cameraManager(nullptr)
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

    // Two-phase polling with enhanced detection
    QElapsedTimer timer;
    timer.start();
    int pollCount = 0;
    CursorDetectionResult lastResult;
    lastResult.status = "unknown";
    bool sawOutput = false;
    bool success = false;
    QString detectedPrompt;
    int stablePromptCount = 0;  // Count consecutive polls with stable prompt

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

        // Run cursor blink detection
        lastResult = m_screenAnalyzer->detectCursorFromFrames(frames);

        // Also check for shell prompt on the last frame (most recent state)
        QString currentPrompt;
        bool hasPrompt = m_screenAnalyzer->detectShellPrompt(frames.last(), currentPrompt);

        pollCount++;

        qCDebug(log_shared_tool) << "runCommandAndWait: poll" << pollCount
                                 << "status:" << lastResult.status
                                 << "hasPrompt:" << hasPrompt
                                 << "prompt:" << currentPrompt
                                 << "sawOutput:" << sawOutput
                                 << "stablePrompts:" << stablePromptCount
                                 << "cursorBlink:" << lastResult.cursorBlinkDetected
                                 << "screenStable:" << lastResult.screenStable
                                 << "changeRatio:" << lastResult.totalChangeRatio
                                 << "elapsed:" << timer.elapsed() << "ms";

        // Enhanced idle detection: consider terminal idle if ANY of these are true:
        // 1. Cursor blink detected with high confidence (strong signal)
        // 2. Status is "idle" or "likely_idle" (from cursor detection)
        // 3. Shell prompt detected + screen is stable + low change ratio
        bool isIdle = false;
        if (lastResult.cursorBlinkDetected && lastResult.confidence >= 0.7f) {
            isIdle = true;
            qCDebug(log_shared_tool) << "runCommandAndWait: idle detected via cursor blink";
        } else if (lastResult.status == "idle" || lastResult.status == "likely_idle") {
            isIdle = true;
            qCDebug(log_shared_tool) << "runCommandAndWait: idle detected via status";
        } else if (hasPrompt && lastResult.screenStable && lastResult.totalChangeRatio < 0.01f) {
            // Shell prompt visible + screen stable + minimal changes = terminal is idle
            isIdle = true;
            qCDebug(log_shared_tool) << "runCommandAndWait: idle detected via prompt + stable screen";
        }

        // Enhanced output detection: consider output happening if:
        // 1. Status is "outputting", OR
        // 2. Screen is changing significantly (totalChangeRatio > 0.05)
        bool isOutputting = false;
        if (lastResult.status == "outputting") {
            isOutputting = true;
        } else if (lastResult.totalChangeRatio > 0.05f) {
            isOutputting = true;
            qCDebug(log_shared_tool) << "runCommandAndWait: output detected via high change ratio:"
                                     << lastResult.totalChangeRatio;
        }

        // Track prompt stability
        if (hasPrompt && currentPrompt == detectedPrompt && !currentPrompt.isEmpty()) {
            stablePromptCount++;
        } else {
            detectedPrompt = currentPrompt;
            stablePromptCount = hasPrompt ? 1 : 0;
        }

        if (!sawOutput) {
            // Phase 1: Waiting for command to start producing output
            if (isOutputting) {
                sawOutput = true;
                qCDebug(log_shared_tool) << "runCommandAndWait: output detected, waiting for idle";
            } else if (isIdle && timer.elapsed() > initialDelayMs + 500) {
                // Fast command — finished before we caught outputting
                // But require prompt stability to avoid false positives
                if (stablePromptCount >= 2 || lastResult.confidence >= 0.6f) {
                    sawOutput = true;
                    success = true;
                    break;
                }
            }
        } else {
            // Phase 2: We saw output, now waiting for idle
            if (isIdle) {
                // Require prompt to be stable for at least 2 consecutive polls
                // to avoid false positives from transient states
                if (stablePromptCount >= 2 || (hasPrompt && lastResult.confidence >= 0.6f)) {
                    success = true;
                    break;
                }
            } else {
                // Reset stable count if we see output again
                stablePromptCount = 0;
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
        // Add enhanced detection info
        QString currentPrompt;
        bool hasPrompt = m_screenAnalyzer->detectShellPrompt(
            m_cameraManager->getLatestOriginalFrame(), currentPrompt);
        signalDetails["enhanced_prompt_detected"] = hasPrompt;
        if (hasPrompt && !currentPrompt.isEmpty()) {
            signalDetails["enhanced_prompt_text"] = currentPrompt;
        }
        signalDetails["prompt_stable_count"] = stablePromptCount;
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
// screen_diff
// ==========================================================================

QJsonObject SharedToolExecutor::screenDiff(const QJsonObject &args)
{
    Q_UNUSED(args)

    if (!m_cameraManager) {
        return QJsonObject{{"error", "CameraManager not initialized"}};
    }
    if (!m_screenAnalyzer) {
        return QJsonObject{{"error", "ScreenAnalyzer not initialized"}};
    }
    if (!m_screenAnalyzer->isAvailable()) {
        return QJsonObject{{"error", "OCR engine not available. Tesseract may not be installed."}};
    }

    QImage frame = m_cameraManager->getLatestOriginalFrame();
    if (frame.isNull()) {
        return QJsonObject{{"error", "No frame available from camera"}};
    }

    qCDebug(log_shared_tool) << "screenDiff: analyzing screen differential";

    ScreenDiffResult diff = m_screenAnalyzer->analyzeScreenDiff(frame);

    QJsonObject response;
    switch (diff.outcome) {
    case ScreenDiffResult::FirstCapture: response["outcome"] = "first_capture"; break;
    case ScreenDiffResult::NoChange:     response["outcome"] = "no_change";     break;
    case ScreenDiffResult::FullChange:   response["outcome"] = "full_change";   break;
    case ScreenDiffResult::PartialChange:response["outcome"] = "partial_change";break;
    }
    response["change_ratio"] = static_cast<double>(diff.changeRatio);
    if (!diff.changedRect.isNull()) {
        QJsonObject r;
        r["x"] = diff.changedRect.x();
        r["y"] = diff.changedRect.y();
        r["width"]  = diff.changedRect.width();
        r["height"] = diff.changedRect.height();
        response["changed_rect"] = r;
    }
    if (!diff.highlightedText.trimmed().isEmpty()) {
        response["highlighted_text"] = diff.highlightedText.trimmed();
        QJsonObject hr;
        hr["x"] = diff.highlightRect.x();
        hr["y"] = diff.highlightRect.y();
        response["highlight_rect"] = hr;
    }
    response["report"] = diff.report;
    return response;
}

// ==========================================================================
// navigate_to_menu_item
// ==========================================================================

namespace {

// Run BIOS reverse-video highlight detection and return the currently
// highlighted item's text (trimmed), or empty string if none is highlighted.
QString detectCurrentHighlight(const QImage &frame)
{
    if (frame.isNull()) return QString();
    QImage rgbImage = frame.convertToFormat(QImage::Format_RGB888);
    int width = rgbImage.width();
    int height = rgbImage.height();
    if (width < 64 || height < 32) {
        qCDebug(log_shared_tool) << "detectCurrentHighlight: frame too small:" << width << "x" << height;
        return QString();
    }

    int bufSize = width * height * 3;
    unsigned char *buf = static_cast<unsigned char *>(malloc(bufSize));
    if (!buf) {
        qCWarning(log_shared_tool) << "detectCurrentHighlight: malloc failed for buffer size" << bufSize;
        return QString();
    }

    for (int y = 0; y < height; ++y) {
        const unsigned char *src = rgbImage.constBits() + y * rgbImage.bytesPerLine();
        memcpy(buf + y * width * 3, src, width * 3);
    }

    BiosFocusResult biosResult;
    int ret = bios_detect_focus_from_pixels(buf, width, height, &biosResult);
    free(buf);

    if (ret != 0) {
        qCWarning(log_shared_tool) << "detectCurrentHighlight: bios_detect_focus_from_pixels returned" << ret;
        return QString();
    }
    if (biosResult.num_highlights == 0) {
        qCDebug(log_shared_tool) << "detectCurrentHighlight: no highlights found";
        return QString();
    }

    QString text = QString::fromUtf8(biosResult.highlights[0].text).trimmed();
    qCDebug(log_shared_tool) << "detectCurrentHighlight: found" << biosResult.num_highlights << "highlight(s), first:" << text;

    // Check if OCR failed and we got the fallback diagnostic message
    if (text.startsWith("Highlight row y=")) {
        qCWarning(log_shared_tool) << "detectCurrentHighlight: OCR failed, got fallback message:" << text;
    }

    return text;
}

// Case-insensitive fuzzy match. "ACPI Settings" should match "ACPI  settings"
// or a truncated OCR read like "ACPI Settin". Normalizes by collapsing spaces
// and lower-casing, then checks equality / containment in both directions.
bool highlightMatchesTarget(const QString &highlight, const QString &target)
{
    QString h = highlight.simplified().toLower();
    QString t = target.simplified().toLower();
    if (h.isEmpty() || t.isEmpty()) return false;
    if (h == t) return true;
    if (h.contains(t) || t.contains(h)) return true;
    return false;
}

} // namespace

QJsonObject SharedToolExecutor::navigateToMenuItem(const QJsonObject &args)
{
    QString target = args.value("target").toString().trimmed();
    if (target.isEmpty()) {
        return QJsonObject{{"error", "target (string) is required — the menu item text to navigate to"}};
    }

    QString direction = args.value("direction").toString("down").toLower();
    int maxSteps = args.value("max_steps").toInt(30);
    maxSteps = qBound(1, maxSteps, 200);

    int keyCode = Qt::Key_Down; // default
    const char *dirName = "down"; // default

    if (direction == "up") {
        keyCode = Qt::Key_Up;
        dirName = "up";
    } else if (direction == "down") {
        keyCode = Qt::Key_Down;
        dirName = "down";
    } else if (direction == "left") {
        keyCode = Qt::Key_Left;
        dirName = "left";
    } else if (direction == "right") {
        keyCode = Qt::Key_Right;
        dirName = "right";
    } else {
        // fallback to down if invalid direction
        qCWarning(log_shared_tool) << "navigateToMenuItem: invalid direction '" << direction << "', defaulting to down";
    }

    if (!m_cameraManager) {
        return QJsonObject{{"error", "CameraManager not initialized"}};
    }

    qCDebug(log_shared_tool) << "navigateToMenuItem: target='" << target
                             << "' direction=" << dirName << " maxSteps=" << maxSteps;

    HostManager &hostManager = HostManager::getInstance();
    int stepsTaken = 0;

    for (int step = 0; step < maxSteps; ++step) {
        QImage frame = m_cameraManager->getLatestOriginalFrame();
        if (frame.isNull()) {
            return QJsonObject{{"error", "No frame available from camera"}};
        }

        QString highlight = detectCurrentHighlight(frame);
        qCDebug(log_shared_tool) << "navigateToMenuItem step" << step << "- detected highlight:" << highlight;

        // Check before pressing (in case the target is already highlighted).
        if (!highlight.isEmpty() && highlightMatchesTarget(highlight, target)) {
            QJsonObject response;
            response["success"] = true;
            response["target_found"] = true;
            response["steps_taken"] = step;
            response["direction"] = dirName;
            response["final_highlight"] = highlight;
            response["message"] = QString("Target '%1' is already highlighted (no key press needed).")
                .arg(target);
            qCInfo(log_shared_tool) << "navigateToMenuItem: target already highlighted:" << highlight;
            return response;
        }

        // Press the arrow key once.
        hostManager.handleKeyboardAction(keyCode, 0, true);
        QThread::msleep(50);
        hostManager.handleKeyboardAction(keyCode, 0, false);
        stepsTaken = step + 1;

        // Give the target screen time to redraw before reading it again.
        QThread::msleep(180);

        // Re-read and check.
        QImage frame2 = m_cameraManager->getLatestOriginalFrame();
        if (frame2.isNull()) continue;
        QString highlight2 = detectCurrentHighlight(frame2);
        qCDebug(log_shared_tool) << "navigateToMenuItem step" << stepsTaken << "- after press highlight:" << highlight2;

        if (!highlight2.isEmpty() && highlightMatchesTarget(highlight2, target)) {
            QJsonObject response;
            response["success"] = true;
            response["target_found"] = true;
            response["steps_taken"] = stepsTaken;
            response["direction"] = dirName;
            response["final_highlight"] = highlight2;
            response["message"] = QString("Reached target '%1' after %2 step(s).")
                .arg(target).arg(stepsTaken);
            qCInfo(log_shared_tool) << "navigateToMenuItem: reached target" << target
                                    << "after" << stepsTaken << "steps";
            return response;
        }
    }

    // Failed to reach target within max steps.
    QImage finalFrame = m_cameraManager->getLatestOriginalFrame();
    QString finalHighlight = finalFrame.isNull() ? QString() : detectCurrentHighlight(finalFrame);

    QJsonObject response;
    response["success"] = false;
    response["target_found"] = false;
    response["steps_taken"] = stepsTaken;
    response["direction"] = dirName;
    response["final_highlight"] = finalHighlight;
    response["message"] = QString("Did not find '%1' after %2 step(s) pressing '%3'. "
                                  "Final highlighted item: '%4'. The target may not exist "
                                  "in this menu, or is behind a submenu — consider pressing "
                                  "'enter' to go deeper or 'esc' to go back.")
        .arg(target).arg(stepsTaken).arg(dirName).arg(finalHighlight);
    qCInfo(log_shared_tool) << "navigateToMenuItem: target" << target << "NOT found after"
                            << stepsTaken << "steps. final highlight:" << finalHighlight;
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
