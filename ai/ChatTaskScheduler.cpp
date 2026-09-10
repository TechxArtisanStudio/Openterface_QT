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

#include "ChatTaskScheduler.h"
#include "ChatSchedulerPersistence.h"
#include "log/opflogging.h"
#include <QUuid>
#include <QDateTime>
#include <QTimer>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QDebug>

Q_DECLARE_LOGGING_CATEGORY(log_task_scheduler)
OPF_LOGGING_CATEGORY(log_task_scheduler, "opf.task.scheduler")

ChatTaskScheduler &ChatTaskScheduler::instance()
{
    static ChatTaskScheduler inst;
    return inst;
}

ChatTaskScheduler::ChatTaskScheduler(QObject *parent)
    : QObject(parent)
    , m_checkTimer(new QTimer(this))
    , m_mutex(new QMutex())
    , m_running(false)
{
    // Set up the timer to check for due tasks every 10 seconds
    m_checkTimer->setInterval(10000);
    m_checkTimer->setSingleShot(false);
    connect(m_checkTimer, &QTimer::timeout, this, &ChatTaskScheduler::checkForDueTasks);
}

ChatTaskScheduler::~ChatTaskScheduler()
{
    stop();
    delete m_mutex;
}

void ChatTaskScheduler::start()
{
    if (m_running) return;

    m_running = true;

    // Load persisted tasks
    m_tasks = ChatSchedulerPersistence::instance().loadTasks();

    // Start the timer
    m_checkTimer->start();

    qCDebug(log_task_scheduler) << "Task scheduler started with" << m_tasks.size() << "tasks";
}

void ChatTaskScheduler::stop()
{
    if (!m_running) return;

    m_running = false;
    m_checkTimer->stop();

    // Save tasks before stopping
    ChatSchedulerPersistence::instance().saveTasks(m_tasks);

    qCDebug(log_task_scheduler) << "Task scheduler stopped";
}

QString ChatTaskScheduler::scheduleTask(const QString &prompt,
                                        int delayMinutes,
                                        const QString &scheduledTime,
                                        bool recurring,
                                        const QString &cronExpression,
                                        const QString &parentTaskId,
                                        const QString &context)
{
    if (prompt.isEmpty()) {
        qCWarning(log_task_scheduler) << "Cannot schedule task with empty prompt";
        return "";
    }

    // Validate cron expression if provided
    if (!cronExpression.isEmpty() && !isCronExpressionValid(cronExpression)) {
        qCWarning(log_task_scheduler) << "Invalid cron expression:" << cronExpression;
        return "";
    }

    QMutexLocker locker(m_mutex);

    ScheduledTask task;
    task.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.prompt = prompt;
    task.scheduledTime = parseScheduledTime(scheduledTime, delayMinutes);
    task.recurring = recurring;
    task.cronExpression = cronExpression;
    task.status = "pending";
    task.parentTaskId = parentTaskId;
    task.context = context;
    task.createdAt = QDateTime::currentDateTime();
    task.executionCount = 0;

    m_tasks.append(task);

    // Save to persistence
    ChatSchedulerPersistence::instance().saveTasks(m_tasks);

    qCDebug(log_task_scheduler) << "Task scheduled:" << task.taskId
                                << "prompt:" << prompt.left(50)
                                << "time:" << task.scheduledTime.toString(Qt::ISODate)
                                << "recurring:" << recurring;

    emit taskScheduled(task.taskId);

    return task.taskId;
}

bool ChatTaskScheduler::cancelTask(const QString &taskId)
{
    if (taskId.isEmpty()) {
        qCWarning(log_task_scheduler) << "Cannot cancel task with empty ID";
        return false;
    }

    QMutexLocker locker(m_mutex);

    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].taskId == taskId) {
            // Only allow cancelling pending or running tasks
            if (m_tasks[i].status == "completed" ||
                m_tasks[i].status == "failed" ||
                m_tasks[i].status == "cancelled") {
                qCWarning(log_task_scheduler) << "Cannot cancel task with status:"
                                             << m_tasks[i].status;
                return false;
            }

            m_tasks[i].status = "cancelled";
            ChatSchedulerPersistence::instance().saveTasks(m_tasks);

            qCDebug(log_task_scheduler) << "Task cancelled:" << taskId;

            emit taskCancelled(taskId);
            return true;
        }
    }

    qCWarning(log_task_scheduler) << "Task not found for cancellation:" << taskId;
    return false;
}

QList<ScheduledTask> ChatTaskScheduler::listTasks(const QString &statusFilter) const
{
    QMutexLocker locker(m_mutex);

    if (statusFilter.isEmpty()) {
        return m_tasks;
    }

    QList<ScheduledTask> filtered;
    for (const ScheduledTask &task : m_tasks) {
        if (task.status == statusFilter) {
            filtered.append(task);
        }
    }
    return filtered;
}

ScheduledTask ChatTaskScheduler::getTask(const QString &taskId) const
{
    QMutexLocker locker(m_mutex);

    for (const ScheduledTask &task : m_tasks) {
        if (task.taskId == taskId) {
            return task;
        }
    }
    return ScheduledTask(); // Return empty task
}

