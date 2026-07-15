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

#include "mcppage.h"
#include "../globalsetting.h"
#include "fontstyle.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFrame>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_ui_mcp_page, "opf.ui.mcp.page")

// ---- Presets for the SSE bind-address combo ----
static const char *kBindPresetAny       = "0.0.0.0";
static const char *kBindPresetLoopback  = "127.0.0.1";
static const int   kBindPresetCustomIdx = 2;

McpPage::McpPage(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    initMcpSettings();
}

// ============================================================================
// UI construction
// ============================================================================

void McpPage::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ---- Page title ----
    QLabel *titleLabel = new QLabel(tr("MCP Server"));
    titleLabel->setStyleSheet(bigLabelFontSize + " QLabel { font-weight: bold; }");
    mainLayout->addWidget(titleLabel);

    QLabel *descLabel = new QLabel(
        tr("Configure the Model Context Protocol (MCP) server.\n"
           "MCP allows AI clients to control the KVM hardware remotely."));
    descLabel->setStyleSheet(commentsFontSize);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // ================================================================
    // Group 1 — Basic Settings
    // ================================================================
    QGroupBox *basicGroup = new QGroupBox(tr("Basic Settings"));
    basicGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QFormLayout *basicForm = new QFormLayout(basicGroup);

    m_enableCheckBox = new QCheckBox(tr("Enable MCP Server"));
    m_enableCheckBox->setToolTip(
        tr("Start the MCP server automatically when the application launches."));
    basicForm->addRow(m_enableCheckBox);

    m_transportCombo = new QComboBox();
    m_transportCombo->addItem(tr("Stdio (local)"),       "stdio");
    m_transportCombo->addItem(tr("SSE (HTTP remote)"),   "sse");
    m_transportCombo->setToolTip(
        tr("Stdio communicates via standard input/output (for local AI clients).\n"
           "SSE (Server-Sent Events) allows remote HTTP clients."));
    basicForm->addRow(tr("Transport Mode:"), m_transportCombo);

    mainLayout->addWidget(basicGroup);

    // ================================================================
    // SSE settings (only visible when SSE mode is selected)
    // ================================================================
    m_sseGroup = new QGroupBox(tr("SSE (HTTP) Settings"));
    m_sseGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QFormLayout *sseForm = new QFormLayout(m_sseGroup);

    // -- Port --
    m_ssePortSpin = new QSpinBox();
    m_ssePortSpin->setRange(1, 65535);
    m_ssePortSpin->setToolTip(tr("TCP port for the SSE HTTP server."));
    sseForm->addRow(tr("Port:"), m_ssePortSpin);

    // -- Bind address preset --
    m_sseBindPresetCombo = new QComboBox();
    m_sseBindPresetCombo->addItem(tr("All interfaces (0.0.0.0)"),  kBindPresetAny);
    m_sseBindPresetCombo->addItem(tr("Localhost only (127.0.0.1)"), kBindPresetLoopback);
    m_sseBindPresetCombo->addItem(tr("Custom..."),                  "custom");
    m_sseBindPresetCombo->setToolTip(
        tr("Which network interface to bind the SSE server to."));
    sseForm->addRow(tr("Bind Address:"), m_sseBindPresetCombo);

    // -- Custom bind address --
    m_sseBindCustomLabel = new QLabel(tr("Custom Address:"));
    m_sseBindCustomEdit  = new QLineEdit();
    m_sseBindCustomEdit->setPlaceholderText("192.168.1.100");
    m_sseBindCustomEdit->setToolTip(tr("Enter a specific IP address to bind to."));
    m_sseBindCustomEdit->setVisible(false);
    m_sseBindCustomLabel->setVisible(false);
    sseForm->addRow(m_sseBindCustomLabel, m_sseBindCustomEdit);

    // -- Separator: Paths --
    QFrame *sep1 = new QFrame();
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    sseForm->addRow(sep1);

    QLabel *pathHeader = new QLabel(tr("Paths"));
    pathHeader->setStyleSheet(smallLabelFontSize + " QLabel { font-weight: bold; }");
    sseForm->addRow(pathHeader);

    m_ssePathSseEdit = new QLineEdit();
    m_ssePathSseEdit->setPlaceholderText("/sse");
    m_ssePathSseEdit->setToolTip(tr("URL path for the SSE event stream endpoint."));
    sseForm->addRow(tr("SSE Path:"), m_ssePathSseEdit);

    m_ssePathMessagesEdit = new QLineEdit();
    m_ssePathMessagesEdit->setPlaceholderText("/messages");
    m_ssePathMessagesEdit->setToolTip(tr("URL path for posting JSON-RPC messages."));
    sseForm->addRow(tr("Messages Path:"), m_ssePathMessagesEdit);

    // -- Separator: Session config --
    QFrame *sep2 = new QFrame();
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    sseForm->addRow(sep2);

    QLabel *sessionHeader = new QLabel(tr("Session Configuration"));
    sessionHeader->setStyleSheet(smallLabelFontSize + " QLabel { font-weight: bold; }");
    sseForm->addRow(sessionHeader);

    m_sseKeepaliveSpin = new QSpinBox();
    m_sseKeepaliveSpin->setRange(1, 60);
    m_sseKeepaliveSpin->setSingleStep(1);
    m_sseKeepaliveSpin->setSuffix(tr(" s"));
    m_sseKeepaliveSpin->setToolTip(
        tr("How often to send keepalive comments on the SSE stream "
           "to prevent proxies and firewalls from closing idle connections."));
    sseForm->addRow(tr("Keepalive Interval:"), m_sseKeepaliveSpin);

    m_sseSessionTimeoutSpin = new QSpinBox();
    m_sseSessionTimeoutSpin->setRange(60, 3600);
    m_sseSessionTimeoutSpin->setSingleStep(60);
    m_sseSessionTimeoutSpin->setSuffix(tr(" s"));
    m_sseSessionTimeoutSpin->setToolTip(
        tr("Time after which an idle SSE session is automatically closed."));
    sseForm->addRow(tr("Session Timeout:"), m_sseSessionTimeoutSpin);

    m_sseCleanupIntervalSpin = new QSpinBox();
    m_sseCleanupIntervalSpin->setRange(10, 300);
    m_sseCleanupIntervalSpin->setSingleStep(10);
    m_sseCleanupIntervalSpin->setSuffix(tr(" s"));
    m_sseCleanupIntervalSpin->setToolTip(
        tr("How often to scan for and remove stale sessions."));
    sseForm->addRow(tr("Cleanup Interval:"), m_sseCleanupIntervalSpin);

    m_sseMaxSessionsSpin = new QSpinBox();
    m_sseMaxSessionsSpin->setRange(1, 64);
    m_sseMaxSessionsSpin->setToolTip(
        tr("Maximum number of simultaneous SSE sessions."));
    sseForm->addRow(tr("Max Sessions:"), m_sseMaxSessionsSpin);

    mainLayout->addWidget(m_sseGroup);

    // ---- Stretch ----
    mainLayout->addStretch();

    // ---- Internal signal connections ----
    connect(m_transportCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &McpPage::onTransportModeChanged);
    connect(m_sseBindPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &McpPage::onBindAddressPresetChanged);
}

