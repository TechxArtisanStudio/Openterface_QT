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

#include "ChatSettingsPage.h"
#include "../globalsetting.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QScrollArea>
#include <QTabWidget>

ChatSettingsPage::ChatSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    initChatSettings();
}

void ChatSettingsPage::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // Use a scroll area for the entire settings page
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *contentWidget = new QWidget();
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setSpacing(12);

    // ---- API Configuration Group ----
    auto *apiGroup = new QGroupBox(tr("API Configuration"));
    auto *apiLayout = new QFormLayout(apiGroup);

    m_apiBaseURLEdit = new QLineEdit();
    m_apiBaseURLEdit->setPlaceholderText("https://api.openai.com/v1");
    m_apiBaseURLEdit->setToolTip(tr("OpenAI-compatible API base URL"));
    apiLayout->addRow(tr("Base URL:"), m_apiBaseURLEdit);

    m_apiKeyEdit = new QLineEdit();
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText("sk-...");
    m_apiKeyEdit->setToolTip(tr("API key for authentication. Can also be set via OPENAI_API_KEY env var."));
    apiLayout->addRow(tr("API Key:"), m_apiKeyEdit);

    m_modelEdit = new QLineEdit();
    m_modelEdit->setPlaceholderText("gpt-4o-mini");
    m_modelEdit->setToolTip(tr("Model name (e.g., gpt-4o-mini, gpt-4o, claude-3.5-sonnet)"));
    apiLayout->addRow(tr("Model:"), m_modelEdit);

    contentLayout->addWidget(apiGroup);

    // ---- Target & Mode Group ----
    auto *modeGroup = new QGroupBox(tr("Target & Mode"));
    auto *modeLayout = new QVBoxLayout(modeGroup);

    // Target system
    auto *targetLayout = new QHBoxLayout();
    m_targetSystemCombo = new QComboBox();
    m_targetSystemCombo->addItems({"Linux", "macOS", "Windows", "iPhone", "iPad", "Android"});
    m_targetSystemCombo->setToolTip(tr("Target operating system for AI context"));
    targetLayout->addWidget(new QLabel(tr("Target System:")));
    targetLayout->addWidget(m_targetSystemCombo);
    targetLayout->addStretch();
    modeLayout->addLayout(targetLayout);

    // Agent max iterations
    auto *iterLayout = new QHBoxLayout();
    m_agentMaxIterationsSpin = new QSpinBox();
    m_agentMaxIterationsSpin->setRange(1, 30);
    m_agentMaxIterationsSpin->setToolTip(tr("Maximum agent loop iterations (1-30)"));
    iterLayout->addWidget(new QLabel(tr("Agent Max Iterations:")));
    iterLayout->addWidget(m_agentMaxIterationsSpin);
    iterLayout->addStretch();
    modeLayout->addLayout(iterLayout);

    // Mode radio buttons
    auto *modeRadioLayout = new QHBoxLayout();
    auto *modeButtonGroup = new QButtonGroup(this);

    m_chatModeRadio = new QRadioButton(tr("Chat"));
    m_chatModeRadio->setToolTip(tr("Standard conversation with text responses"));
    modeButtonGroup->addButton(m_chatModeRadio, 0);
    modeRadioLayout->addWidget(m_chatModeRadio);

    m_agenticModeRadio = new QRadioButton(tr("Agent"));
    m_agenticModeRadio->setToolTip(tr("AI can directly execute actions on the target device"));
    modeButtonGroup->addButton(m_agenticModeRadio, 1);
    modeRadioLayout->addWidget(m_agenticModeRadio);

    m_plannerModeRadio = new QRadioButton(tr("Planner"));
    m_plannerModeRadio->setToolTip(tr("AI creates a multi-step plan for approval before executing"));
    modeButtonGroup->addButton(m_plannerModeRadio, 2);
    modeRadioLayout->addWidget(m_plannerModeRadio);

    m_guideModeRadio = new QRadioButton(tr("Guide"));
    m_guideModeRadio->setToolTip(tr("AI gives turn-by-turn guidance to accomplish your goal"));
    modeButtonGroup->addButton(m_guideModeRadio, 3);
    modeRadioLayout->addWidget(m_guideModeRadio);

    modeRadioLayout->addStretch();
    modeLayout->addLayout(modeRadioLayout);

    contentLayout->addWidget(modeGroup);

    // ---- Prompts Group (with tabs) ----
    auto *promptsGroup = new QGroupBox(tr("Prompts"));
    auto *promptsLayout = new QVBoxLayout(promptsGroup);

    auto *promptTabs = new QTabWidget();

    m_systemPromptEdit = new QTextEdit();
    m_systemPromptEdit->setToolTip(tr("System prompt for standard and agent mode conversations"));
    promptTabs->addTab(m_systemPromptEdit, tr("System"));

    m_plannerPromptEdit = new QTextEdit();
    m_plannerPromptEdit->setToolTip(tr("Prompt for the planner agent that generates execution plans"));
    promptTabs->addTab(m_plannerPromptEdit, tr("Planner"));

    m_screenTaskPromptEdit = new QTextEdit();
    m_screenTaskPromptEdit->setToolTip(tr("Prompt for the screen task agent"));
    promptTabs->addTab(m_screenTaskPromptEdit, tr("Screen Task"));

    m_typingTaskPromptEdit = new QTextEdit();
    m_typingTaskPromptEdit->setToolTip(tr("Prompt for the typing task agent"));
    promptTabs->addTab(m_typingTaskPromptEdit, tr("Typing Task"));

    m_guidePromptEdit = new QTextEdit();
    m_guidePromptEdit->setToolTip(tr("Prompt for guide mode step-by-step instructions"));
    promptTabs->addTab(m_guidePromptEdit, tr("Guide"));

    promptsLayout->addWidget(promptTabs);
    contentLayout->addWidget(promptsGroup);

    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);
}

