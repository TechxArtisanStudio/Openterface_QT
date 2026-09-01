#include "ChatBubbleWidget.h"
#include "QuickReplyWidget.h"
#include <QVBoxLayout>
#include <QPixmap>
#include <QClipboard>
#include <QApplication>
#include <QFileInfo>
#include <QDialog>
#include <QLabel>
#include <QScrollArea>
#include <QScreen>
#include <QPainter>
#include <QTextBrowser>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

ChatBubbleWidget::ChatBubbleWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void ChatBubbleWidget::setupUI()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 4, 8, 4);
    m_layout->setSpacing(2);

    // Header row container: role label (left) + copy button (right).
    // Wrapped in a QWidget so we can hide the entire row for status hints.
    m_headerWidget = new QWidget();
    m_headerWidget->setContentsMargins(0, 0, 0, 0);
    auto *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    m_roleLabel = new QLabel();
    m_roleLabel->setStyleSheet("font-weight: bold; font-size: 11px; color: #666;");
    headerLayout->addWidget(m_roleLabel);

    headerLayout->addStretch();

    // Copy button — icon-only, flat, shown at top-right of bubble.
    // Paint a "copy" icon: two overlapping rectangles.
    m_copyBtn = new QPushButton();
    {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(QPalette::WindowText), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // Back rectangle (offset up-left)
        p.drawRect(2, 1, 9, 10);
        // Front rectangle (offset down-right)
        p.drawRect(5, 4, 9, 10);
        m_copyBtn->setIcon(QIcon(pix));
    }
    m_copyBtn->setIconSize(QSize(16, 16));
    m_copyBtn->setToolTip("Copy message");
    m_copyBtn->setFlat(true);
    m_copyBtn->setFixedSize(24, 24);
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setVisible(false);
    headerLayout->addWidget(m_copyBtn);

    m_layout->addWidget(m_headerWidget);

    m_contentBrowser = new QTextBrowser();
    m_contentBrowser->setOpenExternalLinks(true);
    m_contentBrowser->setReadOnly(true);
    // Disable the browser's own scroll bar — the outer chat scroll area handles
    // scrolling. The browser expands to fit its content via the size policy.
    m_contentBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_contentBrowser->setFrameShape(QFrame::NoFrame);
    m_contentBrowser->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_contentBrowser->setStyleSheet(
        "QTextBrowser { padding: 8px; border-radius: 8px; background-color: transparent; }");
    m_layout->addWidget(m_contentBrowser);

    m_attachmentLabel = new QLabel();
    m_attachmentLabel->setVisible(false);
    m_layout->addWidget(m_attachmentLabel);

    // Quick reply container
    m_quickReplyContainer = new QWidget();
    m_quickReplyLayout = new QHBoxLayout(m_quickReplyContainer);
    m_quickReplyLayout->setContentsMargins(0, 0, 0, 0);
    m_quickReplyLayout->setSpacing(4);
    m_quickReplyContainer->setVisible(false);
    m_layout->addWidget(m_quickReplyContainer);

    // Action buttons
    m_actionLayout = new QHBoxLayout();
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(4);
    m_layout->addLayout(m_actionLayout);
}

void ChatBubbleWidget::setMessage(const ChatMessage &message, int index)
{
    m_message = message;
    m_messageIndex = index;
    updateContent();
}

