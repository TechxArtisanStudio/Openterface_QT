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

#ifndef CHAT_SETTINGS_PAGE_H
#define CHAT_SETTINGS_PAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QTextEdit>
#include <QGroupBox>
#include <QLabel>

/**
 * Preferences page for AI Chat settings.
 *
 * Configures:
 *   - API Base URL, API Key, Model name
 *   - Target system (macOS/Windows/Linux/iPhone/iPad/Android)
 *   - Chat mode (Chat/Agent/Planner/Guide)
 *   - Agent max iterations
 *   - System prompt, Planner prompt, Guide prompt
 */
class ChatSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatSettingsPage(QWidget *parent = nullptr);

    void setupUI();
    void initChatSettings();
    void applyChatSettings();
    void captureSnapshot();
    void revertToSnapshot();

signals:
    void chatSettingsChanged();

private:
    // API Configuration
    QLineEdit   *m_apiBaseURLEdit;
    QLineEdit   *m_apiKeyEdit;
    QLineEdit   *m_modelEdit;
    QComboBox   *m_targetSystemCombo;
    QSpinBox    *m_agentMaxIterationsSpin;

    // Mode selection
    QRadioButton *m_chatModeRadio;
    QRadioButton *m_agenticModeRadio;
    QRadioButton *m_plannerModeRadio;
    QRadioButton *m_guideModeRadio;

    // Prompts
    QTextEdit   *m_systemPromptEdit;
    QTextEdit   *m_plannerPromptEdit;
    QTextEdit   *m_screenTaskPromptEdit;
    QTextEdit   *m_typingTaskPromptEdit;
    QTextEdit   *m_guidePromptEdit;

    // Snapshot members
    QString m_snap_apiBaseURL;
    QString m_snap_apiKey;
    QString m_snap_model;
    QString m_snap_targetSystem;
    int m_snap_agentMaxIterations;
    int m_snap_modeIndex; // 0=chat, 1=agentic, 2=planner, 3=guide
    QString m_snap_systemPrompt;
    QString m_snap_plannerPrompt;
    QString m_snap_screenTaskPrompt;
    QString m_snap_typingTaskPrompt;
    QString m_snap_guidePrompt;
};

#endif // CHAT_SETTINGS_PAGE_H
