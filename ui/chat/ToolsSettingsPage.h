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

#ifndef TOOLS_SETTINGS_PAGE_H
#define TOOLS_SETTINGS_PAGE_H

#include "../preferences/preferencepagebase.h"
#include <QTreeView>
#include <QStandardItemModel>
#include <QCheckBox>
#include <QListWidget>
#include <QLineEdit>
#include <QStringList>

/**
 * Preferences page for AI Tools configuration.
 *
 * Configures:
 *   - Which AI tools are enabled/disabled for the AI agent
 *   - Web Search Provider configuration (providers, API keys, priority)
 *
 * Tools are organized in a tree structure with groups:
 *   - Screen Tools (capture_screen, screen_to_markdown)
 *   - Mouse Tools (move_mouse, left_click, right_click, double_click, left_drag)
 *   - Keyboard Tools (type_text, press_key, repeat_key)
 *   - Recording Tools (start_recording, stop_recording)
 *   - System/Host Tools (set_target_system, run_bash, web_search)
 *
 * Inherits PreferencePageBase so it gets an Apply/Revert/Cancel button bar
 * with dirty-state tracking.
 */
class ToolsSettingsPage : public PreferencePageBase
{
    Q_OBJECT

public:
    explicit ToolsSettingsPage(QWidget *parent = nullptr);

    void setupUI();
    void initToolsSettings();
    void populateToolsTree();
    void populateWebSearchProviders();

    // PreferencePageBase overrides
    void applySettings() override;
    void captureSnapshot() override;
    bool valuesMatchSnapshot() const override;
    void revertToSnapshot() override;

signals:
    void toolsSettingsChanged();

private:
    // Tools tree view
    QTreeView *m_toolsTreeView;
    QStandardItemModel *m_toolsModel;
    QCheckBox *m_selectAllToolsCheck;

    // Web Search Provider Configuration
    QListWidget *m_webSearchProviderList;  // List of providers with checkboxes and priority
    QLineEdit   *m_exaApiKeyEdit;
    QLineEdit   *m_parallelApiKeyEdit;

    // Snapshot members for tool states
    bool m_snap_screenCapture;
    bool m_snap_screenToMarkdown;
    bool m_snap_moveMouse;
    bool m_snap_leftClick;
    bool m_snap_rightClick;
    bool m_snap_doubleClick;
    bool m_snap_leftDrag;
    bool m_snap_typeText;
    bool m_snap_pressKey;
    bool m_snap_repeatKey;
    bool m_snap_startRecording;
    bool m_snap_stopRecording;
    bool m_snap_setTargetSystem;
    bool m_snap_runBash;
    bool m_snap_webSearch;

    // Web search provider snapshots
    QStringList m_snap_webSearchProviders;
    QString m_snap_exaApiKey;
    QString m_snap_parallelApiKey;
};

#endif // TOOLS_SETTINGS_PAGE_H
