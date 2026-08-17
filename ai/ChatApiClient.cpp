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

#include "ChatApiClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QHttpMultiPart>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

ChatApiClient::ChatApiClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

ChatApiClient &ChatApiClient::instance()
{
    static ChatApiClient inst;
    return inst;
}

ChatApiClient::~ChatApiClient()
{
    cancelAll();
}

void ChatApiClient::sendCompletion(
    const QUrl &baseURL,
    const QString &model,
    const QString &apiKey,
    const QList<ChatApiMessage> &messages,
    std::optional<bool> enableThinking,
    std::function<void(bool, const ChatCompletionResult &, const QString &)> callback)
{
    // Build request URL
    QUrl url = baseURL;
    QString path = url.path();
    if (path.endsWith('/')) path.chop(1);
    url.setPath(path + "/chat/completions");

    // Build request body
    QJsonObject requestBody;
    requestBody["model"] = model;
    requestBody["stream"] = false;

    // Build messages array
    QJsonArray messagesArray;
    for (const auto &msg : messages) {
        messagesArray.append(msg.toJson());
    }
    requestBody["messages"] = messagesArray;

    // Optional: enable thinking (for models that support it)
    if (enableThinking.has_value()) {
        requestBody["enable_thinking"] = enableThinking.value();
    }

    QJsonDocument doc(requestBody);
    QByteArray body = doc.toJson(QJsonDocument::Compact);

    // Create HTTP request
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    qCDebug(log_ai_chat) << "AI Chat request ->" << url.toString()
                         << "model=" << model
                         << "messages=" << messages.count()
                         << "bodyBytes=" << body.size();

    emit requestStarted(url.toString(), body.size());

    // Send request
    QNetworkReply *reply = m_networkManager->post(request, body);

    PendingRequest pending;
    pending.reply = reply;
    pending.callback = callback;
    m_pendingRequests.append(pending);

    connect(reply, &QNetworkReply::finished, this, [this, reply, model]() {
        // Find and remove the pending request
        PendingRequest found;
        bool foundIt = false;
        for (int i = 0; i < m_pendingRequests.size(); ++i) {
            if (m_pendingRequests[i].reply == reply) {
                found = m_pendingRequests.takeAt(i);
                foundIt = true;
                break;
            }
        }
        if (!foundIt) {
            reply->deleteLater();
            return;
        }

        reply->deleteLater();

        // Check for network error
        if (reply->error() != QNetworkReply::NoError) {
            QString errStr = reply->errorString();
            qCWarning(log_ai_chat) << "AI Chat network error:" << errStr;
            ChatCompletionResult empty;
            if (found.callback) found.callback(false, empty, errStr);
            return;
        }

        // Get HTTP status
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray responseData = reply->readAll();

        qCDebug(log_ai_chat) << "AI Chat response <-" << httpStatus << "bytes=" << responseData.size();
        emit responseReceived(httpStatus, responseData.size());

        // Check HTTP status
        if (httpStatus < 200 || httpStatus >= 300) {
            QString body = QString::fromUtf8(responseData).left(500);
            QString errStr = QString("Chat API error %1: %2").arg(httpStatus).arg(body);
            qCWarning(log_ai_chat) << "AI Chat HTTP error:" << errStr;
            ChatCompletionResult empty;
            if (found.callback) found.callback(false, empty, errStr);
            return;
        }

        // Parse response
        QJsonParseError parseErr;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            QString errStr = QString("Failed to parse response: %1").arg(parseErr.errorString());
            qCWarning(log_ai_chat) << "AI Chat parse error:" << errStr;
            ChatCompletionResult empty;
            if (found.callback) found.callback(false, empty, errStr);
            return;
        }

        QJsonObject respObj = responseDoc.object();

        // Extract content from choices[0].message.content
        QJsonArray choices = respObj["choices"].toArray();
        if (choices.isEmpty()) {
            QString errStr = "Empty assistant response (no choices)";
            qCWarning(log_ai_chat) << "AI Chat:" << errStr;
            ChatCompletionResult empty;
            if (found.callback) found.callback(false, empty, errStr);
            return;
        }

        QString content = choices.first().toObject()["message"].toObject()["content"].toString();
        if (content.isEmpty()) {
            QString errStr = "Empty assistant content";
            qCWarning(log_ai_chat) << "AI Chat:" << errStr;
            ChatCompletionResult empty;
            if (found.callback) found.callback(false, empty, errStr);
            return;
        }

        // Extract usage
        ChatCompletionResult result;
        result.content = content;
        QJsonObject usage = respObj["usage"].toObject();
        if (!usage.isEmpty()) {
            result.inputTokenCount = usage["prompt_tokens"].toInt(-1);
            result.outputTokenCount = usage["completion_tokens"].toInt(-1);
        }

        qCDebug(log_ai_chat) << "AI Chat response received: chars=" << content.length()
                             << "inputTokens=" << result.inputTokenCount
                             << "outputTokens=" << result.outputTokenCount;

        if (found.callback) found.callback(true, result, QString());
    });
}

void ChatApiClient::cancelAll()
{
    for (auto &pending : m_pendingRequests) {
        if (pending.reply) {
            pending.reply->abort();
        }
    }
    m_pendingRequests.clear();
}