void ChatSettingsPage::initChatSettings()
{
    auto &settings = GlobalSetting::instance();

    m_apiBaseURLEdit->setText(settings.getChatApiBaseURL());
    m_apiKeyEdit->setText(settings.getChatApiKey());
    m_modelEdit->setText(settings.getChatModel());

    // Target system
    QString ts = settings.getChatTargetSystem().toLower();
    if (ts == "macos" || ts == "mac") m_targetSystemCombo->setCurrentIndex(1);
    else if (ts == "windows" || ts == "win") m_targetSystemCombo->setCurrentIndex(2);
    else if (ts == "iphone") m_targetSystemCombo->setCurrentIndex(3);
    else if (ts == "ipad") m_targetSystemCombo->setCurrentIndex(4);
    else if (ts == "android") m_targetSystemCombo->setCurrentIndex(5);
    else m_targetSystemCombo->setCurrentIndex(0); // Linux

    m_agentMaxIterationsSpin->setValue(settings.getChatAgentMaxIterations());

    // Mode
    if (settings.getChatGuideModeEnabled()) {
        m_guideModeRadio->setChecked(true);
    } else if (settings.getChatPlannerModeEnabled()) {
        m_plannerModeRadio->setChecked(true);
    } else if (settings.getChatAgenticModeEnabled()) {
        m_agenticModeRadio->setChecked(true);
    } else {
        m_chatModeRadio->setChecked(true);
    }

    // Prompts
    m_systemPromptEdit->setPlainText(settings.getChatSystemPrompt());
    m_plannerPromptEdit->setPlainText(settings.getChatPlannerPrompt());
    m_screenTaskPromptEdit->setPlainText(settings.getChatScreenTaskPrompt());
    m_typingTaskPromptEdit->setPlainText(settings.getChatTypingTaskPrompt());
    m_guidePromptEdit->setPlainText(settings.getChatGuidePrompt());
}

