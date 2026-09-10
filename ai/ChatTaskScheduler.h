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

#ifndef CHATTASKSCHEDULER_H
#define CHATTASKSCHEDULER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QDateTime>
#include <QTimer>
#include <QMutex>

/**
 * @brief Represents a scheduled task that can be executed at a specific time.
 */
struct ScheduledTask {
    QString taskId;          // Unique identifier
    QString prompt;          // What to execute (natural language)
    QDateTime scheduledTime; // When to execute
    bool recurring;          // Whether this is a recurring task
    QString cronExpression;  // Cron expression for recurring tasks (if applicable)
    QString status;          // "pending", "running", "completed", "failed", "cancelled"
    QString parentTaskId;    // Link to original task that scheduled this
    QString context;         // Additional context/description
    QDateTime createdAt;     // When this task was scheduled
    int executionCount;      // Number of times this task has executed (for recurring)

    ScheduledTask() : recurring(false), executionCount(0) {}
};

/**
 * @brief Singleton service for managing scheduled tasks.
 *
 * This service allows the AI agent to schedule tasks for future execution,
 * persist them across application restarts, and automatically execute them
 * when their scheduled time arrives.
 */
class ChatTaskScheduler : public QObject
{
    Q_OBJECT

public:
    static ChatTaskScheduler &instance();

    /**
     * @brief Start the scheduler service.
     * This should be called during application initialization.
     */
    void start();

    /**
     * @brief Stop the scheduler service.
     * This should be called during application shutdown.
     */
    void stop();

    /**
     * @brief Schedule a new task for future execution.
     * @param prompt The task prompt to execute when scheduled time arrives
     * @param delayMinutes Optional: delay in minutes from now
     * @param scheduledTime Optional: specific date/time to execute (ISO 8601)
     * @param recurring Optional: whether this is a recurring task (default false)
     * @param cronExpression Optional: cron expression for recurring tasks
     * @param parentTaskId Optional: ID of the task that scheduled this
     * @param context Optional: additional context for the task
     * @return The unique task ID for the scheduled task
     */
    QString scheduleTask(const QString &prompt,
                         int delayMinutes = -1,
                         const QString &scheduledTime = "",
                         bool recurring = false,
                         const QString &cronExpression = "",
                         const QString &parentTaskId = "",
                         const QString &context = "");

    /**
     * @brief Cancel a scheduled task.
     * @param taskId The ID of the task to cancel
     * @return true if task was found and cancelled, false otherwise
     */
    bool cancelTask(const QString &taskId);

    /**
     * @brief List all scheduled tasks.
     * @param statusFilter Optional: filter by status ("pending", "running", etc.)
     * @return List of tasks matching the filter
     */
    QList<ScheduledTask> listTasks(const QString &statusFilter = "") const;

    /**
     * @brief Get a specific task by ID.
     * @param taskId The ID of the task to retrieve
     * @return The task if found, empty task if not found
     */
    ScheduledTask getTask(const QString &taskId) const;

    /**
     * @brief Execute a task immediately (bypassing schedule).
     * @param taskId The ID of the task to execute
     * @return true if task was found and execution started, false otherwise
     */
    bool executeTaskNow(const QString &taskId);

    /**
     * @brief Mark a task as completed or failed.
     * @param taskId The ID of the task to mark
     * @param success true if the task completed successfully, false if it failed
     */
    void markTaskCompleted(const QString &taskId, bool success);

signals:
    /** @brief Emitted when a task is scheduled */
    void taskScheduled(const QString &taskId);

    /** @brief Emitted when a task is cancelled */
    void taskCancelled(const QString &taskId);

    /** @brief Emitted when a task starts execution */
    void taskStarted(const QString &taskId);

    /** @brief Emitted when a task completes execution */
    void taskCompleted(const QString &taskId, bool success);

    /** @brief Emitted when a task fails execution */
    void taskFailed(const QString &taskId, const QString &error);

    /** @brief Emitted when a task is ready to be executed (used by ChatToolExecution) */
    void taskReadyForExecution(const QString &taskId, const QString &prompt);

private:
    explicit ChatTaskScheduler(QObject *parent = nullptr);
    ~ChatTaskScheduler() override;

    // Private implementation details
    QTimer *m_checkTimer;
    QMutex *m_mutex;
    QList<ScheduledTask> m_tasks;
    bool m_running;

    /**
     * @brief Check for tasks that are due for execution.
     * Called by the internal timer.
     */
    void checkForDueTasks();

    /**
     * @brief Parse a scheduled time string or delay into a QDateTime.
     * @param timeStr ISO 8601 datetime string (optional)
     * @param delayMinutes Delay in minutes from now (optional)
     * @return The parsed QDateTime
     */
    QDateTime parseScheduledTime(const QString &timeStr, int delayMinutes);

    /**
     * @brief Validate a cron expression.
     * @param cronExpr The cron expression to validate
     * @return true if valid, false otherwise
     */
    bool isCronExpressionValid(const QString &cronExpr) const;

    /**
     * @brief Calculate the next execution time for a recurring task.
     * @param cronExpr The cron expression
     * @param fromTime The current time
     * @return The next execution time
     */
    QDateTime calculateNextCronExecution(const QString &cronExpr, const QDateTime &fromTime) const;
};

#endif // CHATTASKSCHEDULER_H