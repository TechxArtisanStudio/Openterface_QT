#ifndef VIRTUALKEYBOARDWIDGET_H
#define VIRTUALKEYBOARDWIDGET_H

#include <QWidget>
#include <QKeyEvent>
#include <QDebug>

class VirtualKeyboardWidget : public QWidget {
    Q_OBJECT

public:
    explicit VirtualKeyboardWidget(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
};

#endif // VIRTUALKEYBOARDWIDGET_H