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

#ifndef HOTPLUGTESTWIZARD_H
#define HOTPLUGTESTWIZARD_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QString>
#include <functional>

// A single verification check within a test step
struct TestVerification {
    QString name;                       // e.g. "Serial GET_INFO"
    std::function<bool()> check;        // Returns true if verification passes
    int timeoutMs;                      // Max time to wait
    bool required;                      // Must pass for step to succeed

    // Runtime state
    bool passed = false;
    bool timedOut = false;
    QString errorMessage;
    qint64 elapsedMs = 0;
};

// A guided test step with instruction and verification list
struct HotplugTestStep {
    QString id;
    QString title;
    QString instruction;                // What the user should do
    QString autoVerifyHint;             // Shown while waiting for auto-verification

    enum class TriggerType {
        Manual,          // User clicks "Start Verification"
        AutoOnState      // Triggers when DeviceLifecycleManager state changes
    };
    TriggerType trigger = TriggerType::AutoOnState;

    QList<TestVerification> verifications;

    // Runtime state
    bool completed = false;
    bool passed = false;
    int durationMs = 0;
};

// Result of a single step (for report)
struct StepResult {
    QString stepId;
    QString stepTitle;
    QList<TestVerification> verifications;
    bool passed;
    int durationMs;
};

// Full test report
struct TestReport {
    QDateTime startTime;
    QDateTime endTime;
    QList<StepResult> steps;

    bool allPassed() const {
        for (const auto& s : steps) {
            if (!s.passed) return false;
        }
        return !steps.isEmpty();
    }

    QString summary() const;
};

// HotplugTestWizard: Guided hotplug recovery test framework.
// Runs through 7 test steps, verifying device recovery after physical operations.
class HotplugTestWizard : public QObject {
    Q_OBJECT

public:
    static HotplugTestWizard& getInstance();

    void startTest();
    void stopTest();
    void nextStep();
    void retryStep();
    void skipStep();
    void startVerification();  // For Manual trigger steps

    int currentStepIndex() const { return m_currentStep; }
    const HotplugTestStep* currentStep() const;
    TestReport generateReport() const;
    int totalStepCount() const { return m_steps.size(); }

signals:
    void stepChanged(int index, const HotplugTestStep& step);
    void verificationUpdated(const QString& name, bool passed, bool timedOut, qint64 elapsedMs);
    void stepCompleted(int index, bool passed);
    void testCompleted(const TestReport& report);

private:
    explicit HotplugTestWizard(QObject* parent = nullptr);

    QList<HotplugTestStep> m_steps;
    int m_currentStep = -1;
    QTimer* m_verificationTimer = nullptr;
    QDateTime m_testStartTime;
    bool m_running = false;

    void buildTestPlan();
    void runVerifications();
    void checkVerificationProgress();
    void completeCurrentStep(bool passed);

    // Listen to DeviceLifecycleManager for auto-trigger
    void onSessionStateChanged(const QString& key, int state);
};

#endif // HOTPLUGTESTWIZARD_H
