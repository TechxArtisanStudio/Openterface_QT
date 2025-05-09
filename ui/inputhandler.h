#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include "target/mouseeventdto.h"

class VideoPane; // Forward declaration

class InputHandler : public QObject
{
    Q_OBJECT

public:
    explicit InputHandler(VideoPane *videoPane, QObject *parent = nullptr);

    void handleKeyPress(QKeyEvent *event);
    void handleKeyRelease(QKeyEvent *event);
    void handleMousePress(QMouseEvent *event);
    void handleMouseRelease(QMouseEvent *event);
    void handleMouseMove(QMouseEvent *event);
    MouseEventDTO* calculateMouseEventDto(QMouseEvent *event);
    void setDragging(bool m_isDragging); 
    bool isDragging() const { return m_isDragging; }
    int getMouseButton(QMouseEvent *event);

    void handleKeyPressEvent(QKeyEvent *event);
    void handleKeyReleaseEvent(QKeyEvent *event);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    VideoPane *m_videoPane;
    int lastX = 0;
    int lastY = 0;
    int lastMouseButton = 0;
    bool m_isDragging = false;
    bool m_holdingEsc = false;

    MouseEventDTO* calculateRelativePosition(QMouseEvent *event);
    MouseEventDTO* calculateAbsolutePosition(QMouseEvent *event);

    QSize getScreenResolution();

    void handleMouseMoveEvent(QMouseEvent *event);
    void handleMousePressEvent(QMouseEvent *event);
    void handleMouseReleaseEvent(QMouseEvent *event);
    void handleWheelEvent(QWheelEvent *event);
};

#endif // INPUTHANDLER_H