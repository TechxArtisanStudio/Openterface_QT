#include "ChatWindow.h"
#include "ChatBubbleWidget.h"
#include "ChatInputWidget.h"
#include "ChatPlanCardWidget.h"
#include "ChatSkillBar.h"
#include "ChatTraceDialog.h"
#include "ai/ChatManager.h"
#include "ai/ChatSkillManager.h"
#include "ui/globalsetting.h"
#include <QScrollBar>
#include <QApplication>

ChatWindow::ChatWindow(QWidget *parent)
    : QWidget(parent)
{
    // Make this an independent top-level window (companion docked beside the
    // main window) instead of an embedded child widget, which would be clipped
    // by the parent and stay invisible.
    setWindowFlag(Qt::Window);
    setWindowTitle("AI Chat");
    setMinimumWidth(350);
    resize(400, 600);
    setupUI();

    // Connect to ChatManager signals
    ChatManager &mgr = ChatManager::instance();
    connect(&mgr, &ChatManager::messageAppended, this, [this](const ChatMessage &) {
        refreshBubbles();
        scrollToBottom();
    });
    connect(&mgr, &ChatManager::messagesChanged, this, [this]() {
        refreshBubbles();
    });
    connect(&mgr, &ChatManager::sendingStateChanged, this, [this](bool sending) {
        m_inputWidget->setSending(sending);
    });
    connect(&mgr, &ChatManager::lastErrorChanged, this, [this](const QString &error) {
        m_errorLabel->setVisible(!error.isEmpty());
        m_errorLabel->setText(error);
    });
    connect(&mgr, &ChatManager::planChanged, this, [this]() {
        updatePlanCard();
    });

    // Connect skill manager
    connect(&ChatSkillManager::instance(), &ChatSkillManager::skillsChanged,
            this, &ChatWindow::updateSkillBar);

    updateSkillBar();
    updatePlanCard();
}

ChatWindow::~ChatWindow() = default;

void ChatWindow::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(4);

    // Top bar: mode selector + new session + trace
    m_topBar = new QHBoxLayout();
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Chat");
    m_modeCombo->addItem("Agent");
    m_modeCombo->addItem("Planner");
    m_modeCombo->addItem("Guide");

    m_newSessionBtn = new QPushButton("New");
    m_newSessionBtn->setToolTip("Clear chat history");
    m_traceBtn = new QPushButton("Trace");
    m_traceBtn->setToolTip("View AI trace log");

    m_topBar->addWidget(m_modeCombo);
    m_topBar->addStretch();
    m_topBar->addWidget(m_newSessionBtn);
    m_topBar->addWidget(m_traceBtn);
    m_mainLayout->addLayout(m_topBar);

    // Skill bar
    m_skillBar = new ChatSkillBar();
    m_mainLayout->addWidget(m_skillBar);

    // Plan card (hidden by default)
    m_planCard = new ChatPlanCardWidget();
    m_planCard->setVisible(false);
    m_mainLayout->addWidget(m_planCard);

    // Message scroll area
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_messageContainer = new QWidget();
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(4, 4, 4, 4);
    m_messageLayout->setSpacing(4);
    m_messageLayout->addStretch();

    m_scrollArea->setWidget(m_messageContainer);
    m_mainLayout->addWidget(m_scrollArea, 1);

    // Error label
    m_errorLabel = new QLabel();
    m_errorLabel->setStyleSheet("color: red; font-weight: bold;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    m_mainLayout->addWidget(m_errorLabel);

    // Input widget
    m_inputWidget = new ChatInputWidget();
    m_mainLayout->addWidget(m_inputWidget);

    // Connections
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChatWindow::onModeChanged);
    connect(m_newSessionBtn, &QPushButton::clicked, this, &ChatWindow::onNewSessionClicked);
    connect(m_traceBtn, &QPushButton::clicked, this, &ChatWindow::onTraceClicked);
    connect(m_planCard, &ChatPlanCardWidget::approveClicked, this, &ChatWindow::onPlanApproved);
    connect(m_planCard, &ChatPlanCardWidget::clearClicked, this, &ChatWindow::onPlanClearClicked);
    connect(m_skillBar, &ChatSkillBar::skillClicked, this, &ChatWindow::onSkillClicked);
    connect(m_inputWidget, &ChatInputWidget::sendRequested, this, &ChatWindow::onSendClicked);
    connect(m_inputWidget, &ChatInputWidget::stopRequested, this, &ChatWindow::onStopClicked);
}

void ChatWindow::clearAll()
{
    // Remove all bubble widgets
    for (auto *bubble : m_bubbleWidgets) {
        m_messageLayout->removeWidget(bubble);
        bubble->deleteLater();
    }
    m_bubbleWidgets.clear();
    m_errorLabel->setVisible(false);
    m_planCard->setVisible(false);
}

void ChatWindow::scrollToBottom()
{
    QScrollBar *sb = m_scrollArea->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}

