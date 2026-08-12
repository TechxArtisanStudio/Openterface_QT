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

/**
 * Preferences page for the MCP (Model Context Protocol) server.
 *
 * Configures all MCP transport parameters:
 *   - Master enable/disable toggle
 *   - Transport mode (Stdio / SSE HTTP)
 *   - Port, bind address, paths, keepalive, session timeout, max sessions (SSE mode)
 *
 * Changes are persisted via GlobalSetting on applyMcpSettings(), and the
 * mcpSettingsChanged() signal is emitted so the MainWindow can restart the
 * MCP server with the new configuration.
 */
class McpPage : public QWidget
{
    Q_OBJECT

public:
    explicit McpPage(QWidget *parent = nullptr);

    void setupUI();
    void initMcpSettings();
    void applyMcpSettings();

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
};

#endif // MCPPAGE_H
