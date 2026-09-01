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
    : PreferencePageBase(parent)
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
    m_targetSystemCombo->addItems({"Linux", "macOS", "Windows", "iPhone", "iPad", "Android", "BIOS/UEFI", "Text-based UI"});
    m_targetSystemCombo->setToolTip(tr("Target operating system for AI context. Select BIOS/UEFI or Text-based UI for text-menu environments where OCR cannot detect selection state."));
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

    // Typing delay and batch size
    auto *typingLayout = new QHBoxLayout();
    m_typingDelaySpin = new QSpinBox();
    m_typingDelaySpin->setRange(0, 1000);
    m_typingDelaySpin->setSuffix(tr(" ms"));
    m_typingDelaySpin->setToolTip(tr("Delay between keystrokes when typing (0-1000ms)"));
    typingLayout->addWidget(new QLabel(tr("Typing Delay:")));
    typingLayout->addWidget(m_typingDelaySpin);
    typingLayout->addSpacing(20);

    m_batchSizeSpin = new QSpinBox();
    m_batchSizeSpin->setRange(1, 50);
    m_batchSizeSpin->setToolTip(tr("Number of characters typed before a pause (1-50)"));
    typingLayout->addWidget(new QLabel(tr("Batch Size:")));
    typingLayout->addWidget(m_batchSizeSpin);
    typingLayout->addStretch();
    modeLayout->addLayout(typingLayout);

    // USB HID timing delays
    auto *timingLayout = new QHBoxLayout();

    m_mouseToKeyboardDelaySpin = new QSpinBox();
    m_mouseToKeyboardDelaySpin->setRange(0, 5000);
    m_mouseToKeyboardDelaySpin->setSuffix(tr(" ms"));
    m_mouseToKeyboardDelaySpin->setToolTip(tr("Delay after mouse action before keyboard action (click → type). Gives target OS time to process click and shift focus."));
    timingLayout->addWidget(new QLabel(tr("Mouse→Keyboard:")));
    timingLayout->addWidget(m_mouseToKeyboardDelaySpin);
    timingLayout->addSpacing(10);

    m_postKeyboardSettleSpin = new QSpinBox();
    m_postKeyboardSettleSpin->setRange(0, 5000);
    m_postKeyboardSettleSpin->setSuffix(tr(" ms"));
    m_postKeyboardSettleSpin->setToolTip(tr("Delay after keyboard action before next tool (type → capture). Lets target OS render the result."));
    timingLayout->addWidget(new QLabel(tr("Post-Keyboard:")));
    timingLayout->addWidget(m_postKeyboardSettleSpin);
    timingLayout->addSpacing(10);

    m_preCaptureDelaySpin = new QSpinBox();
    m_preCaptureDelaySpin->setRange(0, 5000);
    m_preCaptureDelaySpin->setSuffix(tr(" ms"));
    m_preCaptureDelaySpin->setToolTip(tr("Delay before screen capture to let the screen update."));
    timingLayout->addWidget(new QLabel(tr("Pre-Capture:")));
    timingLayout->addWidget(m_preCaptureDelaySpin);
    timingLayout->addStretch();
    modeLayout->addLayout(timingLayout);

    // Initial typing delay
    auto *initialDelayLayout = new QHBoxLayout();
    m_initialTypingDelaySpin = new QSpinBox();
    m_initialTypingDelaySpin->setRange(0, 5000);
    m_initialTypingDelaySpin->setSuffix(tr(" ms"));
    m_initialTypingDelaySpin->setToolTip(tr("Delay before first character is typed. Gives target OS time to open windows (e.g., after ctrl+alt+t) and be ready for keystrokes."));
    initialDelayLayout->addWidget(new QLabel(tr("Initial Typing Delay:")));
    initialDelayLayout->addWidget(m_initialTypingDelaySpin);
    initialDelayLayout->addStretch();
    modeLayout->addLayout(initialDelayLayout);

    // Mode radio buttons
    auto *modeRadioLayout = new QHBoxLayout();
    auto *modeButtonGroup = new QButtonGroup(this);

    m_agenticModeRadio = new QRadioButton(tr("Agent"));
    m_agenticModeRadio->setToolTip(tr("AI can directly execute actions on the target device"));
    modeButtonGroup->addButton(m_agenticModeRadio, 0);
    modeRadioLayout->addWidget(m_agenticModeRadio);

    m_plannerModeRadio = new QRadioButton(tr("Planner"));
    m_plannerModeRadio->setToolTip(tr("AI creates a multi-step plan for approval before executing"));
    modeButtonGroup->addButton(m_plannerModeRadio, 1);
    modeRadioLayout->addWidget(m_plannerModeRadio);

    m_guideModeRadio = new QRadioButton(tr("Guide"));
    m_guideModeRadio->setToolTip(tr("AI gives turn-by-turn guidance to accomplish your goal"));
    modeButtonGroup->addButton(m_guideModeRadio, 2);
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

    // ---- Apply / Revert / Cancel button bar (from PreferencePageBase) ----
    createButtonBar(mainLayout);

    // ---- Wire widget change signals to dirty-state checking ----
    connect(m_apiBaseURLEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_modelEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_targetSystemCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_agentMaxIterationsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_typingDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_batchSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_mouseToKeyboardDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_postKeyboardSettleSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_preCaptureDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_initialTypingDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_agenticModeRadio, &QRadioButton::toggled, this, [this]{ checkDirtyState(); });
    connect(m_plannerModeRadio, &QRadioButton::toggled, this, [this]{ checkDirtyState(); });
    connect(m_guideModeRadio, &QRadioButton::toggled, this, [this]{ checkDirtyState(); });
    connect(m_systemPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_plannerPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_screenTaskPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_typingTaskPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_guidePromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
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
    else if (ts == "bios" || ts == "uefi") m_targetSystemCombo->setCurrentIndex(6);
    else if (ts == "textui" || ts == "dos") m_targetSystemCombo->setCurrentIndex(7);
    else m_targetSystemCombo->setCurrentIndex(0); // Linux

    m_agentMaxIterationsSpin->setValue(settings.getChatAgentMaxIterations());

    // Typing/paste settings
    m_typingDelaySpin->setValue(settings.getChatTypingDelayMs());
    m_batchSizeSpin->setValue(settings.getChatBatchSize());
    m_mouseToKeyboardDelaySpin->setValue(settings.getChatMouseToKeyboardDelayMs());
    m_postKeyboardSettleSpin->setValue(settings.getChatPostKeyboardSettleMs());
    m_preCaptureDelaySpin->setValue(settings.getChatPreCaptureDelayMs());
    m_initialTypingDelaySpin->setValue(settings.getChatInitialTypingDelayMs());

    // Mode
    if (settings.getChatGuideModeEnabled()) {
        m_guideModeRadio->setChecked(true);
    } else if (settings.getChatPlannerModeEnabled()) {
        m_plannerModeRadio->setChecked(true);
    } else {
        m_agenticModeRadio->setChecked(true);  // Default to Agent mode
    }

    // Prompts
    m_systemPromptEdit->setPlainText(settings.getChatSystemPrompt());
    m_plannerPromptEdit->setPlainText(settings.getChatPlannerPrompt());
    m_screenTaskPromptEdit->setPlainText(settings.getChatScreenTaskPrompt());
    m_typingTaskPromptEdit->setPlainText(settings.getChatTypingTaskPrompt());
    m_guidePromptEdit->setPlainText(settings.getChatGuidePrompt());

    captureSnapshot();
    clearDirty();
}

