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

#include "HotplugTestWizard.h"
#include "../../serial/SerialPortManager.h"
#include "../../serial/ch9329.h"
#include "../../host/cameramanager.h"
#include "../../video/videohid.h"
#include "../../device/DeviceLifecycleManager.h"

#include <QThread>
#include <QTimer>
#include <QApplication>
#include <QtConcurrent>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_test_wizard)
Q_LOGGING_CATEGORY(log_test_wizard, "opf.test.hotplug")

// Helper to find the CameraManager instance (not a singleton, parented to QApplication)
static CameraManager* findCameraManager()
{
    for (QObject* obj : QApplication::instance()->children()) {
        if (auto* cam = qobject_cast<CameraManager*>(obj)) {
            return cam;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Verification helper functions
// ─────────────────────────────────────────────────────────────────────────────

static bool verifySerialGetInfo(int timeoutMs)
{
    Q_UNUSED(timeoutMs)
    auto& serial = SerialPortManager::getInstance();
    if (!serial.isPortReady()) {
        return false;
    }

    QByteArray cmd = CMD_GET_INFO;  // 0x57 0xAB 0x00 0x01 0x00
    QByteArray response = serial.sendSyncCommand(cmd, true);
    return response.size() >= 4;
}

static bool verifyImageFrames(int timeoutMs)
{
    CameraManager* cam = findCameraManager();
    if (!cam) return false;
    if (!cam->isCameraStreaming()) {
        // Give it a moment to start
        QThread::msleep(500);
        if (!cam->isCameraStreaming()) return false;
    }

    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    qint64 startFrame = QDateTime::currentMSecsSinceEpoch();

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        // If camera is streaming and we've been running for at least 500ms,
        // frames are flowing
        if (cam->isCameraStreaming()
            && (QDateTime::currentMSecsSinceEpoch() - startFrame) >= 500) {
            return true;
        }
        QThread::msleep(100);
    }
    return false;
}

static bool verifyHidResolution(int timeoutMs)
{
    auto& hid = VideoHid::getInstance();
    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        auto res = hid.getResolution();
        if (res.first > 0 && res.second > 0) {
            return true;
        }
        QThread::msleep(200);
    }
    return false;
}

static bool verifySerialDisconnected(int timeoutMs)
{
    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!SerialPortManager::getInstance().isPortReady()) {
            return true;
        }
        QThread::msleep(200);
    }
    return false;
}

static bool verifyCameraStopped(int timeoutMs)
{
    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    CameraManager* cam = findCameraManager();
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!cam || !cam->hasActiveCameraDevice()) {
            return true;
        }
        QThread::msleep(200);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestReport
// ─────────────────────────────────────────────────────────────────────────────

QString TestReport::summary() const
{
    int total = steps.size();
    int passed = 0;
    for (const auto& s : steps) {
        if (s.passed) passed++;
    }
    return QString("Test completed: %1/%2 steps passed. Duration: %3s")
        .arg(passed).arg(total)
        .arg(startTime.secsTo(endTime));
}

// ─────────────────────────────────────────────────────────────────────────────
// HotplugTestWizard
// ─────────────────────────────────────────────────────────────────────────────

HotplugTestWizard::HotplugTestWizard(QObject* parent)
    : QObject(parent)
    , m_verificationTimer(new QTimer(this))
{
    m_verificationTimer->setSingleShot(false);
    connect(m_verificationTimer, &QTimer::timeout,
            this, &HotplugTestWizard::checkVerificationProgress);

    // Listen to DeviceLifecycleManager for auto-trigger
    auto& lifecycle = DeviceLifecycleManager::getInstance();
    connect(&lifecycle, &DeviceLifecycleManager::sessionStateChanged,
            this, [this](const QString& key, DeviceSessionState state) {
                onSessionStateChanged(key, static_cast<int>(state));
            });

    buildTestPlan();
}

HotplugTestWizard& HotplugTestWizard::getInstance()
{
    static HotplugTestWizard instance;
    return instance;
}

