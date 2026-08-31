/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
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

#ifndef CHAT_GUIDE_MODE_H
#define CHAT_GUIDE_MODE_H

#include <QObject>
#include <QString>
#include <QRectF>
#include "ChatTypes.h"

/**
 * @brief Guide mode: step-by-step overlay + auto-next for turn-by-turn guidance.
 *
 * Ported from MacOS ChatGuideModeService.swift.
 * Parses guide responses, executes actions, and optionally auto-advances.
 */
class ChatGuideMode : public QObject
{
    Q_OBJECT

public:
    static ChatGuideMode &instance();

    /// Parse guide response: extract next_step, target_box, tool, tool_input, shortcut
    struct GuideResponse {
        QString nextStep;
        QRectF targetBox;       // normalized (0-1) rect for overlay
        QString tool;           // "left_click", "right_click", etc.
        QString toolInput;
        QString shortcut;
        bool isComplete = false;
    };

    /// Parse a guide-mode AI response into structured data
    GuideResponse parseGuideResponse(const QString &responseText) const;

    /// Execute a guide action (click at target box center, or send shortcut)
    void executeGuideAction(const GuideResponse &guide, const QString &messageContent, bool autoNext);

    /// Execute an input sequence (shortcuts and/or text)
    bool executeGuideInputSequence(const QString &inputSequence);

    /// Check if text indicates guide completion
    bool isGuideCompletionText(const QString &text) const;

    /// Complete a guide step and ask for next
    void completeGuideStepAndNext(const QString &stepDescription);

    /// Get/set auto-next status
    GuideAutoNextStatus autoNextStatus() const { return m_autoNextStatus; }
    void setAutoNextStatus(const GuideAutoNextStatus &status);

signals:
    /// Show overlay rectangle on the video pane
    void guideOverlayRequested(const QRectF &normalizedRect, const QString &tool);

    /// Clear the overlay
    void guideOverlayCleared();

    /// Guide action was executed
    void guideActionExecuted(const QString &description);

    /// Auto-next status changed
    void autoNextStatusChanged(const GuideAutoNextStatus &status);

    /// Request to send a message (for auto-next)
    void guideAutoNextMessageRequested(const QString &message);

private:
    explicit ChatGuideMode(QObject *parent = nullptr);

    bool executeShortcut(const QString &shortcut);
    bool executeBracketedGuideInputSequence(const QString &input);

    enum GuideInputStepType { Shortcut, Text };
    struct GuideInputStep {
        GuideInputStepType type;
        QString value;
    };
    QList<GuideInputStep> parseBracketedGuideInputSteps(const QString &input) const;

    bool isModifierToken(const QString &token) const;
    bool isGuideLauncherShortcut(const QString &shortcut) const;
    bool isGuideNavigationShortcut(const QString &shortcut) const;
    bool looksLikeGuideShortcut(const QString &step) const;
    QString normalizeBracketedKeyToken(const QString &token) const;

    double guideDelayAfterStep(const GuideInputStep &step, const GuideInputStep *nextStep) const;

    static int clampCoord(int value);

    GuideAutoNextStatus m_autoNextStatus;
};

#endif // CHAT_GUIDE_MODE_H
