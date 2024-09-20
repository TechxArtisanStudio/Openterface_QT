#include "inputhandler.h"

InputHandler::InputHandler(QObject *parent)
    : QObject(parent)
{
}

void InputHandler::handleKeyPress(QKeyEvent *event)
{
    emit keyPressed(event->key());
}

void InputHandler::handleKeyRelease(QKeyEvent *event)
{
    emit keyReleased(event->key());
}

void InputHandler::handleMousePress(QMouseEvent *event)
{
    emit mousePressed(event->button(), event->pos());
}

void InputHandler::handleMouseRelease(QMouseEvent *event)
{
    emit mouseReleased(event->button(), event->pos());
}

void InputHandler::handleMouseMove(QMouseEvent *event)
{
    emit mouseMoved(event->pos());
}