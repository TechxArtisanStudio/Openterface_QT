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

#ifndef CHAT_TYPES_H
#define CHAT_TYPES_H

#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QUrl>
#include <QRectF>
#include <QList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

// ============================================================================
// Chat Role
// ============================================================================
enum class ChatRole {
    System,
    User,
    Assistant,
    Tool
};

inline QString chatRoleToString(ChatRole role) {
    switch (role) {
        case ChatRole::System:    return "system";
        case ChatRole::User:      return "user";
        case ChatRole::Assistant: return "assistant";
        case ChatRole::Tool:      return "tool";
    }
    return "user";
}

inline ChatRole chatRoleFromString(const QString &str) {
    if (str == "system")    return ChatRole::System;
    if (str == "assistant") return ChatRole::Assistant;
    if (str == "tool")      return ChatRole::Tool;
    return ChatRole::User;
}

// ============================================================================
// Chat Quick Reply
// ============================================================================
struct ChatQuickReply {
    QUuid id;
    QString label;
    QString sendText;

    ChatQuickReply() : id(QUuid::createUuid()) {}
    ChatQuickReply(const QString &lbl, const QString &text)
        : id(QUuid::createUuid()), label(lbl), sendText(text) {}

    bool operator==(const ChatQuickReply &other) const { return id == other.id; }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id.toString();
        obj["label"] = label;
        obj["sendText"] = sendText;
        return obj;
    }

    static ChatQuickReply fromJson(const QJsonObject &obj) {
        ChatQuickReply r;
        r.id = QUuid::fromString(obj["id"].toString());
        r.label = obj["label"].toString();
        r.sendText = obj["sendText"].toString();
        return r;
    }
};

// ============================================================================
// Chat Message
// ============================================================================
struct ChatMessage {
    QUuid id;
    ChatRole role;
    QString content;
    QDateTime createdAt;
    QString attachmentFilePath;
    QRectF guideActionRect;    // normalized (0-1) rect for guide overlay
    QString guideShortcut;
    QString guideTool;
    QList<ChatQuickReply> quickReplies;
    // Display-only hint: status step messages (e.g. "Step 2/10 — examining
    // screen...") are not real AI responses. They're inserted by the agent
    // loop to make progress visible. When loading from disk these are not
    // restored — they only exist for the duration of the running request.
    bool isStatusHint = false;
    // Tool call ID for tool role messages (required by OpenAI API format).
    // Links this tool result back to the assistant's tool call.
    QString toolCallId;
    // Processing time in milliseconds (for AI-generated messages)
    int processingTimeMs = 0;
    // Token usage (for AI-generated messages)
    int inputTokens = 0;
    int outputTokens = 0;

    ChatMessage()
        : id(QUuid::createUuid()), role(ChatRole::User), createdAt(QDateTime::currentDateTime()) {}

    ChatMessage(ChatRole r, const QString &c, const QString &attachment = QString())
        : id(QUuid::createUuid()), role(r), content(c), createdAt(QDateTime::currentDateTime()),
          attachmentFilePath(attachment) {}

