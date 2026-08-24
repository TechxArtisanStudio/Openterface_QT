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

#ifndef CHAT_PERSISTENCE_H
#define CHAT_PERSISTENCE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include "ChatTypes.h"

/**
 * @brief Persists chat history, plans, and traces to disk as JSON.
 */
class ChatPersistence : public QObject
{
    Q_OBJECT

public:
    static ChatPersistence &instance();

    /// Save messages, plan, and traces to disk
    void saveHistory(
        const QList<ChatMessage> &messages,
        const ChatExecutionPlan &plan,
        bool hasPlan,
        const QList<ChatTaskTraceEntry> &plannerTraces
    );

    /// Load messages, plan, and traces from disk
    bool loadHistory(
        QList<ChatMessage> &messages,
        ChatExecutionPlan &plan,
        bool &hasPlan,
        QList<ChatTaskTraceEntry> &plannerTraces
    );

    /// Get the path to the history file
    QString historyFilePath() const;

private:
    explicit ChatPersistence(QObject *parent = nullptr);
};

#endif // CHAT_PERSISTENCE_H
