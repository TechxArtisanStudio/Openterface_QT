#include "scriptEditor.h"
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QScrollBar>
#include <QGuiApplication>

ScriptEditor::ScriptEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setFont(QFont("Courier", 10));
    setLineWrapMode(QTextEdit::NoWrap);

    lineNumberArea = new LineNumberArea(this);
    connect(document(), &QTextDocument::contentsChanged, this, &ScriptEditor::updateLineNumberAreaWidth);
    updateLineNumberAreaWidth();
}

ScriptEditor::~ScriptEditor()
{
}

int ScriptEditor::lineNumberAreaWidth()
{
    int lineCount = document()->blockCount();  // Get line count directly from QTextDocument

    int digits = 1;
    int max = qMax(1, lineCount);
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void ScriptEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void ScriptEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void ScriptEditor::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void ScriptEditor::scrollContentsBy(int dx, int dy)
{
    QTextEdit::scrollContentsBy(dx, dy);
    if (dy != 0) {
        updateLineNumberArea(QRect(), dy);
    }
}

void ScriptEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    
    QPalette palette = QGuiApplication::palette();
    QColor bgColor = palette.color(QPalette::Window);
    painter.fillRect(event->rect(), bgColor);

    int scrollOffset = verticalScrollBar()->value();
    int lineHeight = fontMetrics().height();

    // Calculate the first visible line based on scroll position
    QTextBlock block = document()->begin();
    int blockNumber = 0;
    int top = 0 - scrollOffset + 4;  // Adjust for scroll position
    
    while (block.isValid() && top <= event->rect().bottom()) {
        if (top + lineHeight >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            
            // If it's the highlighted line, draw background color
            if (blockNumber + 1 == highlightedLineNumber) {
                painter.fillRect(0, top, lineNumberArea->width(), lineHeight, Qt::yellow);
                painter.setPen(Qt::black);
            } else {
                painter.setPen(palette.color(QPalette::WindowText));
            }
            
            painter.drawText(0, top, lineNumberArea->width(), lineHeight, 
                           Qt::AlignRight, number);
        }

        block = block.next();
        top += lineHeight;
        ++blockNumber;
    }
}
void ScriptEditor::highlightLine(int lineNumber)
{
    QTextBlock block = document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;

    highlightedLineNumber = lineNumber;  // Update highlighted line number
    lineNumberArea->update();  // Trigger redraw

    QTextCursor cursor = textCursor();
    cursor.setPosition(block.position());
    setTextCursor(cursor);
    ensureCursorVisible();
}

void ScriptEditor::resetHighlightLine(int lineNumber)
{
    if (highlightedLineNumber == lineNumber) {
        highlightedLineNumber = -1;  // Reset highlighted line
        lineNumberArea->update();  // Trigger redraw
    }
}