    bool operator==(const ChatMessage &other) const { return id == other.id; }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id.toString();
        obj["role"] = chatRoleToString(role);
        obj["content"] = content;
        obj["createdAt"] = createdAt.toString(Qt::ISODate);
        if (!attachmentFilePath.isEmpty())
            obj["attachmentFilePath"] = attachmentFilePath;
        if (!guideActionRect.isNull()) {
            QJsonObject rect;
            rect["x"] = guideActionRect.x();
            rect["y"] = guideActionRect.y();
            rect["width"] = guideActionRect.width();
            rect["height"] = guideActionRect.height();
            obj["guideActionRect"] = rect;
        }
        if (!guideShortcut.isEmpty())
            obj["guideShortcut"] = guideShortcut;
        if (!guideTool.isEmpty())
            obj["guideTool"] = guideTool;
        QJsonArray arr;
        for (const auto &qr : quickReplies) arr.append(qr.toJson());
        if (!arr.isEmpty()) obj["quickReplies"] = arr;
        if (!toolCallId.isEmpty()) obj["toolCallId"] = toolCallId;
        return obj;
    }

    static ChatMessage fromJson(const QJsonObject &obj) {
        ChatMessage m;
        m.id = QUuid::fromString(obj["id"].toString());
        m.role = chatRoleFromString(obj["role"].toString());
        m.content = obj["content"].toString();
        m.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
        m.attachmentFilePath = obj["attachmentFilePath"].toString();
        if (obj.contains("guideActionRect")) {
            QJsonObject rect = obj["guideActionRect"].toObject();
            m.guideActionRect = QRectF(rect["x"].toDouble(), rect["y"].toDouble(),
                                       rect["width"].toDouble(), rect["height"].toDouble());
        }
        m.guideShortcut = obj["guideShortcut"].toString();
        m.guideTool = obj["guideTool"].toString();
        m.toolCallId = obj["toolCallId"].toString();
        QJsonArray arr = obj["quickReplies"].toArray();
        for (const auto &v : arr) m.quickReplies.append(ChatQuickReply::fromJson(v.toObject()));
        return m;
    }
};

// ============================================================================
// Chat Task Status / Plan Status
// ============================================================================
enum class ChatTaskStatus {
    Pending,
    Approved,
    Running,
    Completed,
    Failed,
    Skipped
};

inline QString chatTaskStatusToString(ChatTaskStatus s) {
    switch (s) {
        case ChatTaskStatus::Pending:   return "pending";
        case ChatTaskStatus::Approved:  return "approved";
        case ChatTaskStatus::Running:   return "running";
        case ChatTaskStatus::Completed: return "completed";
        case ChatTaskStatus::Failed:    return "failed";
        case ChatTaskStatus::Skipped:   return "skipped";
    }
    return "pending";
}

inline ChatTaskStatus chatTaskStatusFromString(const QString &str) {
    if (str == "approved")  return ChatTaskStatus::Approved;
    if (str == "running")   return ChatTaskStatus::Running;
    if (str == "completed") return ChatTaskStatus::Completed;
    if (str == "failed")    return ChatTaskStatus::Failed;
    if (str == "skipped")   return ChatTaskStatus::Skipped;
    return ChatTaskStatus::Pending;
}

enum class ChatPlanStatus {
    Draft,
    AwaitingApproval,
    AwaitingOSConfirmation,
    Approved,
    Running,
    Completed,
    Failed,
    Cancelled
};

inline QString chatPlanStatusToString(ChatPlanStatus s) {
    switch (s) {
        case ChatPlanStatus::Draft:                  return "draft";
        case ChatPlanStatus::AwaitingApproval:       return "awaitingApproval";
        case ChatPlanStatus::AwaitingOSConfirmation: return "awaitingOSConfirmation";
        case ChatPlanStatus::Approved:               return "approved";
        case ChatPlanStatus::Running:                return "running";
        case ChatPlanStatus::Completed:              return "completed";
        case ChatPlanStatus::Failed:                 return "failed";
        case ChatPlanStatus::Cancelled:              return "cancelled";
    }
    return "draft";
}

inline ChatPlanStatus chatPlanStatusFromString(const QString &str) {
    if (str == "awaitingApproval")       return ChatPlanStatus::AwaitingApproval;
    if (str == "awaitingOSConfirmation") return ChatPlanStatus::AwaitingOSConfirmation;
    if (str == "approved")               return ChatPlanStatus::Approved;
    if (str == "running")                return ChatPlanStatus::Running;
    if (str == "completed")              return ChatPlanStatus::Completed;
    if (str == "failed")                 return ChatPlanStatus::Failed;
    if (str == "cancelled")              return ChatPlanStatus::Cancelled;
    return ChatPlanStatus::Draft;
}