void ChatSettingsPage::applySettings()
{
    auto &settings = GlobalSetting::instance();

    settings.setChatApiBaseURL(m_apiBaseURLEdit->text().trimmed());
    settings.setChatApiKey(m_apiKeyEdit->text().trimmed());
    settings.setChatModel(m_modelEdit->text().trimmed());

    // Target system
    int tsIndex = m_targetSystemCombo->currentIndex();
    QStringList systems = {"linux", "macOS", "windows", "iPhone", "iPad", "android", "bios", "textui"};
    settings.setChatTargetSystem(systems.value(tsIndex, "linux"));

    settings.setChatAgentMaxIterations(m_agentMaxIterationsSpin->value());

    // Typing/paste settings
    settings.setChatTypingDelayMs(m_typingDelaySpin->value());
    settings.setChatBatchSize(m_batchSizeSpin->value());
    settings.setChatMouseToKeyboardDelayMs(m_mouseToKeyboardDelaySpin->value());
    settings.setChatPostKeyboardSettleMs(m_postKeyboardSettleSpin->value());
    settings.setChatPreCaptureDelayMs(m_preCaptureDelaySpin->value());
    settings.setChatInitialTypingDelayMs(m_initialTypingDelaySpin->value());

    // Mode
    settings.setChatAgenticModeEnabled(true);  // All modes use agentic features
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
    m_snap_typingDelay = m_typingDelaySpin->value();
    m_snap_batchSize = m_batchSizeSpin->value();
    m_snap_mouseToKeyboardDelay = m_mouseToKeyboardDelaySpin->value();
    m_snap_postKeyboardSettle = m_postKeyboardSettleSpin->value();
    m_snap_preCaptureDelay = m_preCaptureDelaySpin->value();
    m_snap_initialTypingDelay = m_initialTypingDelaySpin->value();
    if (m_guideModeRadio->isChecked()) m_snap_modeIndex = 2;
    else if (m_plannerModeRadio->isChecked()) m_snap_modeIndex = 1;
    else m_snap_modeIndex = 0;  // Default to Agent mode
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
    m_typingDelaySpin->setValue(m_snap_typingDelay);
    m_batchSizeSpin->setValue(m_snap_batchSize);
    m_mouseToKeyboardDelaySpin->setValue(m_snap_mouseToKeyboardDelay);
    m_postKeyboardSettleSpin->setValue(m_snap_postKeyboardSettle);
    m_preCaptureDelaySpin->setValue(m_snap_preCaptureDelay);
    m_initialTypingDelaySpin->setValue(m_snap_initialTypingDelay);

    switch (m_snap_modeIndex) {
        case 2: m_guideModeRadio->setChecked(true); break;
        case 1: m_plannerModeRadio->setChecked(true); break;
        case 0: m_agenticModeRadio->setChecked(true); break;
        default: m_agenticModeRadio->setChecked(true); break;
    }

    m_systemPromptEdit->setPlainText(m_snap_systemPrompt);
    m_plannerPromptEdit->setPlainText(m_snap_plannerPrompt);
    m_screenTaskPromptEdit->setPlainText(m_snap_screenTaskPrompt);
    m_typingTaskPromptEdit->setPlainText(m_snap_typingTaskPrompt);
    m_guidePromptEdit->setPlainText(m_snap_guidePrompt);
}