void ChatBubbleWidget::updateContent()
{
    // Status-hint messages are ephemeral step indicators ("Step 2/10 —
    // examining screen..."). Style them as a subtle centered divider rather
    // than a full chat bubble so they don't look like AI responses.
    if (m_message.isStatusHint) {
        m_headerWidget->setVisible(false);
        m_attachmentLabel->setVisible(false);
        m_quickReplyContainer->setVisible(false);
        // Clear action layout for status hints
        while (m_actionLayout->count() > 0) {
            QLayoutItem *item = m_actionLayout->takeAt(0);
            delete item;
        }
        m_layout->setContentsMargins(4, 1, 4, 1);
        m_layout->setSpacing(0);
        m_contentBrowser->setAlignment(Qt::AlignCenter);
        m_contentBrowser->setStyleSheet(
            "QTextBrowser { padding: 0px; border: none; "
            "background-color: transparent; color: #888; font-style: italic; "
            "font-size: 11px; }");
        m_contentBrowser->setPlainText(m_message.content);
        // Pin height to actual document content — QTextBrowser's default
        // sizeHint is much taller than a single line of text.
        // Add small padding to ensure no text clipping.
        m_contentBrowser->document()->adjustSize();
        m_contentBrowser->setFixedHeight(
            static_cast<int>(m_contentBrowser->document()->size().height()) + 4);
        return;
    }

    // Restore normal bubble layout for non-status-hint messages
    m_headerWidget->setVisible(true);
    m_layout->setContentsMargins(8, 4, 8, 4);
    m_layout->setSpacing(2);
    m_roleLabel->setVisible(true);
    m_contentBrowser->setAlignment(Qt::AlignLeft);

    // Role label + per-role content styling.
    // Assistant messages are rendered as markdown so headings, code blocks,
    // lists, and links display properly. User/System messages stay as plain
    // text since they're short and don't need rich formatting.
    QString bgStyle;
    switch (m_message.role) {
    case ChatRole::User:
        m_roleLabel->setText("You");
        bgStyle = "background-color: #d4e6ff; color: #000;";
        break;
    case ChatRole::Assistant:
        m_roleLabel->setText("AI Assistant");
        bgStyle = "background-color: #e8e8e8; color: #000;";
        break;
    case ChatRole::System:
        m_roleLabel->setText("System");
        bgStyle = "background-color: #fff3cd; color: #856404;";
        break;
    case ChatRole::Tool:
        m_roleLabel->setText("Tool Result");
        bgStyle = "background-color: #e8f4e8; color: #1e4620;";
        break;
    }

    m_contentBrowser->setStyleSheet(
        "QTextBrowser { padding: 8px; border-radius: 8px; " + bgStyle + " }");

    // Content — render markdown for user, assistant, and tool messages; system
    // messages stay as plain text (they're short status strings).
    // Tool-call JSON and TOOL_RESULT blocks are preprocessed into readable
    // markdown before rendering.
    QString displayContent = formatContentForDisplay(m_message.content);
    if (m_message.role == ChatRole::User || m_message.role == ChatRole::Assistant || m_message.role == ChatRole::Tool) {
        m_contentBrowser->setMarkdown(displayContent);
    } else {
        m_contentBrowser->setPlainText(displayContent);
    }
    // QTextBrowser needs an explicit height when its internal scrollbar is off
    // (so the outer chat scroll area handles scrolling). Compute from the
    // document's laid-out size and pin the widget to that height.
    // Set the text width first to ensure proper line wrapping calculation.
    m_contentBrowser->document()->setTextWidth(m_contentBrowser->viewport()->width());
    m_contentBrowser->document()->adjustSize();

    // Calculate height with very generous padding to prevent any text clipping.
    // Markdown rendering adds significant extra spacing for paragraphs, headings,
    // code blocks, and lists. Assistant messages especially need more room.
    // We add 60px: 16px CSS padding + 44px safety margin for markdown spacing.
    int docHeight = static_cast<int>(m_contentBrowser->document()->size().height());
    m_contentBrowser->setFixedHeight(docHeight + 60);

    // Attachment
    if (!m_message.attachmentFilePath.isEmpty()) {
        QPixmap pix(m_message.attachmentFilePath);
        if (!pix.isNull()) {
            int maxDim = 200;
            QPixmap scaled = pix.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_attachmentLabel->setPixmap(scaled);
            m_attachmentLabel->setCursor(Qt::PointingHandCursor);
            m_attachmentLabel->setToolTip("Click to view full size");
            m_attachmentLabel->setVisible(true);
        } else {
            m_attachmentLabel->setText(QString("[Attachment: %1]")
                .arg(QFileInfo(m_message.attachmentFilePath).fileName()));
            m_attachmentLabel->setVisible(true);
        }
    } else {
        m_attachmentLabel->setVisible(false);
    }

    // Quick replies
    clearQuickReplies();
    if (!m_message.quickReplies.isEmpty() && m_message.role == ChatRole::Assistant) {
        m_quickReplyContainer->setVisible(true);
        for (const auto &qr : m_message.quickReplies) {
            auto *chip = new QuickReplyWidget(qr.label, this);
            connect(chip, &QuickReplyWidget::clicked, this, [this, qr]() {
                emit quickReplyClicked(qr.sendText);
            });
            m_quickReplyLayout->addWidget(chip);
        }
    }

    // Guide action buttons
    // Clear previous action buttons
    QLayoutItem *item;
    while ((item = m_actionLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (!m_message.guideActionRect.isNull() || !m_message.guideShortcut.isEmpty()) {
        auto *execBtn = new QPushButton("Execute");
        auto *execNextBtn = new QPushButton("Execute & Next");
        auto *completeBtn = new QPushButton("I Did This");

        int idx = m_messageIndex;
        connect(execBtn, &QPushButton::clicked, this, [this, idx]() {
            emit guideExecuteClicked(idx);
        });
        connect(execNextBtn, &QPushButton::clicked, this, [this, idx]() {
            emit guideExecuteNextClicked(idx);
        });
        connect(completeBtn, &QPushButton::clicked, this, [this, idx]() {
            emit guideCompleteClicked(idx);
        });

        m_actionLayout->addWidget(execBtn);
        m_actionLayout->addWidget(execNextBtn);
        m_actionLayout->addWidget(completeBtn);
        m_actionLayout->addStretch();
    }

    // Copy button — show for assistant messages with content.
    // Click handler is connected in setupUI; here we just toggle visibility
    // and update what gets copied.
    if (m_message.role == ChatRole::Assistant && !m_message.content.isEmpty()) {
        m_copyBtn->setVisible(true);
        // Disconnect any previous connection, reconnect to this message's content
        disconnect(m_copyBtn, &QPushButton::clicked, nullptr, nullptr);
        connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_message.content);
        });
    } else {
        m_copyBtn->setVisible(false);
    }
}

