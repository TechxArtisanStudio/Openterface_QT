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

#ifndef MCPPAGE_H
#define MCPPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QLabel>
#include "preferencepagebase.h"

/**
 * Preferences page for the MCP (Model Context Protocol) server.
 *
 * Configures all MCP transport parameters:
 *   - Master enable/disable toggle
 *   - Transport mode (Stdio / SSE HTTP)
 *   - Port, bind address, paths, keepalive, session timeout, max sessions (SSE mode)
 *
 * Changes are persisted via GlobalSetting on applySettings(), and the
 * mcpSettingsChanged() signal is emitted so the MainWindow can restart the
 * MCP server with the new configuration.
 */
class McpPage : public PreferencePageBase
{
    Q_OBJECT

public:
    explicit McpPage(QWidget *parent = nullptr);

    void setupUI();
    void initMcpSettings();
    void applySettings() override;
    void captureSnapshot() override;
    void revertToSnapshot() override;

signals:
    void mcpSettingsChanged();

private slots:
    void onTransportModeChanged(int index);
    void onBindAddressPresetChanged(int index);

private:
    // ---- Basic settings ----
    QCheckBox    *m_enableCheckBox;
    QComboBox    *m_transportCombo;

    // ---- SSE settings ----
    QGroupBox    *m_sseGroup;
    QSpinBox     *m_ssePortSpin;
    QComboBox    *m_sseBindPresetCombo;
    QLineEdit    *m_sseBindCustomEdit;
    QLabel       *m_sseBindCustomLabel;
    QLineEdit    *m_ssePathSseEdit;
    QLineEdit    *m_ssePathMessagesEdit;
    QSpinBox     *m_sseKeepaliveSpin;
    QSpinBox     *m_sseSessionTimeoutSpin;
    QSpinBox     *m_sseCleanupIntervalSpin;
    QSpinBox     *m_sseMaxSessionsSpin;

    // Snapshot members
    bool m_snap_enableChecked;
    int m_snap_transportIndex;
    int m_snap_ssePort;
    int m_snap_sseBindPresetIndex;
    QString m_snap_sseBindCustom;
    QString m_snap_ssePathSse;
    QString m_snap_ssePathMessages;
    int m_snap_sseKeepalive;
    int m_snap_sseSessionTimeout;
    int m_snap_sseCleanupInterval;
    int m_snap_sseMaxSessions;
};

#endif // MCPPAGE_H
