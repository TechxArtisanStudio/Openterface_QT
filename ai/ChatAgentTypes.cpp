#include "ChatAgentTypes.h"
#include "ChatInputRouter.h"
#include "ChatToolExecution.h"
#include "WebSearchManager.h"
#include "ui/globalsetting.h"
#include "log/opflogging.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QThread>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
OPF_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

// ============================================================================
// Helper: Extract JSON object from text
// ============================================================================
static bool extractJsonObject(const QString &text, QJsonObject &outObj)
{
    QString trimmed = text.trimmed();
    int start = trimmed.indexOf('{');
    int end = trimmed.lastIndexOf('}');
    if (start < 0 || end < 0 || end <= start) return false;

    QString candidate = trimmed.mid(start, end - start + 1);
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return false;

    outObj = doc.object();
    return true;
}

// ============================================================================
// MainPlannerAgent
// ============================================================================

MainPlannerAgent::MainPlannerAgent(int maxPlannerTasks, QObject *parent)
    : QObject(parent), m_maxPlannerTasks(maxPlannerTasks)
{
}

QList<ChatApiMessage> MainPlannerAgent::buildPlanningConversation(
    const QString &systemPrompt,
    const QString &plannerPrompt,
    const QString &userRequest,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;

    if (!systemPrompt.trimmed().isEmpty()) {
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));
    }

    conversation.append(ChatApiMessage::textMessage(ChatRole::System,
        "Available task agent/tool pairs: screen/capture_screen (AI vision), "
        "screen/screen_to_markdown (OCR text extraction), typing/type_text, "
        "typing/press_key, typing/repeat_key, macro/run_verified_macro, "
        "mouse/move_mouse, mouse/left_click, mouse/left_drag, "
        "mouse/right_click, mouse/double_click, system/run_bash, "
        "system/set_target_system, system/web_search, "
        "recording/start_recording, recording/stop_recording, "
        "terminal/detect_cursor (detect if terminal is idle), "
        "terminal/run_command_and_wait (type command and wait for completion)."));

    if (!plannerPrompt.trimmed().isEmpty()) {
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, plannerPrompt));
    }

    QString requestText = QString("User request:\n%1\n\nReturn a concise JSON plan with at most %2 screen tasks.")
        .arg(userRequest).arg(m_maxPlannerTasks);

    if (!imageDataURL.isEmpty()) {
        conversation.append(ChatApiMessage::multimodalMessage(ChatRole::User, requestText, imageDataURL));
    } else {
        conversation.append(ChatApiMessage::textMessage(ChatRole::User, requestText));
    }

    return conversation;
}

bool MainPlannerAgent::parsePlan(
    const QString &responseText, const QString &goal,
    ChatExecutionPlan &outPlan, QString &error) const
{
    QJsonObject root;
    if (!extractJsonObject(responseText, root)) {
        error = "Planner response did not contain valid JSON";
        return false;
    }

    QString summary = root["summary"].toString().trimmed();
    if (summary.isEmpty()) {
        summary = "Review the current target screen in a few focused steps.";
    }

    QJsonArray taskArray = root["tasks"].toArray();
    if (taskArray.isEmpty()) {
        error = "Planner returned an empty task list";
        return false;
    }

    QList<ChatTask> tasks;
    int limit = qMin(taskArray.size(), m_maxPlannerTasks);
    for (int i = 0; i < limit; ++i) {
        QJsonObject taskObj = taskArray[i].toObject();
        QString title = taskObj["title"].toString().trimmed();
        QString detail = taskObj["detail"].toString().trimmed();
        QString agent = taskObj["agent"].toString().trimmed();
        QString tool = taskObj["tool"].toString().trimmed();

        if (title.isEmpty() || detail.isEmpty()) continue;
        if (agent.isEmpty()) agent = "screen";
        if (tool.isEmpty()) tool = "capture_screen";

        tasks.append(ChatTask(title, detail, agent, tool));
    }

    if (tasks.isEmpty()) {
        error = "Planner returned no valid tasks after filtering";
        return false;
    }

    outPlan = ChatExecutionPlan(goal, summary, tasks);
    outPlan.status = ChatPlanStatus::AwaitingApproval;
    return true;
}

