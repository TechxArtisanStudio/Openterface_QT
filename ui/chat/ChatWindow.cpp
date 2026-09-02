#include "ChatWindow.h"
#include "ChatBubbleWidget.h"
#include "ChatEmptyStateWidget.h"
#include "ChatInputWidget.h"
#include "ChatPlanCardWidget.h"
#include "ChatTraceDialog.h"
#include "ai/ChatManager.h"
#include "ai/ChatSkillManager.h"
#include "ai/ChatTypes.h"
#include "ui/globalsetting.h"
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QStyleOption>
#include <QTimer>
#include <QMenu>
#include <QActionGroup>

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
    connect(&mgr, &ChatManager::messageUpdated, this, [this](int index, const ChatMessage &msg) {
        // Update just the specific bubble instead of rebuilding all
        if (index >= 0 && index < m_bubbleWidgets.size()) {
            m_bubbleWidgets[index]->setMessage(msg, index);
        }
        scrollToBottom();
    });
    connect(&mgr, &ChatManager::messagesChanged, this, [this]() {
        refreshBubbles();
        scrollToBottom();
    });
    connect(&mgr, &ChatManager::lastErrorChanged, this, [this](const QString &error) {
        m_errorRow->setVisible(!error.isEmpty());
        m_errorLabel->setText(error);
    });
    connect(&mgr, &ChatManager::planChanged, this, [this]() {
        updatePlanCard();
        scrollToBottom();
    });
    connect(&mgr, &ChatManager::agentRequestStatusChanged, this,
            [this](const QUuid & /*messageID*/, const GuideAutoNextStatus &status) {
        if (status.phase == GuideAutoNextStatus::Thinking) {
            m_statusLabel->setText(status.text);
            m_statusLabel->setVisible(true);
        } else {
            // Completed / Failed / Cancelled — briefly show the terminal state
            // only if it's noteworthy (Failed / Cancelled). Completed is silent.
            m_statusLabel->setText(status.text);
            m_statusLabel->setVisible(!status.text.isEmpty()
                                      && status.phase != GuideAutoNextStatus::Completed);
        }
        scrollToBottom();
    });
    connect(&mgr, &ChatManager::sendingStateChanged, this, [this](bool sending) {
        m_inputWidget->setSending(sending);
        if (!sending) {
            m_statusLabel->setVisible(false);
        }
    });

    // Connect skill manager — populates the empty-state quick links
    connect(&ChatSkillManager::instance(), &ChatSkillManager::skillsChanged,
            this, &ChatWindow::updateEmptyState);

    // Initialize mode combo box from saved settings
    GlobalSetting &gs = GlobalSetting::instance();
    // Keep the header target-OS button in sync when the setting is changed from
    // elsewhere (e.g. the AI calling set_target_system).
    connect(&gs, &GlobalSetting::chatTargetSystemChanged,
            this, &ChatWindow::updateTargetOsButton);
    if (gs.getChatGuideModeEnabled()) {
        m_modeCombo->setCurrentIndex(2);  // Guide mode
    } else if (gs.getChatPlannerModeEnabled()) {
        m_modeCombo->setCurrentIndex(1);  // Planner mode
    } else {
        m_modeCombo->setCurrentIndex(0);  // Agent mode (default)
    }

    updateEmptyState();
    updatePlanCard();
    updateModeUI();
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
    m_modeCombo->addItem("Agent");
    m_modeCombo->addItem("Planner");
    m_modeCombo->addItem("Guide");

    m_newSessionBtn = new QPushButton();
    {
        QPixmap pix(20, 20);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(QPalette::WindowText), 2.0, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(5, 10, 15, 10); // horizontal
        p.drawLine(10, 5, 10, 15); // vertical
        m_newSessionBtn->setIcon(QIcon(pix));
    }
    m_newSessionBtn->setIconSize(QSize(20, 20));
    m_newSessionBtn->setToolTip("Clear chat history");
    m_newSessionBtn->setFlat(true);
    m_newSessionBtn->setFixedSize(28, 28);

    m_traceBtn = new QPushButton();
    m_traceBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogInfoView));
    m_traceBtn->setIconSize(QSize(20, 20));
    m_traceBtn->setToolTip("View AI trace log");
    m_traceBtn->setFlat(true);
    m_traceBtn->setFixedSize(28, 28);

    // Target OS button (next to the + button): shows current target OS and lets
    // the user change it. The selection is stored in GlobalSetting and consumed
    // by ChatManager when assembling the agent prompt.
    m_targetOsBtn = new QPushButton();
    m_targetOsBtn->setCursor(Qt::PointingHandCursor);
    m_targetOsBtn->setToolTip("Target OS (click to change)");
    m_targetOsBtn->setMinimumHeight(28);
    // Draw a small monitor icon to the left of the OS name text.
    {
        QPixmap pix(20, 20);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(QPalette::WindowText), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // Monitor body
        p.drawRoundedRect(QRectF(2, 3, 16, 10), 1.5, 1.5);
        // Stand
        p.drawLine(QPointF(10, 13), QPointF(10, 16));
        p.drawLine(QPointF(6, 16), QPointF(14, 16));
        m_targetOsBtn->setIcon(QIcon(pix));
    }
    m_targetOsBtn->setIconSize(QSize(20, 20));
    {
        auto *menu = new QMenu(m_targetOsBtn);
        auto *group = new QActionGroup(menu);
        group->setExclusive(true);
        const QList<ChatTargetSystem> systems = {
            ChatTargetSystem::Linux,
            ChatTargetSystem::MacOS,
            ChatTargetSystem::Windows,
            ChatTargetSystem::IPhone,
            ChatTargetSystem::IPad,
            ChatTargetSystem::Android,
            ChatTargetSystem::BIOS,
            ChatTargetSystem::TextUI,
        };
        for (ChatTargetSystem sys : systems) {
            auto *act = new QAction(chatTargetSystemDisplayName(sys), menu);
            act->setCheckable(true);
            act->setData(QVariant::fromValue(static_cast<int>(sys)));
            group->addAction(act);
            menu->addAction(act);
            connect(act, &QAction::triggered, this, [this, sys]() {
                GlobalSetting::instance().setChatTargetSystem(chatTargetSystemToString(sys));
                updateTargetOsButton();
            });
        }
        m_targetOsBtn->setMenu(menu);
    }
    updateTargetOsButton();

    m_topBar->addWidget(m_modeCombo);
    m_topBar->addStretch();
    m_topBar->addWidget(m_newSessionBtn);
    m_topBar->addWidget(m_targetOsBtn);
    m_topBar->addWidget(m_traceBtn);
    m_mainLayout->addLayout(m_topBar);

    // Plan card (hidden by default)
    m_planCard = new ChatPlanCardWidget();
    m_planCard->setVisible(false);
    m_mainLayout->addWidget(m_planCard);

    // Stacked area: empty-state quick links (centered) OR message scroll area
    m_stackedArea = new QStackedWidget();

    // Page 0: Empty state with centered quick-link buttons
    m_emptyState = new ChatEmptyStateWidget();
    connect(m_emptyState, &ChatEmptyStateWidget::skillClicked,
            this, &ChatWindow::onSkillClicked);
    m_stackedArea->addWidget(m_emptyState);

    // Page 1: Message scroll area
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
    m_stackedArea->addWidget(m_scrollArea);

    // Start on the empty-state page
    m_stackedArea->setCurrentIndex(0);
    m_mainLayout->addWidget(m_stackedArea, 1);

    // Error row: label + copy button
    m_errorRow = new QWidget();
    m_errorRow->setVisible(false);
    auto *errorRowLayout = new QHBoxLayout(m_errorRow);
    errorRowLayout->setContentsMargins(0, 0, 0, 0);
    errorRowLayout->setSpacing(4);

    m_errorLabel = new QLabel();
    m_errorLabel->setStyleSheet("color: red; font-weight: bold;");
    m_errorLabel->setWordWrap(true);
    errorRowLayout->addWidget(m_errorLabel, 1);

    m_errorCopyBtn = new QPushButton();
    {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(QPalette::WindowText), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(2, 1, 9, 10);
        p.drawRect(5, 4, 9, 10);
        m_errorCopyBtn->setIcon(QIcon(pix));
    }
    m_errorCopyBtn->setIconSize(QSize(16, 16));
    m_errorCopyBtn->setToolTip("Copy error message");
    m_errorCopyBtn->setFlat(true);
    m_errorCopyBtn->setFixedSize(24, 24);
    m_errorCopyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_errorCopyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_errorLabel->text());
    });
    errorRowLayout->addWidget(m_errorCopyBtn);

    m_mainLayout->addWidget(m_errorRow);

    // Iteration status label (shown during agentic runs, e.g. "Thinking (2/10)...")
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #666; font-style: italic;");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    m_mainLayout->addWidget(m_statusLabel);

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
    m_errorRow->setVisible(false);
    m_statusLabel->setVisible(false);
    m_planCard->setVisible(false);
    // Show the empty-state quick links
    m_stackedArea->setCurrentIndex(0);
}