// ============================================================================
// Chat Task
// ============================================================================
struct ChatTask {
    QUuid id;
    QString title;
    QString detail;
    QString agentName;
    QString toolName;
    ChatTaskStatus status;
    QString resultSummary;
    int inputTokenCount = -1;
    int outputTokenCount = -1;

    ChatTask()
        : id(QUuid::createUuid()), status(ChatTaskStatus::Pending) {}

    ChatTask(const QString &t, const QString &d, const QString &agent, const QString &tool)
        : id(QUuid::createUuid()), title(t), detail(d), agentName(agent), toolName(tool),
          status(ChatTaskStatus::Pending) {}

    bool operator==(const ChatTask &other) const { return id == other.id; }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id.toString();
        obj["title"] = title;
        obj["detail"] = detail;
        obj["agentName"] = agentName;
        obj["toolName"] = toolName;
        obj["status"] = chatTaskStatusToString(status);
        if (!resultSummary.isEmpty()) obj["resultSummary"] = resultSummary;
        if (inputTokenCount >= 0) obj["inputTokenCount"] = inputTokenCount;
        if (outputTokenCount >= 0) obj["outputTokenCount"] = outputTokenCount;
        return obj;
    }

    static ChatTask fromJson(const QJsonObject &obj) {
        ChatTask t;
        t.id = QUuid::fromString(obj["id"].toString());
        t.title = obj["title"].toString();
        t.detail = obj["detail"].toString();
        t.agentName = obj["agentName"].toString();
        t.toolName = obj["toolName"].toString();
        t.status = chatTaskStatusFromString(obj["status"].toString());
        t.resultSummary = obj["resultSummary"].toString();
        t.inputTokenCount = obj["inputTokenCount"].toInt(-1);
        t.outputTokenCount = obj["outputTokenCount"].toInt(-1);
        return t;
    }
};

// ============================================================================
// Chat Execution Plan
// ============================================================================
struct ChatExecutionPlan {
    QUuid id;
    QString goal;
    QString summary;
    ChatPlanStatus status;
    QDateTime createdAt;
    QList<ChatTask> tasks;

    ChatExecutionPlan()
        : id(QUuid::createUuid()), status(ChatPlanStatus::Draft),
          createdAt(QDateTime::currentDateTime()) {}

    ChatExecutionPlan(const QString &g, const QString &s, const QList<ChatTask> &t)
        : id(QUuid::createUuid()), goal(g), summary(s),
          status(ChatPlanStatus::AwaitingApproval),
          createdAt(QDateTime::currentDateTime()), tasks(t) {}

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id.toString();
        obj["goal"] = goal;
        obj["summary"] = summary;
        obj["status"] = chatPlanStatusToString(status);
        obj["createdAt"] = createdAt.toString(Qt::ISODate);
        QJsonArray arr;
        for (const auto &t : tasks) arr.append(t.toJson());
        obj["tasks"] = arr;
        return obj;
    }

    static ChatExecutionPlan fromJson(const QJsonObject &obj) {
        ChatExecutionPlan p;
        p.id = QUuid::fromString(obj["id"].toString());
        p.goal = obj["goal"].toString();
        p.summary = obj["summary"].toString();
        p.status = chatPlanStatusFromString(obj["status"].toString());
        p.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
        QJsonArray arr = obj["tasks"].toArray();
        for (const auto &v : arr) p.tasks.append(ChatTask::fromJson(v.toObject()));
        return p;
    }
};

// ============================================================================
// Chat Target System
// ============================================================================
enum class ChatTargetSystem {
    MacOS,
    Windows,
    Linux,
    IPhone,
    IPad,
    Android,
    BIOS,       // BIOS/UEFI setup screens (text-based menu with color highlighting)
    TextUI      // DOS-like text-based interfaces (e.g., old DOS apps, text menus)
};