bool ChatSettingsPage::valuesMatchSnapshot() const
{
    if (m_apiBaseURLEdit->text() != m_snap_apiBaseURL) return false;
    if (m_apiKeyEdit->text() != m_snap_apiKey) return false;
    if (m_modelEdit->text() != m_snap_model) return false;
    if (m_targetSystemCombo->currentText() != m_snap_targetSystem) return false;
    if (m_agentMaxIterationsSpin->value() != m_snap_agentMaxIterations) return false;
    if (m_typingDelaySpin->value() != m_snap_typingDelay) return false;
    if (m_batchSizeSpin->value() != m_snap_batchSize) return false;
    if (m_mouseToKeyboardDelaySpin->value() != m_snap_mouseToKeyboardDelay) return false;
    if (m_postKeyboardSettleSpin->value() != m_snap_postKeyboardSettle) return false;
    if (m_preCaptureDelaySpin->value() != m_snap_preCaptureDelay) return false;
    if (m_initialTypingDelaySpin->value() != m_snap_initialTypingDelay) return false;

    int currentMode = 0;
    if (m_guideModeRadio->isChecked()) currentMode = 2;
    else if (m_plannerModeRadio->isChecked()) currentMode = 1;
    if (currentMode != m_snap_modeIndex) return false;

    if (m_systemPromptEdit->toPlainText() != m_snap_systemPrompt) return false;
    if (m_plannerPromptEdit->toPlainText() != m_snap_plannerPrompt) return false;
    if (m_screenTaskPromptEdit->toPlainText() != m_snap_screenTaskPrompt) return false;
    if (m_typingTaskPromptEdit->toPlainText() != m_snap_typingTaskPrompt) return false;
    if (m_guidePromptEdit->toPlainText() != m_snap_guidePrompt) return false;

    return true;
}
