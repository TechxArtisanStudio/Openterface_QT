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
#include <QHeaderView>

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

    // ---- Tools Configuration Group (Tree View) ----
    auto *toolsGroup = new QGroupBox(tr("Tools Configuration"));
    auto *toolsLayout = new QVBoxLayout(toolsGroup);

    auto *toolsInfoLabel = new QLabel(tr("Enable or disable specific AI tools. Disabled tools will not be available to the AI agent."));
    toolsInfoLabel->setWordWrap(true);
    toolsInfoLabel->setStyleSheet("color: gray; font-size: 10px;");
    toolsLayout->addWidget(toolsInfoLabel);

    // Master Select All checkbox
    m_selectAllToolsCheck = new QCheckBox(tr("Select All"));
    m_selectAllToolsCheck->setChecked(true);
    m_selectAllToolsCheck->setStyleSheet("font-weight: bold;");
    toolsLayout->addWidget(m_selectAllToolsCheck);

    // Tools tree view
    m_toolsTreeView = new QTreeView();
    m_toolsTreeView->setRootIsDecorated(true);
    m_toolsTreeView->setAlternatingRowColors(false);
    m_toolsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_toolsTreeView->setIndentation(20);
    m_toolsTreeView->setMaximumHeight(280);

    m_toolsModel = new QStandardItemModel(this);
    m_toolsModel->setColumnCount(2);
    m_toolsModel->setHorizontalHeaderLabels({tr("Tool"), tr("Description")});

    m_toolsTreeView->setModel(m_toolsModel);
    m_toolsTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_toolsTreeView->header()->setStretchLastSection(true);

    // Populate the tree
    populateToolsTree();

    toolsLayout->addWidget(m_toolsTreeView);

    contentLayout->addWidget(toolsGroup);

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
    // Tool checkboxes - tree view
    connect(m_selectAllToolsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        // Propagate to all items in the tree
        Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
        for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
            QStandardItem *group = m_toolsModel->item(g);
            if (group && group->isCheckable()) {
                group->setCheckState(state);
            }
            for (int c = 0; c < group->rowCount(); ++c) {
                QStandardItem *child = group->child(c, 0);
                if (child && child->isCheckable()) {
                    child->setCheckState(state);
                }
            }
        }
        checkDirtyState();
    });

    // Model changed signal - handle group checkbox propagation
    static bool propagating = false;
    connect(m_toolsModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item){
        if (propagating) return;

        // If changed item is a group (has children), propagate check state to all children
        if (item && item->rowCount() > 0 && item->isCheckable()) {
            Qt::CheckState groupState = item->checkState();

            // Check if all children already match this state
            bool allMatch = true;
            bool anyChecked = false;
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem* child = item->child(c, 0);
                if (child && child->isCheckable()) {
                    if (child->checkState() != groupState) allMatch = false;
                    if (child->checkState() == Qt::Checked) anyChecked = true;
                }
            }

            // Determine desired state
            Qt::CheckState desiredState;
            if (allMatch) {
                desiredState = (groupState == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
            } else if (groupState == Qt::PartiallyChecked) {
                desiredState = anyChecked ? Qt::Checked : Qt::Unchecked;
            } else {
                desiredState = groupState;
            }

            // Guard against recursive signals while updating children
            propagating = true;
            item->setCheckState(desiredState);
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem* child = item->child(c, 0);
                if (child && child->isCheckable()) {
                    child->setCheckState(desiredState);
                }
            }
            propagating = false;
        }

        // Sync selectAll checkbox
        bool allChecked = true;
        for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
            QStandardItem* grp = m_toolsModel->item(g);
            for (int c = 0; c < grp->rowCount(); ++c) {
                QStandardItem* child = grp->child(c, 0);
                if (child && child->isCheckable() && child->checkState() != Qt::Checked) {
                    allChecked = false;
                    break;
                }
            }
            if (!allChecked) break;
        }
        m_selectAllToolsCheck->blockSignals(true);
        m_selectAllToolsCheck->setChecked(allChecked);
        m_selectAllToolsCheck->blockSignals(false);

        checkDirtyState();
    });
    connect(m_systemPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_plannerPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_screenTaskPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_typingTaskPromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_guidePromptEdit, &QTextEdit::textChanged, this, [this]{ checkDirtyState(); });
}