inline QString chatTargetSystemToString(ChatTargetSystem s) {
    switch (s) {
        case ChatTargetSystem::MacOS:   return "macOS";
        case ChatTargetSystem::Windows: return "windows";
        case ChatTargetSystem::Linux:   return "linux";
        case ChatTargetSystem::IPhone:  return "iPhone";
        case ChatTargetSystem::IPad:    return "iPad";
        case ChatTargetSystem::Android: return "android";
        case ChatTargetSystem::BIOS:    return "bios";
        case ChatTargetSystem::TextUI:  return "textui";
    }
    return "linux";
}

inline ChatTargetSystem chatTargetSystemFromString(const QString &str) {
    QString lower = str.toLower();
    if (lower == "macos" || lower == "mac")   return ChatTargetSystem::MacOS;
    if (lower == "windows" || lower == "win")  return ChatTargetSystem::Windows;
    if (lower == "linux")                      return ChatTargetSystem::Linux;
    if (lower == "iphone")                     return ChatTargetSystem::IPhone;
    if (lower == "ipad")                       return ChatTargetSystem::IPad;
    if (lower == "android")                    return ChatTargetSystem::Android;
    if (lower == "bios" || lower == "uefi")    return ChatTargetSystem::BIOS;
    if (lower == "textui" || lower == "dos")   return ChatTargetSystem::TextUI;
    return ChatTargetSystem::Linux;
}

inline QString chatTargetSystemDisplayName(ChatTargetSystem s) {
    switch (s) {
        case ChatTargetSystem::MacOS:   return "macOS";
        case ChatTargetSystem::Windows: return "Windows";
        case ChatTargetSystem::Linux:   return "Linux";
        case ChatTargetSystem::IPhone:  return "iPhone";
        case ChatTargetSystem::IPad:    return "iPad";
        case ChatTargetSystem::Android: return "Android";
        case ChatTargetSystem::BIOS:    return "BIOS/UEFI";
        case ChatTargetSystem::TextUI:  return "Text-based UI";
    }
    return "Linux";
}

// ============================================================================
// API Configuration and Result
// ============================================================================
struct ChatAPIConfiguration {
    QUrl baseURL;
    QString model;
    QString apiKey;
};

struct ChatCompletionResult {
    QString content;
    int inputTokenCount = -1;
    int outputTokenCount = -1;
};

// ============================================================================
// Agent Tool Call
// ============================================================================
struct AgentToolCall {
    QString tool;
    QVariantMap args;
};

// ============================================================================
// Agent Tool Execution Result
// ============================================================================
struct AgentToolExecutionResult {
    QString summary;
    QString attachmentFilePath;
    QString keyboardOnlyMacroData;
    QString ocrText;  // OCR result from screen_to_markdown

    AgentToolExecutionResult() = default;
    AgentToolExecutionResult(const QString &s, const QString &path, const QString &macroData = QString(), const QString &ocr = QString())
        : summary(s), attachmentFilePath(path), keyboardOnlyMacroData(macroData), ocrText(ocr) {}
};

// ============================================================================
// Chat Task Trace Entry
// ============================================================================
struct ChatTaskTraceEntry {
    QUuid id;
    QDateTime timestamp;
    QString title;
    QString body;
    QString imageFilePath;

    ChatTaskTraceEntry()
        : id(QUuid::createUuid()), timestamp(QDateTime::currentDateTime()) {}

    ChatTaskTraceEntry(const QString &t, const QString &b, const QString &img = QString())
        : id(QUuid::createUuid()), timestamp(QDateTime::currentDateTime()),
          title(t), body(b), imageFilePath(img) {}

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id.toString();
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        obj["title"] = title;
        obj["body"] = body;
        if (!imageFilePath.isEmpty()) obj["imageFilePath"] = imageFilePath;
        return obj;
    }

    static ChatTaskTraceEntry fromJson(const QJsonObject &obj) {
        ChatTaskTraceEntry e;
        e.id = QUuid::fromString(obj["id"].toString());
        e.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        e.title = obj["title"].toString();
        e.body = obj["body"].toString();
        e.imageFilePath = obj["imageFilePath"].toString();
        return e;
    }
};

