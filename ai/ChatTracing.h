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

#ifndef CHAT_TRACING_H
#define CHAT_TRACING_H

#include <QObject>
#include <QString>
#include <QList>
#include <QUuid>
#include "ChatTypes.h"

/**
 * @brief Logs AI request/response traces for debugging.
 */
class ChatTracing : public QObject
{
    Q_OBJECT

public:
    static ChatTracing &instance();

    /// Append a general AI trace entry
    void appendAITrace(const QString &title, const QString &body);

    /// Append a task step trace
    void appendTaskStepTrace(const QUuid &taskID, const QString &title,
                             const QString &body, const QString &imageFilePath = QString());

    /// Append a planner trace
    void appendPlannerTrace(const QString &title, const QString &body,
                            const QString &imageFilePath = QString());

    /// Get the trace file path
    QString traceFilePath() const;

    /// Read the trace file contents
    QString readTraceLog() const;

    /// Clear (delete) the trace log file
    void clearTraceLog();

    /// Format conversation messages for human-readable logging
    QString readableTraceParts(const QList<ChatApiMessage> &messages) const;

private:
    explicit ChatTracing(QObject *parent = nullptr);
    void appendToFile(const QString &section);
};

#endif // CHAT_TRACING_H