void ChatSettingsPage::populateToolsTree()
{
    m_toolsModel->removeRows(0, m_toolsModel->rowCount());

    // Screen tools group
    QStandardItem *screenGroup = new QStandardItem(tr("Screen Tools"));
    screenGroup->setCheckable(true);
    screenGroup->setCheckState(Qt::Checked);
    screenGroup->setEditable(false);
    QFont f = screenGroup->font();
    f.setBold(true);
    screenGroup->setFont(f);

    QStandardItem *screenCapture = new QStandardItem(tr("Screen Capture"));
    screenCapture->setCheckable(true);
    screenCapture->setCheckState(Qt::Checked);
    screenCapture->setEditable(false);
    screenCapture->setData("capture_screen", Qt::UserRole + 1);
    screenCapture->setToolTip(tr("Take screenshots of the target screen"));

    QStandardItem *screenCaptureDesc = new QStandardItem(tr("capture_screen"));
    screenCaptureDesc->setEditable(false);
    screenCaptureDesc->setForeground(QColor(128, 128, 128));

    QStandardItem *screenToMarkdown = new QStandardItem(tr("Screen to Markdown (OCR)"));
    screenToMarkdown->setCheckable(true);
    screenToMarkdown->setCheckState(Qt::Checked);
    screenToMarkdown->setEditable(false);
    screenToMarkdown->setData("screen_to_markdown", Qt::UserRole + 1);
    screenToMarkdown->setToolTip(tr("Extract text from screen using OCR"));

    QStandardItem *screenToMarkdownDesc = new QStandardItem(tr("screen_to_markdown"));
    screenToMarkdownDesc->setEditable(false);
    screenToMarkdownDesc->setForeground(QColor(128, 128, 128));

    screenGroup->appendRow({screenCapture, screenCaptureDesc});
    screenGroup->appendRow({screenToMarkdown, screenToMarkdownDesc});
    m_toolsModel->appendRow(screenGroup);

    // Mouse tools group
    QStandardItem *mouseGroup = new QStandardItem(tr("Mouse Tools"));
    mouseGroup->setCheckable(true);
    mouseGroup->setCheckState(Qt::Checked);
    mouseGroup->setEditable(false);
    mouseGroup->setFont(f);

    auto addMouseTool = [&](const QString &name, const QString &toolId, const QString &desc) {
        QStandardItem *item = new QStandardItem(name);
        item->setCheckable(true);
        item->setCheckState(Qt::Checked);
        item->setEditable(false);
        item->setData(toolId, Qt::UserRole + 1);
        item->setToolTip(desc);

        QStandardItem *descItem = new QStandardItem(toolId);
        descItem->setEditable(false);
        descItem->setForeground(QColor(128, 128, 128));

        mouseGroup->appendRow({item, descItem});
    };

    addMouseTool(tr("Move Mouse"), "move_mouse", tr("Move mouse cursor on target"));
    addMouseTool(tr("Left Click"), "left_click", tr("Perform left click on target"));
    addMouseTool(tr("Right Click"), "right_click", tr("Perform right click on target"));
    addMouseTool(tr("Double Click"), "double_click", tr("Perform double click on target"));
    addMouseTool(tr("Left Drag"), "left_drag", tr("Perform drag operation on target"));

    m_toolsModel->appendRow(mouseGroup);

    // Keyboard tools group
    QStandardItem *keyboardGroup = new QStandardItem(tr("Keyboard Tools"));
    keyboardGroup->setCheckable(true);
    keyboardGroup->setCheckState(Qt::Checked);
    keyboardGroup->setEditable(false);
    keyboardGroup->setFont(f);

    auto addKeyboardTool = [&](const QString &name, const QString &toolId, const QString &desc) {
        QStandardItem *item = new QStandardItem(name);
        item->setCheckable(true);
        item->setCheckState(Qt::Checked);
        item->setEditable(false);
        item->setData(toolId, Qt::UserRole + 1);
        item->setToolTip(desc);

        QStandardItem *descItem = new QStandardItem(toolId);
        descItem->setEditable(false);
        descItem->setForeground(QColor(128, 128, 128));

        keyboardGroup->appendRow({item, descItem});
    };

    addKeyboardTool(tr("Type Text"), "type_text", tr("Type text on target keyboard"));
    addKeyboardTool(tr("Press Key"), "press_key", tr("Press key combinations on target"));
    addKeyboardTool(tr("Repeat Key"), "repeat_key", tr("Press a key repeatedly (e.g., for BIOS entry)"));

    m_toolsModel->appendRow(keyboardGroup);

    // Recording tools group
    QStandardItem *recordingGroup = new QStandardItem(tr("Recording Tools"));
    recordingGroup->setCheckable(true);
    recordingGroup->setCheckState(Qt::Checked);
    recordingGroup->setEditable(false);
    recordingGroup->setFont(f);

    QStandardItem *startRecording = new QStandardItem(tr("Start Recording"));
    startRecording->setCheckable(true);
    startRecording->setCheckState(Qt::Checked);
    startRecording->setEditable(false);
    startRecording->setData("start_recording", Qt::UserRole + 1);
    startRecording->setToolTip(tr("Start screen recording"));

    QStandardItem *startRecordingDesc = new QStandardItem("start_recording");
    startRecordingDesc->setEditable(false);
    startRecordingDesc->setForeground(QColor(128, 128, 128));

    QStandardItem *stopRecording = new QStandardItem(tr("Stop Recording"));
    stopRecording->setCheckable(true);
    stopRecording->setCheckState(Qt::Checked);
    stopRecording->setEditable(false);
    stopRecording->setData("stop_recording", Qt::UserRole + 1);
    stopRecording->setToolTip(tr("Stop screen recording"));

    QStandardItem *stopRecordingDesc = new QStandardItem("stop_recording");
    stopRecordingDesc->setEditable(false);
    stopRecordingDesc->setForeground(QColor(128, 128, 128));

    recordingGroup->appendRow({startRecording, startRecordingDesc});
    recordingGroup->appendRow({stopRecording, stopRecordingDesc});
    m_toolsModel->appendRow(recordingGroup);

    // System/Host tools group
    QStandardItem *systemGroup = new QStandardItem(tr("System/Host Tools"));
    systemGroup->setCheckable(true);
    systemGroup->setCheckState(Qt::Checked);
    systemGroup->setEditable(false);
    systemGroup->setFont(f);

    QStandardItem *setTargetSystem = new QStandardItem(tr("Set Target System"));
    setTargetSystem->setCheckable(true);
    setTargetSystem->setCheckState(Qt::Checked);
    setTargetSystem->setEditable(false);
    setTargetSystem->setData("set_target_system", Qt::UserRole + 1);
    setTargetSystem->setToolTip(tr("Change target OS/environment setting"));

    QStandardItem *setTargetSystemDesc = new QStandardItem("set_target_system");
    setTargetSystemDesc->setEditable(false);
    setTargetSystemDesc->setForeground(QColor(128, 128, 128));

    QStandardItem *runBash = new QStandardItem(tr("Run Bash (Host)"));
    runBash->setCheckable(true);
    runBash->setCheckState(Qt::Checked);
    runBash->setEditable(false);
    runBash->setData("run_bash", Qt::UserRole + 1);
    runBash->setToolTip(tr("Run commands on the host machine (not target)"));

    QStandardItem *runBashDesc = new QStandardItem("run_bash");
    runBashDesc->setEditable(false);
    runBashDesc->setForeground(QColor(128, 128, 128));

    systemGroup->appendRow({setTargetSystem, setTargetSystemDesc});
    systemGroup->appendRow({runBash, runBashDesc});
    m_toolsModel->appendRow(systemGroup);

    m_toolsTreeView->expandAll();
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

    // Tools configuration - load from settings into tree
    auto &settings2 = GlobalSetting::instance();
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *toolItem = group->child(c, 0);
            QString toolId = toolItem->data(Qt::UserRole + 1).toString();
            bool enabled = settings2.getChatToolEnabled(toolId);
            toolItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        }
    }

    // Update group check states based on children
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        bool allChecked = true;
        bool anyChecked = false;
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable()) {
                if (child->checkState() == Qt::Checked) anyChecked = true;
                else allChecked = false;
            }
        }
        if (allChecked) group->setCheckState(Qt::Checked);
        else if (anyChecked) group->setCheckState(Qt::PartiallyChecked);
        else group->setCheckState(Qt::Unchecked);
    }

    // Update select all checkbox
    bool allToolsChecked = true;
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable() && child->checkState() != Qt::Checked) {
                allToolsChecked = false;
                break;
            }
        }
        if (!allToolsChecked) break;
    }
    m_selectAllToolsCheck->blockSignals(true);
    m_selectAllToolsCheck->setChecked(allToolsChecked);
    m_selectAllToolsCheck->blockSignals(false);

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

    // Tools configuration - save from tree to settings
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *toolItem = group->child(c, 0);
            QString toolId = toolItem->data(Qt::UserRole + 1).toString();
            bool enabled = toolItem->checkState() == Qt::Checked;
            settings.setChatToolEnabled(toolId, enabled);
        }
    }

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
    // Tool snapshots - read from tree
    m_snap_screenCapture = false;
    m_snap_screenToMarkdown = false;
    m_snap_moveMouse = false;
    m_snap_leftClick = false;
    m_snap_rightClick = false;
    m_snap_doubleClick = false;
    m_snap_leftDrag = false;
    m_snap_typeText = false;
    m_snap_pressKey = false;
    m_snap_repeatKey = false;
    m_snap_startRecording = false;
    m_snap_stopRecording = false;
    m_snap_setTargetSystem = false;
    m_snap_runBash = false;

    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *toolItem = group->child(c, 0);
            QString toolId = toolItem->data(Qt::UserRole + 1).toString();
            bool checked = toolItem->checkState() == Qt::Checked;

            if (toolId == "capture_screen") m_snap_screenCapture = checked;
            else if (toolId == "screen_to_markdown") m_snap_screenToMarkdown = checked;
            else if (toolId == "move_mouse") m_snap_moveMouse = checked;
            else if (toolId == "left_click") m_snap_leftClick = checked;
            else if (toolId == "right_click") m_snap_rightClick = checked;
            else if (toolId == "double_click") m_snap_doubleClick = checked;
            else if (toolId == "left_drag") m_snap_leftDrag = checked;
            else if (toolId == "type_text") m_snap_typeText = checked;
            else if (toolId == "press_key") m_snap_pressKey = checked;
            else if (toolId == "repeat_key") m_snap_repeatKey = checked;
            else if (toolId == "start_recording") m_snap_startRecording = checked;
            else if (toolId == "stop_recording") m_snap_stopRecording = checked;
            else if (toolId == "set_target_system") m_snap_setTargetSystem = checked;
            else if (toolId == "run_bash") m_snap_runBash = checked;
        }
    }
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

    // Tool checkboxes - restore to tree
    auto setToolCheck = [this](const QString &toolId, bool checked) {
        for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
            QStandardItem *group = m_toolsModel->item(g);
            for (int c = 0; c < group->rowCount(); ++c) {
                QStandardItem *toolItem = group->child(c, 0);
                if (toolItem->data(Qt::UserRole + 1).toString() == toolId) {
                    toolItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                    return;
                }
            }
        }
    };

    setToolCheck("capture_screen", m_snap_screenCapture);
    setToolCheck("screen_to_markdown", m_snap_screenToMarkdown);
    setToolCheck("move_mouse", m_snap_moveMouse);
    setToolCheck("left_click", m_snap_leftClick);
    setToolCheck("right_click", m_snap_rightClick);
    setToolCheck("double_click", m_snap_doubleClick);
    setToolCheck("left_drag", m_snap_leftDrag);
    setToolCheck("type_text", m_snap_typeText);
    setToolCheck("press_key", m_snap_pressKey);
    setToolCheck("repeat_key", m_snap_repeatKey);
    setToolCheck("start_recording", m_snap_startRecording);
    setToolCheck("stop_recording", m_snap_stopRecording);
    setToolCheck("set_target_system", m_snap_setTargetSystem);
    setToolCheck("run_bash", m_snap_runBash);

    // Update group check states
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        bool allChecked = true;
        bool anyChecked = false;
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable()) {
                if (child->checkState() == Qt::Checked) anyChecked = true;
                else allChecked = false;
            }
        }
        if (allChecked) group->setCheckState(Qt::Checked);
        else if (anyChecked) group->setCheckState(Qt::PartiallyChecked);
        else group->setCheckState(Qt::Unchecked);
    }

    // Update select all
    bool allToolsChecked = true;
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable() && child->checkState() != Qt::Checked) {
                allToolsChecked = false;
                break;
            }
        }
        if (!allToolsChecked) break;
    }
    m_selectAllToolsCheck->blockSignals(true);
    m_selectAllToolsCheck->setChecked(allToolsChecked);
    m_selectAllToolsCheck->blockSignals(false);
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

    // Tool checkboxes - check from tree
    auto getToolCheck = [this](const QString &toolId) -> bool {
        for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
            QStandardItem *group = m_toolsModel->item(g);
            for (int c = 0; c < group->rowCount(); ++c) {
                QStandardItem *toolItem = group->child(c, 0);
                if (toolItem->data(Qt::UserRole + 1).toString() == toolId) {
                    return toolItem->checkState() == Qt::Checked;
                }
            }
        }
        return false;
    };

    if (getToolCheck("capture_screen") != m_snap_screenCapture) return false;
    if (getToolCheck("screen_to_markdown") != m_snap_screenToMarkdown) return false;
    if (getToolCheck("move_mouse") != m_snap_moveMouse) return false;
    if (getToolCheck("left_click") != m_snap_leftClick) return false;
    if (getToolCheck("right_click") != m_snap_rightClick) return false;
    if (getToolCheck("double_click") != m_snap_doubleClick) return false;
    if (getToolCheck("left_drag") != m_snap_leftDrag) return false;
    if (getToolCheck("type_text") != m_snap_typeText) return false;
    if (getToolCheck("press_key") != m_snap_pressKey) return false;
    if (getToolCheck("repeat_key") != m_snap_repeatKey) return false;
    if (getToolCheck("start_recording") != m_snap_startRecording) return false;
    if (getToolCheck("stop_recording") != m_snap_stopRecording) return false;
    if (getToolCheck("set_target_system") != m_snap_setTargetSystem) return false;
    if (getToolCheck("run_bash") != m_snap_runBash) return false;

    return true;
}
