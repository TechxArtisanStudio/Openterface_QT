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

#ifndef CHAT_API_CLIENT_H
#define CHAT_API_CLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QString>
#include <functional>
#include "ChatTypes.h"

/**
 * @brief OpenAI-compatible Chat Completions API client.
 *
 * Sends POST requests to {baseURL}/chat/completions and parses responses.
 * Uses QNetworkAccessManager for async HTTP.
 */
class ChatApiClient : public QObject
{
    Q_OBJECT

public:
    static ChatApiClient &instance();
    explicit ChatApiClient(QObject *parent = nullptr);
    ~ChatApiClient() override;

    /**
     * @brief Send a chat completion request.
     * @param baseURL The API base URL (e.g., https://api.openai.com/v1)
     * @param model The model name (e.g., gpt-4o-mini)
     * @param apiKey The API key for authentication
     * @param messages The conversation messages
     * @param enableThinking Optional: enable thinking/reasoning mode
     * @param callback Called with the result or error message
     */
    void sendCompletion(
        const QUrl &baseURL,
        const QString &model,
        const QString &apiKey,
        const QList<ChatApiMessage> &messages,
        std::optional<bool> enableThinking,
        std::function<void(bool success, const ChatCompletionResult &result, const QString &error)> callback
    );

    /**
     * @brief Cancel all pending requests.
     */
    void cancelAll();

signals:
    /**
     * @brief Emitted when a request starts (for logging/tracing).
     */
    void requestStarted(const QString &url, int bodyBytes);

    /**
     * @brief Emitted when a response is received (for logging/tracing).
     */
    void responseReceived(int httpStatus, int bodyBytes);

private:
    QNetworkAccessManager *m_networkManager;

    struct PendingRequest {
        QNetworkReply *reply;
        std::function<void(bool, const ChatCompletionResult &, const QString &)> callback;
    };
    QList<PendingRequest> m_pendingRequests;
};

#endif // CHAT_API_CLIENT_H
