
#include "virtualkeyboardwidget.h"
#include <QInputMethod>
#include <QGuiApplication>

VirtualKeyboardWidget::VirtualKeyboardWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void VirtualKeyboardWidget::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "Key pressed:" << event->text();
    QWidget::keyPressEvent(event);
}

void VirtualKeyboardWidget::focusInEvent(QFocusEvent *event)
{
    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    inputMethod->show();
    qDebug() << "VirtualKeyboardWidget focused, virtual keyboard shown";
    QWidget::focusInEvent(event);
}

void VirtualKeyboardWidget::focusOutEvent(QFocusEvent *event)
{
    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    inputMethod->hide();
    qDebug() << "VirtualKeyboardWidget lost focus, virtual keyboard hidden";
    QWidget::focusOutEvent(event);
}
