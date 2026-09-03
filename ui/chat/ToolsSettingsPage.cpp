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

#include "ToolsSettingsPage.h"
#include "../globalsetting.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QScrollArea>
#include <QFrame>

ToolsSettingsPage::ToolsSettingsPage(QWidget *parent)
    : PreferencePageBase(parent)
{
    setupUI();
    initToolsSettings();
}

void ToolsSettingsPage::setupUI()
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

    // ---- Page title ----
    auto *titleLabel = new QLabel(tr("Tools Configuration"));
    titleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; }");
    contentLayout->addWidget(titleLabel);

    auto *descLabel = new QLabel(tr("Enable or disable specific AI tools. Disabled tools will not be available to the AI agent."));
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: gray; font-size: 11px;");
    contentLayout->addWidget(descLabel);

    // ---- Tools Configuration Group (Tree View) ----
    auto *toolsGroup = new QGroupBox(tr("Available Tools"));
    auto *toolsLayout = new QVBoxLayout(toolsGroup);

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
    m_toolsTreeView->setMinimumHeight(400);

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

    // ---- Web Search Provider Configuration Group ----
    auto *webSearchGroup = new QGroupBox(tr("Web Search Providers"));
    auto *webSearchLayout = new QVBoxLayout(webSearchGroup);

    auto *webSearchInfoLabel = new QLabel(tr("Configure web search providers for the AI agent. "
        "Providers are tried in order (top to bottom) until one succeeds. "
        "Drag to reorder priority. Check to enable."));
    webSearchInfoLabel->setWordWrap(true);
    webSearchInfoLabel->setStyleSheet("color: gray; font-size: 11px;");
    webSearchLayout->addWidget(webSearchInfoLabel);

    // Provider list with checkboxes and drag-to-reorder
    m_webSearchProviderList = new QListWidget();
    m_webSearchProviderList->setDragDropMode(QAbstractItemView::InternalMove);
    m_webSearchProviderList->setDefaultDropAction(Qt::MoveAction);
    m_webSearchProviderList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_webSearchProviderList->setMaximumHeight(180);
    m_webSearchProviderList->setToolTip(tr("Drag to reorder provider priority. Check to enable."));
    webSearchLayout->addWidget(m_webSearchProviderList);

    // Exa API key
    auto *exaLayout = new QHBoxLayout();
    m_exaApiKeyEdit = new QLineEdit();
    m_exaApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_exaApiKeyEdit->setPlaceholderText(tr("Enter Exa API key from exa.ai"));
    m_exaApiKeyEdit->setToolTip(tr("API key for Exa AI search (https://exa.ai). Required for Exa provider."));
    exaLayout->addWidget(new QLabel(tr("Exa API Key:")));
    exaLayout->addWidget(m_exaApiKeyEdit);
    webSearchLayout->addLayout(exaLayout);

    // Parallel API key
    auto *parallelLayout = new QHBoxLayout();
    m_parallelApiKeyEdit = new QLineEdit();
    m_parallelApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_parallelApiKeyEdit->setPlaceholderText(tr("Enter Parallel API key from parallel.ai"));
    m_parallelApiKeyEdit->setToolTip(tr("API key for Parallel search (https://parallel.ai). Required for Parallel provider."));
    parallelLayout->addWidget(new QLabel(tr("Parallel API Key:")));
    parallelLayout->addWidget(m_parallelApiKeyEdit);
    webSearchLayout->addLayout(parallelLayout);

    // Populate the provider list
    populateWebSearchProviders();

    contentLayout->addWidget(webSearchGroup);

    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);

    // ---- Apply / Revert / Cancel button bar (from PreferencePageBase) ----
    createButtonBar(mainLayout);

    // ---- Wire widget change signals to dirty-state checking ----
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

    // Web search provider list changes
    connect(m_webSearchProviderList, &QListWidget::itemChanged, this, [this]{ checkDirtyState(); });
    connect(m_webSearchProviderList->model(), &QAbstractItemModel::rowsMoved, this, [this]{ checkDirtyState(); });
    connect(m_exaApiKeyEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_parallelApiKeyEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
}

void ToolsSettingsPage::populateToolsTree()
{
    m_toolsModel->removeRows(0, m_toolsModel->rowCount());

    QFont f;
    f.setBold(true);

    // Screen tools group
    QStandardItem *screenGroup = new QStandardItem(tr("Screen Tools"));
    screenGroup->setCheckable(true);
    screenGroup->setCheckState(Qt::Checked);
    screenGroup->setEditable(false);
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

    QStandardItem *webSearch = new QStandardItem(tr("Web Search"));
    webSearch->setCheckable(true);
    webSearch->setCheckState(Qt::Checked);
    webSearch->setEditable(false);
    webSearch->setData("web_search", Qt::UserRole + 1);
    webSearch->setToolTip(tr("Search the internet for information"));

    QStandardItem *webSearchDesc = new QStandardItem("web_search");
    webSearchDesc->setEditable(false);
    webSearchDesc->setForeground(QColor(128, 128, 128));

    QStandardItem *detectCursor = new QStandardItem(tr("Detect Cursor (Terminal Idle)"));
    detectCursor->setCheckable(true);
    detectCursor->setCheckState(Qt::Checked);
    detectCursor->setEditable(false);
    detectCursor->setData("detect_cursor", Qt::UserRole + 1);
    detectCursor->setToolTip(tr("Detect whether the target terminal is idle and waiting for input"));

    QStandardItem *detectCursorDesc = new QStandardItem("detect_cursor");
    detectCursorDesc->setEditable(false);
    detectCursorDesc->setForeground(QColor(128, 128, 128));

    systemGroup->appendRow({setTargetSystem, setTargetSystemDesc});
    systemGroup->appendRow({runBash, runBashDesc});
    systemGroup->appendRow({webSearch, webSearchDesc});
    systemGroup->appendRow({detectCursor, detectCursorDesc});
    m_toolsModel->appendRow(systemGroup);

    m_toolsTreeView->expandAll();
}

