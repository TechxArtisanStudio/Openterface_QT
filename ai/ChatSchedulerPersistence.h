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

#ifndef CHATSCHEDULERPERSISTENCE_H
#define CHATSCHEDULERPERSISTENCE_H

#include <QObject>
#include <QString>
#include <QList>
#include "ChatTaskScheduler.h"

/**
 * @brief Handles persistence of scheduled tasks to disk.
 *
 * This class saves and loads scheduled tasks to/from a JSON file,
 * allowing tasks to survive application restarts.
 */
class ChatSchedulerPersistence : public QObject
{
    Q_OBJECT

public:
    static ChatSchedulerPersistence &instance();

    /**
     * @brief Save the list of scheduled tasks to disk.
     * @param tasks The list of tasks to save
     * @return true if successful, false otherwise
     */
    bool saveTasks(const QList<ScheduledTask> &tasks);

    /**
     * @brief Load the list of scheduled tasks from disk.
     * @return List of loaded tasks (empty list if none found or error)
     */
    QList<ScheduledTask> loadTasks() const;

    /**
     * @brief Get the file path where tasks are stored.
     * @return Full path to the tasks JSON file
     */
    QString filePath() const;

private:
    explicit ChatSchedulerPersistence(QObject *parent = nullptr);
    ~ChatSchedulerPersistence() override;
};

#endif // CHATSCHEDULERPERSISTENCE_H