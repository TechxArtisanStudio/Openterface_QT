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

#include "HotplugTestDialog.h"
#include <QDateTime>
#include <QMessageBox>

HotplugTestDialog::HotplugTestDialog(QWidget* parent)
    : QDialog(parent)
    , m_wizard(HotplugTestWizard::getInstance())
{
    setupUI();
    setWindowTitle(tr("热插拔恢复测试向导"));
    resize(700, 600);

    // Connect wizard signals
    connect(&m_wizard, &HotplugTestWizard::stepChanged,
            this, &HotplugTestDialog::onStepChanged);
    connect(&m_wizard, &HotplugTestWizard::verificationUpdated,
            this, &HotplugTestDialog::onVerificationUpdated);
    connect(&m_wizard, &HotplugTestWizard::stepCompleted,
            this, &HotplugTestDialog::onStepCompleted);
    connect(&m_wizard, &HotplugTestWizard::testCompleted,
            this, &HotplugTestDialog::onTestCompleted);
}

HotplugTestDialog::~HotplugTestDialog()
{
    m_wizard.stopTest();
}

void HotplugTestDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Step list
    auto* stepGroup = new QGroupBox(tr("测试步骤"));
    auto* stepLayout = new QVBoxLayout(stepGroup);
    m_stepList = new QListWidget();
    m_stepList->setSelectionMode(QAbstractItemView::NoSelection);
    stepLayout->addWidget(m_stepList);
    mainLayout->addWidget(stepGroup);

    // Current step info
    auto* infoGroup = new QGroupBox(tr("当前步骤"));
    auto* infoLayout = new QVBoxLayout(infoGroup);

    m_instructionLabel = new QLabel(tr("点击「开始测试」启动测试流程"));
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setStyleSheet("QLabel { font-weight: bold; padding: 8px; background: #f0f0f0; border-radius: 4px; }");
    infoLayout->addWidget(m_instructionLabel);

    m_statusLabel = new QLabel(tr("状态: 等待开始"));
    infoLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    infoLayout->addWidget(m_progressBar);

    mainLayout->addWidget(infoGroup);

    // Control buttons
    auto* buttonLayout = new QHBoxLayout();
    m_startButton = new QPushButton(tr("开始测试"));
    m_stopButton = new QPushButton(tr("停止测试"));
    m_skipButton = new QPushButton(tr("跳过步骤"));
    m_retryButton = new QPushButton(tr("重试步骤"));
    m_verifyButton = new QPushButton(tr("开始验证"));

    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_verifyButton);
    buttonLayout->addWidget(m_skipButton);
    buttonLayout->addWidget(m_retryButton);
    mainLayout->addLayout(buttonLayout);

    // Log area
    auto* logGroup = new QGroupBox(tr("测试日志"));
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setMaximumHeight(200);
    logLayout->addWidget(m_logText);
    mainLayout->addWidget(logGroup);

    // Button connections
    connect(m_startButton, &QPushButton::clicked, this, &HotplugTestDialog::onStartTestClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &HotplugTestDialog::onStopTestClicked);
    connect(m_skipButton, &QPushButton::clicked, this, &HotplugTestDialog::onSkipStepClicked);
    connect(m_retryButton, &QPushButton::clicked, this, &HotplugTestDialog::onRetryStepClicked);
    connect(m_verifyButton, &QPushButton::clicked, this, &HotplugTestDialog::onManualVerifyClicked);

    // Initial button states
    m_stopButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    m_retryButton->setEnabled(false);
    m_verifyButton->setEnabled(false);

    // Populate step list with all steps from wizard
    for (int i = 0; i < m_wizard.totalStepCount(); i++) {
        auto* item = new QListWidgetItem();
        m_stepList->addItem(item);
    }
}

void HotplugTestDialog::onStartTestClicked()
{
    m_logText->clear();
    appendLog(tr("测试开始..."));

    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_skipButton->setEnabled(true);
    m_retryButton->setEnabled(true);

    m_wizard.startTest();
}

void HotplugTestDialog::onStopTestClicked()
{
    m_wizard.stopTest();
    appendLog(tr("测试已停止"));

    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    m_retryButton->setEnabled(false);
    m_verifyButton->setEnabled(false);

    m_statusLabel->setText(tr("状态: 已停止"));
}

void HotplugTestDialog::onSkipStepClicked()
{
    appendLog(tr("跳过当前步骤..."));
    m_wizard.skipStep();
}

void HotplugTestDialog::onRetryStepClicked()
{
    appendLog(tr("重试当前步骤..."));
    m_wizard.retryStep();
}

