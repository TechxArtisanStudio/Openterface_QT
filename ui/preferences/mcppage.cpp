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
#include <QPushButton>
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_ui_mcp_page, "opf.ui.mcp.page")

// ---- Presets for the SSE bind-address combo ----
static const char *kBindPresetAny       = "0.0.0.0";
static const char *kBindPresetLoopback  = "127.0.0.1";
static const int   kBindPresetCustomIdx = 2;

McpPage::McpPage(QWidget *parent)
    : PreferencePageBase(parent)
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

    // Screen to Markdown feature
    m_screenToMarkdownCheckBox = new QCheckBox(tr("Enable Screen to Markdown (OCR)"));
    m_screenToMarkdownCheckBox->setToolTip(
        tr("Enable the screen_to_markdown MCP tool which uses OCR to extract text\n"
           "and UI elements from the target screen. This helps AI agents find buttons\n"
           "and navigate interfaces without vision capabilities."));
    basicForm->addRow(m_screenToMarkdownCheckBox);

    // Status label showing Tesseract availability
    m_screenToMarkdownStatusLabel = new QLabel();
    m_screenToMarkdownStatusLabel->setStyleSheet(commentsFontSize + " QLabel { color: #666; }");
    m_screenToMarkdownStatusLabel->setWordWrap(true);
    basicForm->addRow(m_screenToMarkdownStatusLabel);

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

    // ---- Internal signal connections ----
    connect(m_transportCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &McpPage::onTransportModeChanged);
    connect(m_sseBindPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &McpPage::onBindAddressPresetChanged);

    // ---- Stretch ----
    mainLayout->addStretch();

    // ---- Button bar via base class ----
    createButtonBar(mainLayout);

    // ---- Connect setting widgets to markDirty() ----
    connect(m_enableCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });
    connect(m_transportCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_screenToMarkdownCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });
    connect(m_ssePortSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_sseBindPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_sseBindCustomEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_ssePathSseEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_ssePathMessagesEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(m_sseKeepaliveSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_sseSessionTimeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_sseCleanupIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
    connect(m_sseMaxSessionsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]{ checkDirtyState(); });
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

    // Screen to Markdown feature
    m_screenToMarkdownCheckBox->setChecked(s.getMcpScreenToMarkdown());

    // Check Tesseract availability and update status label
    updateTesseractStatus();

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

    captureSnapshot();
    clearDirty();
}

// ============================================================================
// Save values and emit change signal
// ============================================================================

void McpPage::applySettings()
{
    GlobalSetting &s = GlobalSetting::instance();

    s.setMcpEnabled(m_enableCheckBox->isChecked());

    QString transport = m_transportCombo->currentData().toString();
    s.setMcpTransport(transport);

    // Screen to Markdown feature
    s.setMcpScreenToMarkdown(m_screenToMarkdownCheckBox->isChecked());

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

void McpPage::updateTesseractStatus()
{
#ifdef HAVE_TESSERACT
    m_screenToMarkdownStatusLabel->setText(
        tr("✓ Tesseract OCR is available. Screen to Markdown feature is enabled."));
    m_screenToMarkdownCheckBox->setEnabled(true);
#else
    QString installInstructions;
#ifdef Q_OS_LINUX
    installInstructions = tr(
        "To enable this feature, install Tesseract OCR:\n"
        "• Fedora/RHEL: sudo dnf install tesseract tesseract-devel leptonica-devel\n"
        "• Ubuntu/Debian: sudo apt-get install tesseract-ocr libtesseract-dev libleptonica-dev\n"
        "Then rebuild the application.");
#elif defined(Q_OS_WIN)
    installInstructions = tr(
        "To enable this feature, install Tesseract OCR for Windows:\n"
        "• Download from: https://github.com/UB-Mannheim/tesseract/wiki\n"
        "• Install and add to PATH, then rebuild the application.");
#else
    installInstructions = tr("Install Tesseract OCR and rebuild the application to enable this feature.");
#endif
    m_screenToMarkdownStatusLabel->setText(installInstructions);
    m_screenToMarkdownCheckBox->setEnabled(false);
    m_screenToMarkdownCheckBox->setChecked(false);
#endif
}

void McpPage::captureSnapshot()
{
    m_snap_enableChecked = m_enableCheckBox->isChecked();
    m_snap_transportIndex = m_transportCombo->currentIndex();
    m_snap_screenToMarkdownChecked = m_screenToMarkdownCheckBox->isChecked();
    m_snap_ssePort = m_ssePortSpin->value();
    m_snap_sseBindPresetIndex = m_sseBindPresetCombo->currentIndex();
    m_snap_sseBindCustom = m_sseBindCustomEdit->text();
    m_snap_ssePathSse = m_ssePathSseEdit->text();
    m_snap_ssePathMessages = m_ssePathMessagesEdit->text();
    m_snap_sseKeepalive = m_sseKeepaliveSpin->value();
    m_snap_sseSessionTimeout = m_sseSessionTimeoutSpin->value();
    m_snap_sseCleanupInterval = m_sseCleanupIntervalSpin->value();
    m_snap_sseMaxSessions = m_sseMaxSessionsSpin->value();
}

void McpPage::revertToSnapshot()
{
    m_enableCheckBox->setChecked(m_snap_enableChecked);
    m_transportCombo->setCurrentIndex(m_snap_transportIndex);
    m_screenToMarkdownCheckBox->setChecked(m_snap_screenToMarkdownChecked);
    m_ssePortSpin->setValue(m_snap_ssePort);
    m_sseBindPresetCombo->setCurrentIndex(m_snap_sseBindPresetIndex);
    m_sseBindCustomEdit->setText(m_snap_sseBindCustom);
    m_ssePathSseEdit->setText(m_snap_ssePathSse);
    m_ssePathMessagesEdit->setText(m_snap_ssePathMessages);
    m_sseKeepaliveSpin->setValue(m_snap_sseKeepalive);
    m_sseSessionTimeoutSpin->setValue(m_snap_sseSessionTimeout);
    m_sseCleanupIntervalSpin->setValue(m_snap_sseCleanupInterval);
    m_sseMaxSessionsSpin->setValue(m_snap_sseMaxSessions);

    // Trigger UI state updates
    onTransportModeChanged(m_snap_transportIndex);
    onBindAddressPresetChanged(m_snap_sseBindPresetIndex);
}

bool McpPage::valuesMatchSnapshot() const
{
    return m_enableCheckBox->isChecked() == m_snap_enableChecked
        && m_transportCombo->currentIndex() == m_snap_transportIndex
        && m_screenToMarkdownCheckBox->isChecked() == m_snap_screenToMarkdownChecked
        && m_ssePortSpin->value() == m_snap_ssePort
        && m_sseBindPresetCombo->currentIndex() == m_snap_sseBindPresetIndex
        && m_sseBindCustomEdit->text() == m_snap_sseBindCustom
        && m_ssePathSseEdit->text() == m_snap_ssePathSse
        && m_ssePathMessagesEdit->text() == m_snap_ssePathMessages
        && m_sseKeepaliveSpin->value() == m_snap_sseKeepalive
        && m_sseSessionTimeoutSpin->value() == m_snap_sseSessionTimeout
        && m_sseCleanupIntervalSpin->value() == m_snap_sseCleanupInterval
        && m_sseMaxSessionsSpin->value() == m_snap_sseMaxSessions;
}
