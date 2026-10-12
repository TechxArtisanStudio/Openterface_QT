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

#ifndef CHAT_SCREEN_CAPTURE_H
#define CHAT_SCREEN_CAPTURE_H

#include <QObject>
#include <QString>
#include <QImage>
#include <functional>

class CameraManager;

/**
 * @brief Captures the target screen for AI analysis.
 *
 * Uses the CameraManager to grab the current video frame,
 * saves it as JPEG, and converts to base64 data URL for API calls.
 */
class ChatScreenCapture : public QObject
{
    Q_OBJECT

public:
    static ChatScreenCapture &instance();

    /// Set the CameraManager to use for screen captures
    void setCameraManager(CameraManager *cam);

    /// Get the CameraManager (for tools that need frame access)
    CameraManager *cameraManager() const { return m_cameraManager; }

    /// Capture the current target screen and return the file path.
    /// Returns empty string on failure.
    QString captureScreen();

    /// Convert an image file to a base64 data URL.
    QString dataURLForImage(const QString &filePath) const;

    /// Capture an annotated click image showing where the AI clicked.
    QString captureAnnotatedClick(int absX, int absY, const QString &actionName);

signals:
    /// Emitted when a screenshot is captured (for preview in chat)
    void screenshotCaptured(const QString &filePath);

private:
    explicit ChatScreenCapture(QObject *parent = nullptr);
    QString tempFilePath() const;
    /// Thread-safe internal capture (must be called on the main thread)
    QString doCaptureScreen();

    CameraManager *m_cameraManager = nullptr;
};

#endif // CHAT_SCREEN_CAPTURE_H