// ============================================================================
// ScreenTaskAgent
// ============================================================================

QString ScreenTaskAgent::prompt() const
{
    return GlobalSetting::instance().getChatScreenTaskPrompt().trimmed();
}

QList<ChatApiMessage> ScreenTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\nTask title: %2\nTask detail: %3\nTool: %4\n\n"
        "Use the latest screen image to complete only this task.")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    if (!imageDataURL.isEmpty()) {
        conversation.append(ChatApiMessage::multimodalMessage(ChatRole::User, instruction, imageDataURL));
    } else {
        conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    }

    return conversation;
}

void ScreenTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (extractJsonObject(response, obj)) {
        QString status = obj["status"].toString().trimmed().toLower();
        task.status = (status == "completed") ? ChatTaskStatus::Completed : ChatTaskStatus::Failed;
        task.resultSummary = obj["result_summary"].toString().trimmed();
    } else {
        task.status = ChatTaskStatus::Completed;
        task.resultSummary = response.trimmed();
    }
}

// ============================================================================
// TypeTextTaskAgent
// ============================================================================

QString TypeTextTaskAgent::prompt() const
{
    return GlobalSetting::instance().getChatTypingTaskPrompt().trimmed();
}

QList<ChatApiMessage> TypeTextTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\nTask title: %2\nTask detail: %3\nTool: %4\n\n"
        "Return text_to_type containing the exact text that must be sent to target keyboard input.")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    if (!imageDataURL.isEmpty()) {
        conversation.append(ChatApiMessage::multimodalMessage(ChatRole::User, instruction, imageDataURL));
    } else {
        conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    }

    return conversation;
}

void TypeTextTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (!extractJsonObject(response, obj)) {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = "Typing task failed: response was not valid JSON.";
        return;
    }

    QString status = obj["status"].toString().trimmed().toLower();
    QString textToType = obj["text_to_type"].toString().trimmed();
    QString shortcut = obj["shortcut"].toString().trimmed();

    ChatInputRouter &router = ChatInputRouter::instance();

    if (status == "completed" && !shortcut.isEmpty()) {
        router.sendShortcut(shortcut);
        task.status = ChatTaskStatus::Completed;
        QString summary = obj["result_summary"].toString().trimmed();
        task.resultSummary = summary.isEmpty()
            ? QString("Executed shortcut %1 on target.").arg(shortcut)
            : summary;
        return;
    }

    if (status == "completed" && !textToType.isEmpty()) {
        router.sendText(textToType);
        task.status = ChatTaskStatus::Completed;
        QString summary = obj["result_summary"].toString().trimmed();
        task.resultSummary = summary.isEmpty()
            ? QString("Typed %1 characters on target.").arg(textToType.length())
            : summary;
        return;
    }

    task.status = ChatTaskStatus::Failed;
    QString summary = obj["result_summary"].toString().trimmed();
    task.resultSummary = summary.isEmpty()
        ? "Typing task failed: missing text_to_type/shortcut or status not completed."
        : summary;
}

// ============================================================================
// MouseTaskAgent
// ============================================================================

MouseTaskAgent::MouseTaskAgent(const QString &tool)
    : m_toolName(tool)
{
}

QString MouseTaskAgent::prompt() const
{
    return GlobalSetting::instance().getChatScreenTaskPrompt().trimmed();
}

