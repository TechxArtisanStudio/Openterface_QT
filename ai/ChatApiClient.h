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
#include <QNetworkReply>
#include <QUrl>
#include <QString>
#include <atomic>
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

    // ------------------------------------------------------------------
    // Rate-limit (HTTP 429) awareness
    //
    // When the API responds with 429 we don't just report the error — we
    // record a cooldown window (from Retry-After when present, otherwise an
    // exponential backoff that grows with each consecutive 429) so callers
    // can pause before issuing further requests instead of hammering the
    // upstream. Any successful 2xx response clears the state.
    // ------------------------------------------------------------------

    /** True while we are inside a rate-limit cooldown window. Thread-safe. */
    bool isRateLimited() const;

    /** Milliseconds remaining in the current cooldown (0 if not limited). */
    qint64 rateLimitRemainingMs() const;

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
    // Called from the finished handler whenever we observe an HTTP 429.
    void recordRateLimit(int httpStatus, const QNetworkReply *reply);
    // Called from the finished handler on any successful 2xx response.
    void clearRateLimit();

    QNetworkAccessManager *m_networkManager;

    /// Epoch-ms timestamp when the current cooldown expires (0 = not limited).
    /// Atomic so the status can be read from worker threads.
    std::atomic<qint64> m_rateLimitUntilMs{0};
    /// Consecutive 429 responses seen; drives the exponential backoff.
    std::atomic<int> m_consecutive429Count{0};

    /// Internal: actually perform the HTTP post. Always runs on the main thread.
    void doPost(
        const QNetworkRequest &request,
        const QByteArray &body,
        std::function<void(bool, const ChatCompletionResult &, const QString &)> callback,
        const QString &model);

    struct PendingRequest {
        QNetworkReply *reply;
        std::function<void(bool, const ChatCompletionResult &, const QString &)> callback;
    };
    QList<PendingRequest> m_pendingRequests;
};

#endif // CHAT_API_CLIENT_H