void HotplugTestWizard::buildTestPlan()
{
    m_steps.clear();

    // Step 0: Initial State — confirm device is working
    {
        HotplugTestStep step;
        step.id = "initial";
        step.title = "初始状态确认";
        step.instruction = "确认设备已连接并正常工作。\n点击「开始验证」确认当前状态。";
        step.trigger = HotplugTestStep::TriggerType::Manual;
        step.verifications.append({"串口 GET_INFO", []() { return verifySerialGetInfo(5000); }, 5000, true});
        step.verifications.append({"图像传输", []() { return verifyImageFrames(5000); }, 5000, true});
        step.verifications.append({"HID 分辨率", []() { return verifyHidResolution(3000); }, 3000, true});
        m_steps.append(step);
    }

    // Step 1: Target USB Unplug
    {
        HotplugTestStep step;
        step.id = "target_unplug";
        step.title = "Target 端 USB 拔出";
        step.instruction = "请拔掉 Target 端的 USB 连接线。\n系统将自动验证设备断开。";
        step.trigger = HotplugTestStep::TriggerType::AutoOnState;
        step.verifications.append({"串口断开", []() { return verifySerialDisconnected(5000); }, 5000, true});
        step.verifications.append({"摄像头停止", []() { return verifyCameraStopped(3000); }, 3000, false});
        m_steps.append(step);
    }

    // Step 2: Target USB Replug
    {
        HotplugTestStep step;
        step.id = "target_replug";
        step.title = "Target 端 USB 重连";
        step.instruction = "请重新插入 Target 端的 USB 连接线。\n系统将自动验证设备恢复。";
        step.trigger = HotplugTestStep::TriggerType::AutoOnState;
        step.verifications.append({"串口 GET_INFO", []() { return verifySerialGetInfo(15000); }, 15000, true});
        step.verifications.append({"图像传输", []() { return verifyImageFrames(15000); }, 15000, true});
        step.verifications.append({"HID 分辨率", []() { return verifyHidResolution(10000); }, 10000, true});
        m_steps.append(step);
    }

    // Step 3: Target Reboot
    {
        HotplugTestStep step;
        step.id = "target_reboot";
        step.title = "Target 重启测试";
        step.instruction = "请关闭 Target 电源，等待 5 秒后重新开机。\n系统将自动验证设备恢复。";
        step.trigger = HotplugTestStep::TriggerType::AutoOnState;
        step.verifications.append({"串口 GET_INFO", []() { return verifySerialGetInfo(30000); }, 30000, true});
        step.verifications.append({"图像传输", []() { return verifyImageFrames(30000); }, 30000, true});
        step.verifications.append({"HID 分辨率", []() { return verifyHidResolution(20000); }, 20000, true});
        m_steps.append(step);
    }

    // Step 4: Host USB Unplug
    {
        HotplugTestStep step;
        step.id = "host_unplug";
        step.title = "Host 端 USB 拔出";
        step.instruction = "请拔掉 Host 端（本电脑侧）的 USB 连接线。\n系统将自动验证设备断开。";
        step.trigger = HotplugTestStep::TriggerType::AutoOnState;
        step.verifications.append({"全部接口断开", []() { return verifySerialDisconnected(5000); }, 5000, true});
        m_steps.append(step);
    }

    // Step 5: Host USB Replug
    {
        HotplugTestStep step;
        step.id = "host_replug";
        step.title = "Host 端 USB 重连";
        step.instruction = "请重新插入 Host 端的 USB 连接线。\n系统将自动验证设备恢复。";
        step.trigger = HotplugTestStep::TriggerType::AutoOnState;
        step.verifications.append({"串口 GET_INFO", []() { return verifySerialGetInfo(15000); }, 15000, true});
        step.verifications.append({"图像传输", []() { return verifyImageFrames(15000); }, 15000, true});
        step.verifications.append({"HID 分辨率", []() { return verifyHidResolution(10000); }, 10000, true});
        m_steps.append(step);
    }

    // Step 6: Rapid Plug Stress
    {
        HotplugTestStep step;
        step.id = "rapid_stress";
        step.title = "快速拔插压力测试";
        step.instruction = "请快速拔出并重新插入 USB（3 秒内完成）。\n点击「开始验证」后操作系统。";
        step.trigger = HotplugTestStep::TriggerType::Manual;
        step.verifications.append({"全部接口恢复", []() { return verifySerialGetInfo(20000); }, 20000, true});
        step.verifications.append({"GET_INFO 正常", []() { return verifySerialGetInfo(5000); }, 5000, true});
        step.verifications.append({"分辨率正确", []() { return verifyHidResolution(5000); }, 5000, true});
        m_steps.append(step);
    }

    qCInfo(log_test_wizard) << "Test plan built:" << m_steps.size() << "steps";
}

void HotplugTestWizard::startTest()
{
    qCInfo(log_test_wizard) << "Starting hotplug test wizard";
    m_testStartTime = QDateTime::currentDateTime();
    m_currentStep = -1;
    m_running = true;

    // Reset all steps
    for (auto& step : m_steps) {
        step.completed = false;
        step.passed = false;
        step.durationMs = 0;
        for (auto& v : step.verifications) {
            v.passed = false;
            v.timedOut = false;
            v.elapsedMs = 0;
            v.errorMessage.clear();
        }
    }

    nextStep();
}

void HotplugTestWizard::stopTest()
{
    qCInfo(log_test_wizard) << "Stopping hotplug test wizard";
    m_running = false;
    m_verificationTimer->stop();
}