QList<ChatApiMessage> MouseTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\n"
        "Task title: %2\n"
        "Task detail: %3\n"
        "Tool: %4\n\n"
        "Return JSON only.\n"
        "- Always provide x and y as normalized floats from 0.0 to 1.0 (fraction of screen width/height).\n"
        "- For click and move tools, x and y are required.\n"
        "- For left_drag, x and y are the drag destination and optional start_x/start_y specify the drag start point.\n"
        "- Choose the center point of the exact UI element to interact with.")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    if (!imageDataURL.isEmpty()) {
        conversation.append(ChatApiMessage::multimodalMessage(ChatRole::User, instruction, imageDataURL));
    } else {
        conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    }

    return conversation;
}

void MouseTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (!extractJsonObject(response, obj)) {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = "Mouse task failed: response was not valid JSON.";
        return;
    }

    QString status = obj["status"].toString().trimmed().toLower();
    if (status != "completed") {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = obj["result_summary"].toString().trimmed();
        return;
    }

    bool xOk = obj.contains("x");
    bool yOk = obj.contains("y");
    if (!xOk || !yOk) {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = QString("Mouse task failed: x and y are required for %1.").arg(m_toolName);
        return;
    }

    double rawX = obj["x"].toDouble();
    double rawY = obj["y"].toDouble();
    int targetX = ChatInputRouter::normalizedToAbsolute(rawX);
    int targetY = ChatInputRouter::normalizedToAbsolute(rawY);

    ChatInputRouter &router = ChatInputRouter::instance();

    if (m_toolName == "move_mouse") {
        router.sendMouseMove(targetX, targetY);
    } else if (m_toolName == "left_click") {
        router.animatedClick(0x01, targetX, targetY, false);
    } else if (m_toolName == "right_click") {
        router.animatedClick(0x02, targetX, targetY, false);
    } else if (m_toolName == "double_click") {
        router.animatedClick(0x01, targetX, targetY, true);
    } else if (m_toolName == "left_drag") {
        int startX = obj.contains("start_x") ? ChatInputRouter::normalizedToAbsolute(obj["start_x"].toDouble()) : router.trackedMouseX();
        int startY = obj.contains("start_y") ? ChatInputRouter::normalizedToAbsolute(obj["start_y"].toDouble()) : router.trackedMouseY();
        router.animatedDrag(startX, startY, targetX, targetY);
    } else {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = QString("Mouse task failed: unsupported tool %1.").arg(m_toolName);
        return;
    }

    task.status = ChatTaskStatus::Completed;
    QString summary = obj["result_summary"].toString().trimmed();
    task.resultSummary = summary.isEmpty()
        ? QString("Mouse task executed using %1 at normalized (%2, %3).")
            .arg(m_toolName).arg(rawX, 0, 'f', 3).arg(rawY, 0, 'f', 3)
        : summary;
}

// ============================================================================
// PressKeyTaskAgent
// ============================================================================

QString PressKeyTaskAgent::prompt() const
{
    return GlobalSetting::instance().getChatTypingTaskPrompt().trimmed();
}

QList<ChatApiMessage> PressKeyTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\nTask title: %2\nTask detail: %3\nTool: %4\n\n"
        "Return shortcut containing the exact keyboard combination to press (e.g. Ctrl+Alt+T, Cmd+Space, Enter).")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    if (!imageDataURL.isEmpty()) {
        conversation.append(ChatApiMessage::multimodalMessage(ChatRole::User, instruction, imageDataURL));
    } else {
        conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    }

    return conversation;
}

void PressKeyTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (!extractJsonObject(response, obj)) {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = "Press key task failed: response was not valid JSON.";
        return;
    }

    QString status = obj["status"].toString().trimmed().toLower();
    QString shortcut = obj["shortcut"].toString().trimmed();

    if (status == "completed" && !shortcut.isEmpty()) {
        ChatInputRouter &router = ChatInputRouter::instance();
        router.sendShortcut(shortcut);

        // Give the target time to process the key and execute any command
        // Especially important for Enter key which triggers command execution
        QThread::msleep(1500);

        task.status = ChatTaskStatus::Completed;
        QString summary = obj["result_summary"].toString().trimmed();
        task.resultSummary = summary.isEmpty()
            ? QString("Executed shortcut %1 on target.").arg(shortcut)
            : summary;
        return;
    }

    task.status = ChatTaskStatus::Failed;
    QString summary = obj["result_summary"].toString().trimmed();
    task.resultSummary = summary.isEmpty()
        ? "Press key task failed: missing shortcut or status not completed."
        : summary;
}