// ============================================================================
// Guide Auto-Next Status
// ============================================================================
struct GuideAutoNextStatus {
    enum Phase { Thinking, Completed, Failed, Cancelled };
    Phase phase;
    QString text;

    GuideAutoNextStatus() : phase(Thinking) {}
    GuideAutoNextStatus(Phase p, const QString &t) : phase(p), text(t) {}

    bool operator==(const GuideAutoNextStatus &other) const {
        return phase == other.phase && text == other.text;
    }
};

// ============================================================================
// Chat Skill
// ============================================================================
struct ChatSkill {
    QString id;
    QString name;
    QString icon;       // icon name (theme icon or resource path)
    QString prompt;
    bool captureScreen = false;
    QString userLabel;

    QString displayLabel() const {
        return userLabel.isEmpty() ? name : userLabel;
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["icon"] = icon;
        obj["prompt"] = prompt;
        obj["captureScreen"] = captureScreen;
        if (!userLabel.isEmpty()) obj["userLabel"] = userLabel;
        return obj;
    }

    static ChatSkill fromJson(const QJsonObject &obj) {
        ChatSkill s;
        s.id = obj["id"].toString();
        s.name = obj["name"].toString();
        s.icon = obj["icon"].toString();
        s.prompt = obj["prompt"].toString();
        s.captureScreen = obj["captureScreen"].toBool(false);
        s.userLabel = obj["userLabel"].toString();
        return s;
    }
};

// ============================================================================
// Chat API Message (for building API requests)
// ============================================================================
struct ChatApiContentPart {
    enum Type { Text, ImageUrl };
    Type type;
    QString text;
    QString imageUrl;  // data URL or http URL

    static ChatApiContentPart textPart(const QString &t) {
        ChatApiContentPart p;
        p.type = Text;
        p.text = t;
        return p;
    }

    static ChatApiContentPart imagePart(const QString &url) {
        ChatApiContentPart p;
        p.type = ImageUrl;
        p.imageUrl = url;
        return p;
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        if (type == Text) {
            obj["type"] = "text";
            obj["text"] = text;
        } else {
            obj["type"] = "image_url";
            QJsonObject urlObj;
            urlObj["url"] = imageUrl;
            obj["image_url"] = urlObj;
        }
        return obj;
    }
};

struct ChatApiMessage {
    ChatRole role;
    // If contentParts is non-empty, use multimodal encoding.
    // Otherwise use simpleText.
    QString simpleText;
    QList<ChatApiContentPart> contentParts;
    // Tool call ID (required for tool role messages in OpenAI API format).
    QString toolCallId;

    static ChatApiMessage textMessage(ChatRole r, const QString &text) {
        ChatApiMessage m;
        m.role = r;
        m.simpleText = text;
        return m;
    }

    static ChatApiMessage multimodalMessage(ChatRole r, const QString &text, const QString &imageDataURL) {
        ChatApiMessage m;
        m.role = r;
        m.contentParts.append(ChatApiContentPart::textPart(text));
        m.contentParts.append(ChatApiContentPart::imagePart(imageDataURL));
        return m;
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["role"] = chatRoleToString(role);
        if (contentParts.isEmpty()) {
            obj["content"] = simpleText;
        } else {
            QJsonArray arr;
            for (const auto &part : contentParts) arr.append(part.toJson());
            obj["content"] = arr;
        }
        // Include tool_call_id for tool messages (required by OpenAI API)
        if (role == ChatRole::Tool && !toolCallId.isEmpty()) {
            obj["tool_call_id"] = toolCallId;
        }
        return obj;
    }
};

#endif // CHAT_TYPES_H