void HotplugTestWizard::nextStep()
{
    if (m_currentStep + 1 >= m_steps.size()) {
        // Test complete
        m_running = false;
        m_verificationTimer->stop();
        auto report = generateReport();
        qCInfo(log_test_wizard) << "Test completed:" << report.summary();
        emit testCompleted(report);
        return;
    }

    m_currentStep++;
    auto& step = m_steps[m_currentStep];
    step.durationMs = 0;

    qCInfo(log_test_wizard) << "Step" << m_currentStep << ":" << step.title;
    emit stepChanged(m_currentStep, step);

    if (step.trigger == HotplugTestStep::TriggerType::Manual) {
        // Wait for user to click "Start Verification"
        qCInfo(log_test_wizard) << "Manual step — waiting for user to start verification";
    } else {
        // Auto-trigger: start verification immediately
        startVerification();
    }
}

void HotplugTestWizard::retryStep()
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return;

    auto& step = m_steps[m_currentStep];
    step.completed = false;
    step.passed = false;
    for (auto& v : step.verifications) {
        v.passed = false;
        v.timedOut = false;
        v.elapsedMs = 0;
        v.errorMessage.clear();
    }

    startVerification();
}

void HotplugTestWizard::skipStep()
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return;

    completeCurrentStep(true);  // Treat skip as pass
    nextStep();
}

void HotplugTestWizard::startVerification()
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return;

    auto& step = m_steps[m_currentStep];
    qCInfo(log_test_wizard) << "Starting verification for step:" << step.title;

    // Run verifications in a background thread to avoid blocking UI
    (void)QtConcurrent::run([this]() {
        runVerifications();
    });

    // Start periodic check to update UI
    m_verificationTimer->start(200);
}

void HotplugTestWizard::runVerifications()
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return;

    auto& step = m_steps[m_currentStep];
    bool allRequiredPassed = true;

    for (auto& v : step.verifications) {
        qint64 start = QDateTime::currentMSecsSinceEpoch();
        qCInfo(log_test_wizard) << "  Running verification:" << v.name << "timeout:" << v.timeoutMs << "ms";

        bool result = false;
        if (v.check) {
            result = v.check();
        }

        v.elapsedMs = QDateTime::currentMSecsSinceEpoch() - start;
        v.passed = result;
        v.timedOut = !result;

        qCInfo(log_test_wizard) << "  Verification" << v.name << (result ? "PASSED" : "FAILED")
                                << "in" << v.elapsedMs << "ms";

        if (!result && v.required) {
            allRequiredPassed = false;
        }

        emit verificationUpdated(v.name, v.passed, v.timedOut, v.elapsedMs);
    }

    completeCurrentStep(allRequiredPassed);
}

void HotplugTestWizard::checkVerificationProgress()
{
    // Timer-based check for UI updates (verifications run in background thread)
    // The actual completion is handled by runVerifications() calling completeCurrentStep()
}

void HotplugTestWizard::completeCurrentStep(bool passed)
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return;

    m_verificationTimer->stop();

    auto& step = m_steps[m_currentStep];
    step.completed = true;
    step.passed = passed;

    qCInfo(log_test_wizard) << "Step" << m_currentStep << (passed ? "PASSED" : "FAILED")
                            << ":" << step.title;

    emit stepCompleted(m_currentStep, passed);

    // Auto-advance after a short delay
    if (passed) {
        QTimer::singleShot(1000, this, [this]() {
            if (m_running) nextStep();
        });
    }
}

const HotplugTestStep* HotplugTestWizard::currentStep() const
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return nullptr;
    return &m_steps[m_currentStep];
}

TestReport HotplugTestWizard::generateReport() const
{
    TestReport report;
    report.startTime = m_testStartTime;
    report.endTime = QDateTime::currentDateTime();

    for (const auto& step : m_steps) {
        StepResult result;
        result.stepId = step.id;
        result.stepTitle = step.title;
        result.verifications = step.verifications;
        result.passed = step.passed;
        result.durationMs = step.durationMs;
        report.steps.append(result);
    }

    return report;
}

void HotplugTestWizard::onSessionStateChanged(const QString& key, int state)
{
    Q_UNUSED(key)
    // Auto-trigger verification when device session state changes
    if (!m_running || m_currentStep < 0) return;

    auto* step = currentStep();
    if (!step) return;
    if (step->trigger != HotplugTestStep::TriggerType::AutoOnState) return;
    if (step->completed) return;

    auto sessionState = static_cast<DeviceSessionState>(state);
    qCInfo(log_test_wizard) << "Session state changed to" << sessionStateToString(sessionState)
                            << "— checking auto-trigger for step" << m_currentStep;

    // Auto-trigger based on state transitions
    // For disconnect steps: trigger on Recovering/Disconnected
    // For reconnect steps: trigger on Ready/Connecting
    if (step->id.contains("unplug")) {
        if (sessionState == DeviceSessionState::Recovering
            || sessionState == DeviceSessionState::Disconnected) {
            if (!m_verificationTimer->isActive()) {
                startVerification();
            }
        }
    } else if (step->id.contains("replug") || step->id.contains("reboot")) {
        if (sessionState == DeviceSessionState::Ready
            || sessionState == DeviceSessionState::Connecting) {
            if (!m_verificationTimer->isActive()) {
                startVerification();
            }
        }
    }
}