// ============================================================================
// ScreenToMarkdownTaskAgent
// ============================================================================

QString ScreenToMarkdownTaskAgent::prompt() const
{
    return GlobalSetting::instance().getChatScreenTaskPrompt().trimmed();
}

QList<ChatApiMessage> ScreenToMarkdownTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\nTask title: %2\nTask detail: %3\nTool: %4\n\n"
        "Use OCR to extract text from the screen image.")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    if (!imageDataURL.isEmpty()) {
        conversation.append(ChatApiMessage::multimodalMessage(ChatRole::User, instruction, imageDataURL));
    } else {
        conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    }

    return conversation;
}

void ScreenToMarkdownTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (extractJsonObject(response, obj)) {
        QString status = obj["status"].toString().trimmed().toLower();
        task.status = (status == "completed") ? ChatTaskStatus::Completed : ChatTaskStatus::Failed;
        task.resultSummary = obj["result_summary"].toString().trimmed();
    } else {
        task.status = ChatTaskStatus::Completed;
        task.resultSummary = response.trimmed();
    }
}

// ============================================================================
// RunBashTaskAgent
// ============================================================================

QString RunBashTaskAgent::prompt() const
{
    return "You are the Openterface Bash Task Agent.\n\n"
           "You are responsible for executing bash commands on the HOST computer.\n\n"
           "Rules:\n"
           "- Return ONLY JSON.\n"
           "- Provide the exact command to execute.\n"
           "- The command will run on the HOST (not the target machine).\n\n"
           "Schema:\n"
           "{\n"
           "    \"status\": \"completed\" | \"failed\",\n"
           "    \"command\": \"bash command to execute\",\n"
           "    \"result_summary\": \"short summary for the user\"\n"
           "}\n";
}

QList<ChatApiMessage> RunBashTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\nTask title: %2\nTask detail: %3\nTool: %4\n\n"
        "Return the bash command to execute on the HOST computer.")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    return conversation;
}

void RunBashTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (!extractJsonObject(response, obj)) {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = "Bash task failed: response was not valid JSON.";
        return;
    }

    QString status = obj["status"].toString().trimmed().toLower();
    QString command = obj["command"].toString().trimmed();

    if (status == "completed" && !command.isEmpty()) {
        QString result = ChatToolExecution::instance().runBashCommand(command);
        task.status = ChatTaskStatus::Completed;
        QString summary = obj["result_summary"].toString().trimmed();
        task.resultSummary = summary.isEmpty()
            ? QString("Executed: %1\nResult: %2").arg(command, result)
            : QString("%1\nResult: %2").arg(summary, result);
        return;
    }

    task.status = ChatTaskStatus::Failed;
    QString summary = obj["result_summary"].toString().trimmed();
    task.resultSummary = summary.isEmpty()
        ? "Bash task failed: missing command or status not completed."
        : summary;
}

// ============================================================================
// WebSearchTaskAgent
// ============================================================================

QString WebSearchTaskAgent::prompt() const
{
    return "You are the Openterface Web Search Agent.\n\n"
           "You are responsible for searching the internet for information.\n\n"
           "Rules:\n"
           "- Return ONLY JSON.\n"
           "- Provide a clear search query.\n\n"
           "Schema:\n"
           "{\n"
           "    \"status\": \"completed\" | \"failed\",\n"
           "    \"query\": \"search query\",\n"
           "    \"result_summary\": \"short summary for the user\"\n"
           "}\n";
}