void ChatBubbleWidget::clearQuickReplies()
{
    QLayoutItem *item;
    while ((item = m_quickReplyLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_quickReplyContainer->setVisible(false);
}

void ChatBubbleWidget::mousePressEvent(QMouseEvent *event)
{
    // Check if the click was on the attachment label
    if (m_attachmentLabel && m_attachmentLabel->isVisible() && !m_message.attachmentFilePath.isEmpty()) {
        QRect attachmentRect = m_attachmentLabel->geometry();
        if (attachmentRect.contains(event->pos())) {
            showFullImage();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ChatBubbleWidget::showFullImage()
{
    if (m_message.attachmentFilePath.isEmpty()) {
        return;
    }

    QPixmap fullImage(m_message.attachmentFilePath);
    if (fullImage.isNull()) {
        return;
    }

    // Create a dialog to show the full image
    QDialog dialog(this);
    dialog.setWindowTitle("Image Preview");
    dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowMaximizeButtonHint);

    // Create a scroll area to allow scrolling if the image is larger than the screen
    QScrollArea *scrollArea = new QScrollArea(&dialog);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setWidgetResizable(false);

    // Create a label to display the image
    QLabel *imageLabel = new QLabel();
    imageLabel->setPixmap(fullImage);
    imageLabel->setAlignment(Qt::AlignCenter);

    scrollArea->setWidget(imageLabel);

    // Set up the dialog layout
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);

    // Size the dialog to fit the screen while maintaining aspect ratio
    QScreen *screen = this->screen();
    if (screen) {
        QSize screenSize = screen->availableSize();
        int maxDim = qMin(screenSize.width(), screenSize.height()) - 100;

        // Scale the image to fit the screen if it's too large
        if (fullImage.width() > maxDim || fullImage.height() > maxDim) {
            QPixmap scaled = fullImage.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            imageLabel->setPixmap(scaled);
            dialog.resize(scaled.size() + QSize(20, 20));
        } else {
            dialog.resize(fullImage.size() + QSize(20, 20));
        }
    } else {
        dialog.resize(800, 600);
    }

    dialog.exec();
}

// ============================================================================
// Content formatting — convert tool-call JSON and TOOL_RESULT into readable
// markdown before rendering.
// ============================================================================

QString ChatBubbleWidget::formatContentForDisplay(const QString &content) const
{
    QString text = content;

    // --- TOOL_RESULT formatting ---
    // Tool results arrive as user messages like:
    //   "TOOL_RESULT [14:32:05]:\n<summary text>"
    // Optionally followed by:
    //   "--- OCR Analysis Result ---\n<markdown text>"
    // The OCR text is full markdown (headings, tables, etc.) and must be
    // rendered as markdown — NOT wrapped in a code block or blockquote.
    if (text.startsWith("TOOL_RESULT")) {
        QString timestamp;
        QString body = text;
        int tsStart = text.indexOf('[');
        int tsEnd = text.indexOf(']');
        if (tsStart >= 0 && tsEnd > tsStart) {
            timestamp = text.mid(tsStart + 1, tsEnd - tsStart - 1);
            body = text.mid(tsEnd + 1).trimmed();
            if (body.startsWith(':')) body = body.mid(1).trimmed();
        }

        QString header = timestamp.isEmpty()
            ? QStringLiteral("✅ **Tool Result**")
            : QStringLiteral("✅ **Tool Result** _(%1)_").arg(timestamp);

        // Split out the OCR section (if present)
        QString ocrSection;
        int ocrIdx = body.indexOf("--- OCR Analysis Result ---");
        if (ocrIdx >= 0) {
            ocrSection = body.mid(ocrIdx + strlen("--- OCR Analysis Result ---")).trimmed();
            body = body.left(ocrIdx).trimmed();
        }

        QString result;
        result += header + "\n\n";
        if (!body.isEmpty()) {
            // Render body as-is (may contain short status text).
            // Don't wrap in blockquote — that would break embedded markdown tables.
            result += body + "\n\n";
        }
        if (!ocrSection.isEmpty()) {
            // OCR output is full markdown (headings, tables, lists).
            // Render directly so tables and headings display correctly.
            // Only wrap in a code block if the content has no markdown syntax.
            bool hasMarkdown = ocrSection.contains('#')
                            || ocrSection.contains('|')
                            || ocrSection.contains("**")
                            || ocrSection.contains("- ");
            result += "**📝 OCR Output:**\n\n";
            if (hasMarkdown) {
                result += ocrSection + "\n";
            } else {
                result += "```\n" + ocrSection + "\n```\n";
            }
        }
        return result;
    }

    // --- Tool-call JSON formatting ---
    // Walk balanced {…} blocks looking for tool-call JSON. Replace each with
    // a formatted markdown card. Text around the JSON is preserved.
    QString result;
    int lastAppend = 0;

    for (int i = 0; i < text.length(); ++i) {
        if (text[i] != '{') continue;

        // Find matching closing brace
        int depth = 0;
        int end = -1;
        for (int j = i; j < text.length(); ++j) {
            if (text[j] == '{') depth++;
            else if (text[j] == '}') {
                depth--;
                if (depth == 0) { end = j; break; }
            }
        }
        if (end < 0) continue;

        QString candidate = text.mid(i, end - i + 1);
        if (!candidate.contains(QLatin1String("\"tool"))) continue;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            // Not valid JSON — skip past this block so nested braces aren't re-scanned
            i = end;
            continue;
        }

        QJsonObject root = doc.object();
        QList<QPair<QString, QJsonObject>> calls; // tool name -> args object

        if (root.contains("tool_calls") && root["tool_calls"].isArray()) {
            QJsonArray arr = root["tool_calls"].toArray();
            for (const auto &val : arr) {
                QJsonObject callObj = val.toObject();
                QString tool = callObj["tool"].toString();
                if (tool.isEmpty()) continue;
                calls.append({tool, callObj});
            }
        } else if (root.contains("tool")) {
            QString tool = root["tool"].toString();
            if (!tool.isEmpty()) {
                calls.append({tool, root});
            }
        }

        if (calls.isEmpty()) {
            i = end;
            continue;
        }

        // Append any text before this JSON block
        result += text.mid(lastAppend, i - lastAppend);

        // Collect all tool calls into a single table.
        // Rows: "tool_name | arg_key | arg_value". If a call has no args
        // (other than "tool"), emit one row with "(no arguments)".
        result += QStringLiteral("\n🔧 **Tool Call%1**\n\n").arg(calls.size() > 1 ? "s" : "");
        result += "| Tool | Argument | Value |\n|------|----------|-------|\n";
        for (const auto &call : calls) {
            QJsonObject args = call.second;
            bool hasArgs = false;
            for (auto it = args.begin(); it != args.end(); ++it) {
                QString key = it.key();
                if (key == "tool") continue;
                hasArgs = true;
                QString val;
                if (it.value().isString()) {
                    val = it.value().toString();
                    if (val.length() > 80) val = val.left(77) + "...";
                } else if (it.value().isDouble()) {
                    val = QString::number(it.value().toDouble());
                } else if (it.value().isBool()) {
                    val = it.value().toBool() ? "true" : "false";
                } else if (it.value().isNull()) {
                    val = "null";
                } else {
                    val = QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
                    if (val.length() > 80) val = val.left(77) + "...";
                }
                val.replace("|", "\\|");
                result += QStringLiteral("| `%1` | %2 | %3 |\n").arg(call.first, key, val);
            }
            if (!hasArgs) {
                result += QStringLiteral("| `%1` | _(no arguments)_ | |\n").arg(call.first);
            }
        }
        result += "\n";

        lastAppend = end + 1;
        i = end; // advance past this block
    }

    // Append trailing text
    result += text.mid(lastAppend);

    // Collapse excessive blank lines left by replacements
    while (result.contains("\n\n\n\n"))
        result.replace("\n\n\n\n", "\n\n\n");

    return result.trimmed();
}
