#ifndef QUICK_REPLY_WIDGET_H
#define QUICK_REPLY_WIDGET_H

#include <QPushButton>

/**
 * @brief Tappable chip for quick reply suggestions.
 */
class QuickReplyWidget : public QPushButton
{
    Q_OBJECT

public:
    explicit QuickReplyWidget(const QString &label, QWidget *parent = nullptr)
        : QPushButton(label, parent)
    {
        setStyleSheet(
            "QPushButton { padding: 4px 10px; border-radius: 12px; "
            "background-color: #e0e0e0; border: 1px solid #ccc; font-size: 11px; }"
            "QPushButton:hover { background-color: #c0c0c0; }"
        );
        setCursor(Qt::PointingHandCursor);
    }
};

#endif // QUICK_REPLY_WIDGET_H