// ============================================================================
// Load saved values from GlobalSetting
// ============================================================================

void McpPage::initMcpSettings()
{
    GlobalSetting &s = GlobalSetting::instance();

    m_enableCheckBox->setChecked(s.getMcpEnabled());

    // Transport mode
    QString transport = s.getMcpTransport();
    int transportIdx = (transport == "sse") ? 1 : 0;
    m_transportCombo->setCurrentIndex(transportIdx);

    // Show/hide SSE group based on transport mode
    m_sseGroup->setVisible(transportIdx == 1);

    // SSE
    m_ssePortSpin->setValue(s.getMcpSsePort());

    // Bind address — resolve to preset index
    QString bindAddr = s.getMcpSseBindAddress();
    if (bindAddr == QLatin1String(kBindPresetAny)) {
        m_sseBindPresetCombo->setCurrentIndex(0);
    } else if (bindAddr == QLatin1String(kBindPresetLoopback)) {
        m_sseBindPresetCombo->setCurrentIndex(1);
    } else {
        m_sseBindPresetCombo->setCurrentIndex(kBindPresetCustomIdx);
        m_sseBindCustomEdit->setText(bindAddr);
        m_sseBindCustomEdit->setVisible(true);
        m_sseBindCustomLabel->setVisible(true);
    }

    m_ssePathSseEdit->setText(s.getMcpSsePathSse());
    m_ssePathMessagesEdit->setText(s.getMcpSsePathMessages());

    // Convert milliseconds (storage) to seconds (UI)
    m_sseKeepaliveSpin->setValue(s.getMcpSseKeepaliveInterval() / 1000);
    m_sseSessionTimeoutSpin->setValue(s.getMcpSseSessionTimeout() / 1000);
    m_sseCleanupIntervalSpin->setValue(s.getMcpSseCleanupInterval() / 1000);
    m_sseMaxSessionsSpin->setValue(s.getMcpSseMaxSessions());
}

