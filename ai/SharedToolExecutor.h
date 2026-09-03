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

#ifndef SHARED_TOOL_EXECUTOR_H
#define SHARED_TOOL_EXECUTOR_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QImage>

class CameraManager;
class ScreenAnalyzer;

/**
 * @brief Shared implementation for KVM control tools.
 *
 * Singleton that provides the core logic for terminal detection, keyboard input,
 * and mouse control tools. Both the MCP tool handler and the AI chat system
 * delegate to this class, avoiding duplicated implementations.
 *
 * Returns plain QJsonObject results; callers format for their own protocol.
 */
class SharedToolExecutor : public QObject
{
    Q_OBJECT

public:
    static SharedToolExecutor &instance();

    /// Inject the CameraManager (called once at startup from mainwindow)
    void setCameraManager(CameraManager *cam);

    /**
     * @brief Detect whether the terminal is idle/waiting for input.
     *
     * Captures multiple frames and analyzes cursor blink, screen stability,
     * and shell prompt presence.
     *
     * @param args JSON with optional keys: samples (int, 3-8, default 5),
     *             interval_ms (int, 200-1000, default 350)
     * @return Result object with keys: detected, status, confidence, description,
     *         cursor_position, signals, frames_analyzed, total_duration_ms.
     *         On error, contains "error" key.
     */
    QJsonObject detectCursor(const QJsonObject &args);

    /**
     * @brief Type a command and wait until the terminal is idle again.
     *
     * Two-phase polling: Phase 1 waits for "outputting" (command running),
     * Phase 2 waits for "idle" (command finished). Fast commands that
     * complete before outputting is detected are handled as a special case.
     *
     * @param args JSON with keys: command (required), max_wait_ms, poll_interval_ms,
     *             initial_delay_ms, samples, detect_interval_ms
     * @return Result object with keys: success, status, command, wait_time_ms,
     *         poll_count, saw_output, detection, description.
     *         On error, contains "error" key.
     */
    QJsonObject runCommandAndWait(const QJsonObject &args);

    /**
     * @brief Analyze screen with OCR and return markdown text.
     *
     * Captures current frame and runs ScreenAnalyzer to extract text.
     *
     * @param args JSON with optional keys: detail_level ("basic"|"detailed"), mode ("general"|"terminal")
     * @return Result object with keys: markdown, detail_level, mode.
     *         On error, contains "error" key.
     */
    QJsonObject screenToMarkdown(const QJsonObject &args);

    /**
     * @brief Capture current frame from camera.
     *
     * @return QImage of current frame, or null image on error.
     */
    QImage captureFrame();

    /**
     * @brief Type text character-by-character with proper key press/release.
     *
     * Handles special characters (shift chars, uppercase, newlines, tabs).
     * Used by runCommandAndWait() internally and can be used by MCP keyboard_type_text.
     *
     * @param text The text to type
     */
    void typeText(const QString &text);

    /**
     * @brief Type text and return result as QJsonObject.
     *
     * Wrapper for MCP that returns a result object with success message.
     *
     * @param args JSON with key: text (required)
     * @return Result object with keys: success, chars_typed, text.
     *         On error, contains "error" key.
     */
    QJsonObject typeTextCommand(const QJsonObject &args);

    /**
     * @brief Move mouse to absolute position.
     *
     * @param args JSON with keys: x (0-4096), y (0-4096)
     * @return Result object with keys: success, x, y.
     *         On error, contains "error" key.
     */
    QJsonObject mouseMoveAbsolute(const QJsonObject &args);

    /**
     * @brief Click mouse at position.
     *
     * @param args JSON with keys: x, y, button ("left"|"right"|"middle"), count
     * @return Result object with keys: success, x, y, button, count.
     *         On error, contains "error" key.
     */
    QJsonObject mouseClick(const QJsonObject &args);

private:
    explicit SharedToolExecutor(QObject *parent = nullptr);

    CameraManager *m_cameraManager = nullptr;
    ScreenAnalyzer *m_screenAnalyzer = nullptr;
};

#endif // SHARED_TOOL_EXECUTOR_H