void ChatWindow::scrollToBottom()
{
    // Defer the scroll to allow the layout to fully update.
    // We use a small delay (50ms) instead of 0ms to ensure all widgets have
    // finished their layout calculations, especially the QTextBrowser in chat
    // bubbles which needs time to measure text height after content changes.
    QTimer::singleShot(50, this, [this]() {
        // Force layout update before calculating scroll position
        m_scrollArea->widget()->updateGeometry();
        m_scrollArea->widget()->layout()->activate();

        QScrollBar *sb = m_scrollArea->verticalScrollBar();
        if (sb) {
            // Scroll to the absolute maximum to ensure we reach the bottom
            sb->setValue(sb->maximum());
        }
    });
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
    // Map combo index to settings (Agent=0, Planner=1, Guide=2)
    GlobalSetting &gs = GlobalSetting::instance();
    gs.setChatAgenticModeEnabled(true);  // All modes use agentic features
    gs.setChatPlannerModeEnabled(index == 1);  // Planner mode
    gs.setChatGuideModeEnabled(index == 2);  // Guide mode
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
    // Update placeholder text based on mode (Agent=0, Planner=1, Guide=2)
    QStringList placeholders = {
        "Ask the agent to interact with the target...",
        "Describe a multi-step goal...",
        "Ask for step-by-step guidance..."
    };
    m_inputWidget->setPlaceholder(placeholders.value(mode, placeholders[0]));
}

void ChatWindow::refreshBubbles()
{
    auto messages = ChatManager::instance().messages();

    // Switch to messages page when there are messages
    if (!messages.isEmpty()) {
        m_stackedArea->setCurrentIndex(1);
    } else {
        m_stackedArea->setCurrentIndex(0);
    }

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

void ChatWindow::updateEmptyState()
{
    m_emptyState->setSkills(ChatSkillManager::instance().skills());
}

void ChatWindow::updateTargetOsButton()
{
    if (!m_targetOsBtn) return;
    const QString sys = GlobalSetting::instance().getChatTargetSystem();
    const ChatTargetSystem ts = chatTargetSystemFromString(sys);
    const QString display = chatTargetSystemDisplayName(ts);
    m_targetOsBtn->setText(display);
    // Highlight the matching menu action
    if (auto *menu = m_targetOsBtn->menu()) {
        for (QAction *act : menu->actions()) {
            const ChatTargetSystem actSys =
                static_cast<ChatTargetSystem>(act->data().toInt());
            act->setChecked(actSys == ts);
        }
    }
}

void ChatWindow::onTargetOsSelected(const QString &system)
{
    GlobalSetting::instance().setChatTargetSystem(system);
    updateTargetOsButton();
}