void HotplugTestDialog::onManualVerifyClicked()
{
    appendLog(tr("开始手动验证..."));
    m_verifyButton->setEnabled(false);
    m_wizard.startVerification();
}

void HotplugTestDialog::onStepChanged(int index, const HotplugTestStep& step)
{
    updateStepDisplay();

    // Update step list
    for (int i = 0; i < m_stepList->count(); i++) {
        auto* item = m_stepList->item(i);
        if (i == index) {
            item->setText(QString("▶ %1 — %2").arg(i + 1).arg(step.title));
            item->setBackground(QColor("#e3f2fd"));
            item->setFont([item]() { QFont f = item->font(); f.setBold(true); return f; }());
        } else {
            // Keep existing icon/text for completed steps
        }
    }

    // Update instruction
    m_instructionLabel->setText(step.instruction);

    // Update progress
    int progress = (index + 1) * 100 / m_wizard.totalStepCount();
    m_progressBar->setValue(progress);

    m_statusLabel->setText(tr("状态: 步骤 %1/%2 — %3")
        .arg(index + 1).arg(m_wizard.totalStepCount()).arg(step.title));

    appendLog(QString(tr("步骤 %1: %2")).arg(index + 1).arg(step.title));

    // Manual trigger step: enable verify button
    if (step.trigger == HotplugTestStep::TriggerType::Manual) {
        m_verifyButton->setEnabled(true);
    } else {
        m_verifyButton->setEnabled(false);
        appendLog(tr("自动验证模式 — 等待设备状态变化..."));
    }
}

void HotplugTestDialog::onVerificationUpdated(const QString& name, bool passed, bool timedOut, qint64 elapsedMs)
{
    QString status;
    if (passed) {
        status = tr("✅ 通过");
    } else if (timedOut) {
        status = tr("❌ 超时");
    } else {
        status = tr("❌ 失败");
    }

    appendLog(QString("  %1: %2 (%3ms)").arg(name, status).arg(elapsedMs));
}

void HotplugTestDialog::onStepCompleted(int index, bool passed)
{
    // Update step list item
    auto* item = m_stepList->item(index);
    if (item) {
        const HotplugTestStep* step = m_wizard.currentStep();
        // The step might have advanced already, so use the stored step info
        QString title = item->text().mid(2); // Remove the ▶ prefix
        if (passed) {
            item->setText(QString("✅ %1").arg(title));
            item->setBackground(QColor("#e8f5e9"));
        } else {
            item->setText(QString("❌ %1").arg(title));
            item->setBackground(QColor("#ffebee"));
        }
        item->setFont([item]() { QFont f = item->font(); f.setBold(false); return f; }());
    }

    if (passed) {
        appendLog(QString(tr("步骤 %1 完成 ✅")).arg(index + 1));
    } else {
        appendLog(QString(tr("步骤 %1 失败 ❌ — 可以重试或跳过")).arg(index + 1));
    }
}

void HotplugTestDialog::onTestCompleted(const TestReport& report)
{
    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    m_retryButton->setEnabled(false);
    m_verifyButton->setEnabled(false);

    m_progressBar->setValue(100);
    m_statusLabel->setText(tr("状态: 测试完成"));

    appendLog("────────────────────────────");
    appendLog(report.summary());

    if (report.allPassed()) {
        appendLog(tr("🎉 所有测试通过！热插拔恢复功能正常。"));
        QMessageBox::information(this, tr("测试通过"),
            tr("所有测试步骤均已通过！\n\n%1").arg(report.summary()));
    } else {
        int failed = 0;
        for (const auto& s : report.steps) {
            if (!s.passed) failed++;
        }
        appendLog(tr("⚠️ 有 %1 个步骤未通过，请检查日志。").arg(failed));
        QMessageBox::warning(this, tr("测试部分失败"),
            tr("有 %1 个测试步骤未通过。\n\n%2\n\n请查看详细日志。").arg(failed).arg(report.summary()));
    }
}

void HotplugTestDialog::updateStepDisplay()
{
    // Refresh step list items that haven't been marked yet
    for (int i = 0; i < m_stepList->count() && i < m_wizard.totalStepCount(); i++) {
        auto* item = m_stepList->item(i);
        // Only update if item doesn't have a status icon yet
        if (!item->text().startsWith("✅") && !item->text().startsWith("❌") && !item->text().startsWith("▶")) {
            // Find the step title — we need to get it from the wizard or store it
            // For pending steps, just show the number
            item->setText(QString("  %1. ???").arg(i + 1));
        }
    }
}

void HotplugTestDialog::appendLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logText->append(QString("[%1] %2").arg(timestamp, message));
}