void ToolsSettingsPage::initToolsSettings()
{
    auto &settings = GlobalSetting::instance();

    // Tools configuration - load from settings into tree
    for (int g = 0; g < m_toolsModel->rowCount(); ++g) {
        QStandardItem *group = m_toolsModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *toolItem = group->child(c, 0);
            QString toolId = toolItem->data(Qt::UserRole + 1).toString();
            bool enabled = settings.getChatToolEnabled(toolId);
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

    // Web Search Providers
    populateWebSearchProviders();
    m_exaApiKeyEdit->setText(settings.getChatExaApiKey());
    m_parallelApiKeyEdit->setText(settings.getChatParallelApiKey());

    captureSnapshot();
    clearDirty();
}

void ToolsSettingsPage::applySettings()
{
    auto &settings = GlobalSetting::instance();

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

    // Web Search Providers - save checked providers in list order
    QStringList enabledProviders;
    for (int i = 0; i < m_webSearchProviderList->count(); ++i) {
        auto *item = m_webSearchProviderList->item(i);
        if (item->checkState() == Qt::Checked) {
            enabledProviders.append(item->data(Qt::UserRole).toString());
        }
    }
    settings.setChatWebSearchProviders(enabledProviders);
    settings.setChatExaApiKey(m_exaApiKeyEdit->text().trimmed());
    settings.setChatParallelApiKey(m_parallelApiKeyEdit->text().trimmed());

    emit toolsSettingsChanged();
}

void ToolsSettingsPage::captureSnapshot()
{
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
    m_snap_webSearch = false;

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
            else if (toolId == "web_search") m_snap_webSearch = checked;
        }
    }

    // Web search provider snapshots
    m_snap_webSearchProviders.clear();
    for (int i = 0; i < m_webSearchProviderList->count(); ++i) {
        auto *item = m_webSearchProviderList->item(i);
        if (item->checkState() == Qt::Checked) {
            m_snap_webSearchProviders.append(item->data(Qt::UserRole).toString());
        }
    }
    m_snap_exaApiKey = m_exaApiKeyEdit->text();
    m_snap_parallelApiKey = m_parallelApiKeyEdit->text();
}

void ToolsSettingsPage::revertToSnapshot()
{
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
    setToolCheck("web_search", m_snap_webSearch);

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

    // Web search providers - restore by repopulating
    populateWebSearchProviders();
    // Now restore check state based on snapshot
    for (int i = 0; i < m_webSearchProviderList->count(); ++i) {
        auto *item = m_webSearchProviderList->item(i);
        QString id = item->data(Qt::UserRole).toString();
        item->setCheckState(m_snap_webSearchProviders.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    m_exaApiKeyEdit->setText(m_snap_exaApiKey);
    m_parallelApiKeyEdit->setText(m_snap_parallelApiKey);
}

bool ToolsSettingsPage::valuesMatchSnapshot() const
{
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
    if (getToolCheck("web_search") != m_snap_webSearch) return false;

    // Web search providers - check order and enabled state
    QStringList currentProviders;
    for (int i = 0; i < m_webSearchProviderList->count(); ++i) {
        auto *item = m_webSearchProviderList->item(i);
        if (item->checkState() == Qt::Checked) {
            currentProviders.append(item->data(Qt::UserRole).toString());
        }
    }
    if (currentProviders != m_snap_webSearchProviders) return false;
    if (m_exaApiKeyEdit->text() != m_snap_exaApiKey) return false;
    if (m_parallelApiKeyEdit->text() != m_snap_parallelApiKey) return false;

    return true;
}

void ToolsSettingsPage::populateWebSearchProviders()
{
    m_webSearchProviderList->clear();

    // Define all available providers with their descriptions
    struct ProviderInfo {
        QString id;
        QString name;
        QString description;
        bool requiresKey;
    };

    QList<ProviderInfo> providers = {
        {"duckduckgo", "DuckDuckGo", "Free search for well-known topics", false},
        {"wikipedia", "Wikipedia", "Wikipedia article summaries", false},
        {"exa", "Exa AI", "AI-optimized semantic search (requires API key)", true},
        {"parallel", "Parallel", "AI-optimized web search (requires API key)", true}
    };

    // Get configured providers from settings
    auto &settings = GlobalSetting::instance();
    QStringList configuredIds = settings.getChatWebSearchProviders();

    // First, add configured providers in order (these are enabled and prioritized)
    for (const QString &id : configuredIds) {
        for (const auto &prov : providers) {
            if (prov.id == id) {
                auto *item = new QListWidgetItem();
                item->setText(QString("%1 - %2").arg(prov.name, prov.description));
                item->setData(Qt::UserRole, prov.id);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
                item->setCheckState(Qt::Checked);
                m_webSearchProviderList->addItem(item);
                break;
            }
        }
    }

    // Then, add remaining unconfigured providers (unchecked)
    for (const auto &prov : providers) {
        if (!configuredIds.contains(prov.id)) {
            auto *item = new QListWidgetItem();
            item->setText(QString("%1 - %2").arg(prov.name, prov.description));
            item->setData(Qt::UserRole, prov.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
            item->setCheckState(Qt::Unchecked);
            m_webSearchProviderList->addItem(item);
        }
    }
}