// ============================================================================
// Save values and emit change signal
// ============================================================================

void McpPage::applyMcpSettings()
{
    GlobalSetting &s = GlobalSetting::instance();

    s.setMcpEnabled(m_enableCheckBox->isChecked());

    QString transport = m_transportCombo->currentData().toString();
    s.setMcpTransport(transport);

    s.setMcpSsePort(m_ssePortSpin->value());

    // Resolve bind address from preset combo
    QString bindAddr;
    int presetIdx = m_sseBindPresetCombo->currentIndex();
    if (presetIdx == kBindPresetCustomIdx) {
        bindAddr = m_sseBindCustomEdit->text().trimmed();
        if (bindAddr.isEmpty())
            bindAddr = QLatin1String(kBindPresetAny);
    } else {
        bindAddr = m_sseBindPresetCombo->currentData().toString();
    }
    s.setMcpSseBindAddress(bindAddr);

    QString pathSse = m_ssePathSseEdit->text().trimmed();
    if (pathSse.isEmpty()) pathSse = "/sse";
    s.setMcpSsePathSse(pathSse);

    QString pathMsg = m_ssePathMessagesEdit->text().trimmed();
    if (pathMsg.isEmpty()) pathMsg = "/messages";
    s.setMcpSsePathMessages(pathMsg);

    // Convert seconds (UI) to milliseconds (storage)
    s.setMcpSseKeepaliveInterval(m_sseKeepaliveSpin->value() * 1000);
    s.setMcpSseSessionTimeout(m_sseSessionTimeoutSpin->value() * 1000);
    s.setMcpSseCleanupInterval(m_sseCleanupIntervalSpin->value() * 1000);
    s.setMcpSseMaxSessions(m_sseMaxSessionsSpin->value());

    qCInfo(log_ui_mcp_page) << "MCP settings saved — enabled:"
                             << m_enableCheckBox->isChecked()
                             << "transport:" << transport;

    emit mcpSettingsChanged();
}

// ============================================================================
// Slots
// ============================================================================

void McpPage::onTransportModeChanged(int index)
{
    m_sseGroup->setVisible(index == 1);
}

void McpPage::onBindAddressPresetChanged(int index)
{
    bool custom = (index == kBindPresetCustomIdx);
    m_sseBindCustomEdit->setVisible(custom);
    m_sseBindCustomLabel->setVisible(custom);
    if (custom) {
        m_sseBindCustomEdit->setFocus();
    }
}
