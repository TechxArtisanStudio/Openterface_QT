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

    // Mouse throttling configuration and statistics
    void setMouseMoveInterval(int intervalMs) { m_mouseMoveInterval = qMax(17, intervalMs); } // Minimum 60 FPS
    int getMouseMoveInterval() const { return m_mouseMoveInterval; }
    int getDroppedMouseEvents() const { return m_droppedMouseEvents; }
    void resetThrottlingStats() { m_droppedMouseEvents = 0; }
    
    struct ThrottlingStats {
        int droppedEvents;
        int currentInterval;
        double effectiveFPS;
    };
    ThrottlingStats getThrottlingStats() const {
        return {m_droppedMouseEvents, m_mouseMoveInterval, 1000.0 / m_mouseMoveInterval};
    }

    void handleKeyPressEvent(QKeyEvent *event);
    void handleKeyReleaseEvent(QKeyEvent *event);
    void handleWheelEvent(QWheelEvent *event);
    
    // Methods to handle GStreamer overlay widget events
    void updateEventFilterTarget();
    void installOverlayEventFilter(QWidget* overlayWidget);
    void removeOverlayEventFilter();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    VideoPane *m_videoPane;
    int lastX = 0;
    int lastY = 0;
    int lastMouseButton = 0;
    bool m_isDragging = false;
    bool m_holdingEsc = false;
    QWidget* m_currentEventTarget = nullptr;  // Track current event filter target

    // Mouse throttling for performance optimization (60 FPS limit)
    qint64 m_lastMouseMoveTime = 0;
    int m_mouseMoveInterval = 17;  // 17ms = 60 FPS limit
    int m_droppedMouseEvents = 0;

    MouseEventDTO* calculateRelativePosition(QMouseEvent *event);
    MouseEventDTO* calculateAbsolutePosition(QMouseEvent *event);

    QSize getScreenResolution();

    void handleMouseMoveEvent(QMouseEvent *event);
    void handleMousePressEvent(QMouseEvent *event);
    void handleMouseReleaseEvent(QMouseEvent *event);
    
    // Helper methods for coordinate transformation
    QPoint transformMousePosition(QMouseEvent *event, QWidget* sourceWidget);
    QWidget* getEffectiveVideoWidget() const;
};

#endif // INPUTHANDLER_H