void ChatSettingsPage::applyChatSettings()
{
    auto &settings = GlobalSetting::instance();

    settings.setChatApiBaseURL(m_apiBaseURLEdit->text().trimmed());
    settings.setChatApiKey(m_apiKeyEdit->text().trimmed());
    settings.setChatModel(m_modelEdit->text().trimmed());

    // Target system
    int tsIndex = m_targetSystemCombo->currentIndex();
    QStringList systems = {"linux", "macOS", "windows", "iPhone", "iPad", "android"};
    settings.setChatTargetSystem(systems.value(tsIndex, "linux"));

    settings.setChatAgentMaxIterations(m_agentMaxIterationsSpin->value());

    // Mode
    settings.setChatAgenticModeEnabled(m_agenticModeRadio->isChecked());
    settings.setChatPlannerModeEnabled(m_plannerModeRadio->isChecked());
    settings.setChatGuideModeEnabled(m_guideModeRadio->isChecked());

    // Prompts
    settings.setChatSystemPrompt(m_systemPromptEdit->toPlainText());
    settings.setChatPlannerPrompt(m_plannerPromptEdit->toPlainText());
    settings.setChatScreenTaskPrompt(m_screenTaskPromptEdit->toPlainText());
    settings.setChatTypingTaskPrompt(m_typingTaskPromptEdit->toPlainText());
    settings.setChatGuidePrompt(m_guidePromptEdit->toPlainText());

    emit chatSettingsChanged();
}

void ChatSettingsPage::captureSnapshot()
{
    m_snap_apiBaseURL = m_apiBaseURLEdit->text();
    m_snap_apiKey = m_apiKeyEdit->text();
    m_snap_model = m_modelEdit->text();
    m_snap_targetSystem = m_targetSystemCombo->currentText();
    m_snap_agentMaxIterations = m_agentMaxIterationsSpin->value();
    if (m_guideModeRadio->isChecked()) m_snap_modeIndex = 3;
    else if (m_plannerModeRadio->isChecked()) m_snap_modeIndex = 2;
    else if (m_agenticModeRadio->isChecked()) m_snap_modeIndex = 1;
    else m_snap_modeIndex = 0;
    m_snap_systemPrompt = m_systemPromptEdit->toPlainText();
    m_snap_plannerPrompt = m_plannerPromptEdit->toPlainText();
    m_snap_screenTaskPrompt = m_screenTaskPromptEdit->toPlainText();
    m_snap_typingTaskPrompt = m_typingTaskPromptEdit->toPlainText();
    m_snap_guidePrompt = m_guidePromptEdit->toPlainText();
}

void ChatSettingsPage::revertToSnapshot()
{
    m_apiBaseURLEdit->setText(m_snap_apiBaseURL);
    m_apiKeyEdit->setText(m_snap_apiKey);
    m_modelEdit->setText(m_snap_model);

    // Find and set target system combo
    int idx = m_targetSystemCombo->findText(m_snap_targetSystem, Qt::MatchFixedString);
    if (idx >= 0) m_targetSystemCombo->setCurrentIndex(idx);

    m_agentMaxIterationsSpin->setValue(m_snap_agentMaxIterations);

    switch (m_snap_modeIndex) {
        case 3: m_guideModeRadio->setChecked(true); break;
        case 2: m_plannerModeRadio->setChecked(true); break;
        case 1: m_agenticModeRadio->setChecked(true); break;
        default: m_chatModeRadio->setChecked(true); break;
    }

    m_systemPromptEdit->setPlainText(m_snap_systemPrompt);
    m_plannerPromptEdit->setPlainText(m_snap_plannerPrompt);
    m_screenTaskPromptEdit->setPlainText(m_snap_screenTaskPrompt);
    m_typingTaskPromptEdit->setPlainText(m_snap_typingTaskPrompt);
    m_guidePromptEdit->setPlainText(m_snap_guidePrompt);
}