void ChatWindow::onSendClicked()
{
    QString text = m_inputWidget->text();
    QString attachment = m_inputWidget->attachmentPath();
    if (text.isEmpty() && attachment.isEmpty()) return;

    m_inputWidget->clear();
    emit messageSendRequested(text, attachment);
    ChatManager::instance().sendMessage(text, attachment);
}

void ChatWindow::onStopClicked()
{
    ChatManager::instance().cancelSending();
}

void ChatWindow::onNewSessionClicked()
{
    ChatManager::instance().clearHistory();
    clearAll();
}

void ChatWindow::onModeChanged(int index)
{
    // Map combo index to settings
    GlobalSetting &gs = GlobalSetting::instance();
    gs.setChatAgenticModeEnabled(index >= 1);  // Agent, Planner, Guide
    gs.setChatPlannerModeEnabled(index == 2);
    gs.setChatGuideModeEnabled(index == 3);
    updateModeUI();
    emit modeChanged(index);
}

void ChatWindow::onTraceClicked()
{
    auto *dialog = new ChatTraceDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ChatWindow::onPlanApproved()
{
    ChatManager::instance().approveCurrentPlan();
}

void ChatWindow::onPlanClearClicked()
{
    ChatManager::instance().clearCurrentPlan();
}

void ChatWindow::onSkillClicked(const QString &skillId)
{
    ChatSkill skill = ChatSkillManager::instance().skillById(skillId);
    if (!skill.id.isEmpty()) {
        ChatManager::instance().runSkill(skill);
    }
    emit skillSelected(skillId);
}

void ChatWindow::onQuickReplyClicked(const QString &text)
{
    ChatManager::instance().sendMessage(text);
}

void ChatWindow::onGuideExecuteClicked(int messageIndex)
{
    auto messages = ChatManager::instance().messages();
    if (messageIndex >= 0 && messageIndex < messages.size()) {
        ChatManager::instance().executeGuideAction(messages[messageIndex], false);
    }
    emit guideActionRequested(messageIndex, false);
}

void ChatWindow::onGuideExecuteNextClicked(int messageIndex)
{
    auto messages = ChatManager::instance().messages();
    if (messageIndex >= 0 && messageIndex < messages.size()) {
        ChatManager::instance().executeGuideAction(messages[messageIndex], true);
    }
    emit guideActionRequested(messageIndex, true);
}

void ChatWindow::onGuideCompleteClicked(int messageIndex)
{
    auto messages = ChatManager::instance().messages();
    if (messageIndex >= 0 && messageIndex < messages.size()) {
        ChatManager::instance().completeGuideStepAndNext(messages[messageIndex].content);
    }
    emit guideStepCompleted(messages[messageIndex].content);
}

void ChatWindow::updateModeUI()
{
    int mode = m_modeCombo->currentIndex();
    // Update placeholder text based on mode
    QStringList placeholders = {
        "Ask anything...",
        "Ask the agent to interact with the target...",
        "Describe a multi-step goal...",
        "Ask for step-by-step guidance..."
    };
    m_inputWidget->setPlaceholder(placeholders.value(mode, placeholders[0]));
}

void ChatWindow::refreshBubbles()
{
    auto messages = ChatManager::instance().messages();

    // Remove excess bubbles
    while (m_bubbleWidgets.size() > messages.size()) {
        auto *bubble = m_bubbleWidgets.takeLast();
        m_messageLayout->removeWidget(bubble);
        bubble->deleteLater();
    }

    // Add missing bubbles
    while (m_bubbleWidgets.size() < messages.size()) {
        int idx = m_bubbleWidgets.size();
        auto *bubble = createBubbleWidget(idx);
        m_bubbleWidgets.append(bubble);
        // Insert before the stretch
        m_messageLayout->insertWidget(m_messageLayout->count() - 1, bubble);
    }

    // Update all bubbles
    for (int i = 0; i < messages.size(); ++i) {
        m_bubbleWidgets[i]->setMessage(messages[i], i);
    }
}

ChatBubbleWidget *ChatWindow::createBubbleWidget(int index)
{
    auto *bubble = new ChatBubbleWidget();
    connect(bubble, &ChatBubbleWidget::quickReplyClicked,
            this, &ChatWindow::onQuickReplyClicked);
    connect(bubble, &ChatBubbleWidget::guideExecuteClicked,
            this, &ChatWindow::onGuideExecuteClicked);
    connect(bubble, &ChatBubbleWidget::guideExecuteNextClicked,
            this, &ChatWindow::onGuideExecuteNextClicked);
    connect(bubble, &ChatBubbleWidget::guideCompleteClicked,
            this, &ChatWindow::onGuideCompleteClicked);
    return bubble;
}

void ChatWindow::updatePlanCard()
{
    if (ChatManager::instance().hasPlan()) {
        m_planCard->setPlan(ChatManager::instance().currentPlan());
        m_planCard->setVisible(true);
    } else {
        m_planCard->setVisible(false);
    }
}

void ChatWindow::updateSkillBar()
{
    m_skillBar->setSkills(ChatSkillManager::instance().skills());
}
