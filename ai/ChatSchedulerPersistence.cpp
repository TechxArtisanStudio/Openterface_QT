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

#include "ChatSchedulerPersistence.h"
#include "log/opflogging.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_scheduler_persistence)
OPF_LOGGING_CATEGORY(log_scheduler_persistence, "opf.scheduler.persistence")

ChatSchedulerPersistence::ChatSchedulerPersistence(QObject *parent)
    : QObject(parent)
{
}

ChatSchedulerPersistence::~ChatSchedulerPersistence()
{
}

ChatSchedulerPersistence &ChatSchedulerPersistence::instance()
{
    static ChatSchedulerPersistence inst;
    return inst;
}

QString ChatSchedulerPersistence::filePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath("scheduled_tasks.json");
}

bool ChatSchedulerPersistence::saveTasks(const QList<ScheduledTask> &tasks)
{
    QJsonObject root;
    QJsonArray tasksArray;

    for (const auto &task : tasks) {
        QJsonObject taskObj;
        taskObj["taskId"] = task.taskId;
        taskObj["prompt"] = task.prompt;
        taskObj["scheduledTime"] = task.scheduledTime.toString(Qt::ISODateWithMs);
        taskObj["recurring"] = task.recurring;
        taskObj["cronExpression"] = task.cronExpression;
        taskObj["status"] = task.status;
        taskObj["parentTaskId"] = task.parentTaskId;
        taskObj["context"] = task.context;
        taskObj["createdAt"] = task.createdAt.toString(Qt::ISODateWithMs);
        taskObj["executionCount"] = task.executionCount;

        tasksArray.append(taskObj);
    }

    root["tasks"] = tasksArray;
    root["version"] = 1; // For future compatibility
    root["lastSaved"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

    QJsonDocument doc(root);
    QString path = filePath();

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << doc.toJson(QJsonDocument::Indented);
        file.close();

        qCDebug(log_scheduler_persistence) << "Saved" << tasks.size() << "scheduled tasks to" << path;
        return true;
    } else {
        qCWarning(log_scheduler_persistence) << "Failed to save scheduled tasks to" << path
                                             << "error:" << file.errorString();
        return false;
    }
}

QList<ScheduledTask> ChatSchedulerPersistence::loadTasks() const
{
    QList<ScheduledTask> tasks;

    QString path = filePath();
    QFile file(path);

    if (!file.exists()) {
        qCDebug(log_scheduler_persistence) << "No scheduled tasks file found at" << path;
        return tasks;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(log_scheduler_persistence) << "Failed to open scheduled tasks file:" << path
                                             << "error:" << file.errorString();
        return tasks;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    if (jsonData.isEmpty()) {
        qCWarning(log_scheduler_persistence) << "Scheduled tasks file is empty:" << path;
        return tasks;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(log_scheduler_persistence) << "Failed to parse scheduled tasks JSON:"
                                             << parseError.errorString()
                                             << "at offset:" << parseError.offset;
        return tasks;
    }

    if (!doc.isObject()) {
        qCWarning(log_scheduler_persistence) << "Scheduled tasks JSON is not an object";
        return tasks;
    }

    QJsonObject root = doc.object();

    // Check version for compatibility
    int version = root["version"].toInt(1);
    if (version > 1) {
        qCWarning(log_scheduler_persistence) << "Scheduled tasks file version" << version
                                             << "is newer than supported (max: 1)";
    }

    QJsonArray tasksArray = root["tasks"].toArray();
    if (tasksArray.isEmpty()) {
        qCDebug(log_scheduler_persistence) << "No tasks found in scheduled tasks file";
        return tasks;
    }

    for (const QJsonValue &taskValue : tasksArray) {
        if (!taskValue.isObject()) {
            qCWarning(log_scheduler_persistence) << "Invalid task entry (not an object), skipping";
            continue;
        }

        QJsonObject taskObj = taskValue.toObject();

        ScheduledTask task;
        task.taskId = taskObj["taskId"].toString();
        task.prompt = taskObj["prompt"].toString();
        task.scheduledTime = QDateTime::fromString(taskObj["scheduledTime"].toString(), Qt::ISODateWithMs);
        if (!task.scheduledTime.isValid()) {
            task.scheduledTime = QDateTime::fromString(taskObj["scheduledTime"].toString(), Qt::ISODate);
        }
        task.recurring = taskObj["recurring"].toBool();
        task.cronExpression = taskObj["cronExpression"].toString();
        task.status = taskObj["status"].toString();
        task.parentTaskId = taskObj["parentTaskId"].toString();
        task.context = taskObj["context"].toString();
        task.createdAt = QDateTime::fromString(taskObj["createdAt"].toString(), Qt::ISODateWithMs);
        if (!task.createdAt.isValid()) {
            task.createdAt = QDateTime::fromString(taskObj["createdAt"].toString(), Qt::ISODate);
        }
        task.executionCount = taskObj["executionCount"].toInt(0);

        // Validate required fields
        if (task.taskId.isEmpty() || task.prompt.isEmpty()) {
            qCWarning(log_scheduler_persistence) << "Skipping task with missing required fields (taskId or prompt)";
            continue;
        }

        // Set default status if missing
        if (task.status.isEmpty()) {
            task.status = "pending";
        }

        // Set default createdAt if missing
        if (!task.createdAt.isValid()) {
            task.createdAt = QDateTime::currentDateTime();
        }

        tasks.append(task);
    }

    qCDebug(log_scheduler_persistence) << "Loaded" << tasks.size() << "scheduled tasks from" << path;
    return tasks;
}