bool ChatTaskScheduler::executeTaskNow(const QString &taskId)
{
    if (taskId.isEmpty()) {
        qCWarning(log_task_scheduler) << "Cannot execute task with empty ID";
        return false;
    }

    QMutexLocker locker(m_mutex);

    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].taskId == taskId) {
            // Change status to running and trigger execution
            m_tasks[i].status = "running";
            m_tasks[i].executionCount++;
            ChatSchedulerPersistence::instance().saveTasks(m_tasks);

            qCDebug(log_task_scheduler) << "Task executed now:" << taskId;

            emit taskStarted(taskId);

            // Emit signal that task is ready for execution
            // The ChatToolExecution will handle connecting this to actual execution
            emit taskReadyForExecution(taskId, m_tasks[i].prompt);

            return true;
        }
    }

    qCWarning(log_task_scheduler) << "Task not found for execution:" << taskId;
    return false;
}

void ChatTaskScheduler::checkForDueTasks()
{
    if (!m_running) return;

    QMutexLocker locker(m_mutex);
    QDateTime now = QDateTime::currentDateTime();
    bool tasksModified = false;

    for (int i = 0; i < m_tasks.size(); ++i) {
        ScheduledTask &task = m_tasks[i];
        if (task.status != "pending") continue;

        bool shouldExecute = false;

        if (!task.cronExpression.isEmpty() && task.recurring) {
            // Check cron-based recurring task
            if (task.executionCount == 0) {
                // First execution - check if time has come
                if (now >= task.scheduledTime) {
                    shouldExecute = true;
                }
            } else {
                // Subsequent executions - calculate next time based on cron
                QDateTime nextTime = calculateNextCronExecution(task.cronExpression,
                                task.scheduledTime.addSecs(task.executionCount * 60));
                if (now >= nextTime) {
                    shouldExecute = true;
                }
            }
        } else {
            // One-time task or simple recurring
            if (now >= task.scheduledTime) {
                shouldExecute = true;
            }
        }

        if (shouldExecute) {
            task.status = "running";
            task.executionCount++;
            tasksModified = true;

            qCDebug(log_task_scheduler) << "Task due for execution:" << task.taskId
                                        << "prompt:" << task.prompt.left(50)
                                        << "execution:" << task.executionCount;

            // Emit signal that task is ready for execution
            emit taskReadyForExecution(task.taskId, task.prompt);
            emit taskStarted(task.taskId);
        }
    }

    if (tasksModified) {
        // Save updated task list
        ChatSchedulerPersistence::instance().saveTasks(m_tasks);
    }
}

void ChatTaskScheduler::markTaskCompleted(const QString &taskId, bool success)
{
    QMutexLocker locker(m_mutex);

    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].taskId == taskId) {
            m_tasks[i].status = success ? "completed" : "failed";
            ChatSchedulerPersistence::instance().saveTasks(m_tasks);

            qCDebug(log_task_scheduler) << "Task marked as" << m_tasks[i].status << ":" << taskId;

            if (success) {
                emit taskCompleted(taskId, true);
            } else {
                emit taskFailed(taskId, "Execution failed");
            }
            return;
        }
    }
}

QDateTime ChatTaskScheduler::parseScheduledTime(const QString &timeStr, int delayMinutes)
{
    if (!timeStr.isEmpty()) {
        // Try to parse as ISO 8601 datetime
        QDateTime dt = QDateTime::fromString(timeStr, Qt::ISODateWithMs);
        if (dt.isValid()) {
            return dt;
        }
        // Try without milliseconds
        dt = QDateTime::fromString(timeStr, Qt::ISODate);
        if (dt.isValid()) {
            return dt;
        }
    }

    if (delayMinutes >= 0) {
        return QDateTime::currentDateTime().addSecs(delayMinutes * 60);
    }

    // Default: schedule for 1 minute from now
    return QDateTime::currentDateTime().addSecs(60);
}

bool ChatTaskScheduler::isCronExpressionValid(const QString &cronExpr) const
{
    // Simple validation: 5 fields separated by spaces
    QStringList parts = cronExpr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() != 5) {
        return false;
    }

    // Basic validation for each field
    // We'll accept *, numbers, and common patterns like */5, 0-5, etc.
    QRegularExpression validPattern("^(\\*|\\*/\\d+|\\d+(-\\d+)?(,\\d+(-\\d+)?)*|\\d+(-\\d+)?/\\d+)$");

    for (const QString &part : parts) {
        if (!validPattern.match(part).hasMatch()) {
            return false;
        }
    }

    return true;
}

QDateTime ChatTaskScheduler::calculateNextCronExecution(const QString &cronExpr, const QDateTime &fromTime) const
{
    // Simple cron implementation for common patterns
    // A full cron parser would be more complex, but this handles basic cases
    QStringList parts = cronExpr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() != 5) {
        return fromTime.addSecs(3600); // fallback to 1 hour
    }

    int intervalMinutes = 60; // default to 1 hour

    // Check for simple interval patterns
    if (parts[0].startsWith("*/")) {
        bool ok;
        int minutes = parts[0].mid(2).toInt(&ok);
        if (ok && minutes > 0 && minutes < 60) {
            intervalMinutes = minutes;
        }
    } else if (parts[0] == "0" && parts[1] == "*") {
        intervalMinutes = 60; // hourly
    } else if (parts[0] == "0" && parts[1] == "0") {
        intervalMinutes = 24 * 60; // daily
    } else if (parts[4] != "*" && parts[0] == "0" && parts[1] == "0") {
        intervalMinutes = 7 * 24 * 60; // weekly
    }

    return fromTime.addSecs(intervalMinutes * 60);
}