QList<ChatApiMessage> WebSearchTaskAgent::buildTaskConversation(
    const QString &systemPrompt,
    const ChatExecutionPlan &plan,
    const ChatTask &task,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;
    if (!systemPrompt.trimmed().isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));

    QString taskPrompt = prompt();
    if (!taskPrompt.isEmpty())
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, taskPrompt));

    QString instruction = QString(
        "Plan summary: %1\n\nTask title: %2\nTask detail: %3\nTool: %4\n\n"
        "Return the search query to find information on the internet.")
        .arg(plan.summary, task.title, task.detail, task.toolName);

    conversation.append(ChatApiMessage::textMessage(ChatRole::User, instruction));
    return conversation;
}

void WebSearchTaskAgent::applyResponse(const QString &response, ChatTask &task)
{
    QJsonObject obj;
    if (!extractJsonObject(response, obj)) {
        task.status = ChatTaskStatus::Failed;
        task.resultSummary = "Web search task failed: response was not valid JSON.";
        return;
    }

    QString status = obj["status"].toString().trimmed().toLower();
    QString query = obj["query"].toString().trimmed();

    if (status == "completed" && !query.isEmpty()) {
        QString result = WebSearchManager::instance().search(query);
        task.status = ChatTaskStatus::Completed;
        QString summary = obj["result_summary"].toString().trimmed();
        task.resultSummary = summary.isEmpty()
            ? QString("Searched for: %1\nResult: %2").arg(query, result)
            : QString("%1\nResult: %2").arg(summary, result);
        return;
    }

    task.status = ChatTaskStatus::Failed;
    QString summary = obj["result_summary"].toString().trimmed();
    task.resultSummary = summary.isEmpty()
        ? "Web search task failed: missing query or status not completed."
        : summary;
}

// ============================================================================
// TaskAgentRegistry
// ============================================================================

TaskAgentRegistry::TaskAgentRegistry()
{
    // Create static agents (owned by the registry via parent-child, but we keep raw pointers)
    static ScreenTaskAgent screenAgent;
    static ScreenToMarkdownTaskAgent screenToMarkdownAgent;
    static TypeTextTaskAgent typingAgent;
    static PressKeyTaskAgent pressKeyAgent;
    static MouseTaskAgent moveAgent("move_mouse");
    static MouseTaskAgent leftClickAgent("left_click");
    static MouseTaskAgent rightClickAgent("right_click");
    static MouseTaskAgent doubleClickAgent("double_click");
    static MouseTaskAgent leftDragAgent("left_drag");
    static RunBashTaskAgent runBashAgent;
    static WebSearchTaskAgent webSearchAgent;

    QList<TaskAgentExecutor *> agents = {
        &screenAgent, &screenToMarkdownAgent,
        &typingAgent, &pressKeyAgent,
        &moveAgent, &leftClickAgent, &rightClickAgent,
        &doubleClickAgent, &leftDragAgent,
        &runBashAgent, &webSearchAgent
    };

    for (auto *agent : agents) {
        m_exactMappings[makeExactKey(agent->agentName(), agent->toolName())] = agent;
        m_toolMappings[agent->toolName().trimmed().toLower()] = agent;
    }
}

TaskAgentExecutor *TaskAgentRegistry::resolve(const ChatTask &task) const
{
    QString exactKey = makeExactKey(task.agentName, task.toolName);
    auto it = m_exactMappings.find(exactKey);
    if (it != m_exactMappings.end()) return it.value();

    QString toolKey = task.toolName.trimmed().toLower();
    auto it2 = m_toolMappings.find(toolKey);
    if (it2 != m_toolMappings.end()) return it2.value();

    return nullptr;
}

QString TaskAgentRegistry::makeExactKey(const QString &agentName, const QString &toolName)
{
    return agentName.trimmed().toLower() + "::" + toolName.trimmed().toLower();
}
