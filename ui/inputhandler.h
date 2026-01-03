#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include "target/mouseeventdto.h"

class VideoPane; // Forward declaration

class InputHandler : public QObject
{
    Q_OBJECT

public:
    explicit InputHandler(VideoPane *videoPane, QObject *parent = nullptr);
    ~InputHandler();

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
    QPointer<VideoPane> m_videoPane;  // Use QPointer for automatic null safety
    int lastX = 0;
    int lastY = 0;
    int lastMouseButton = 0;
    bool m_isDragging = false;
    bool m_holdingEsc = false;
    bool m_processingEnabled = true;  // Safety flag to disable event processing
    QPointer<QWidget> m_currentEventTarget;  // Use QPointer for safety

    // Mouse move timer for smooth event processing
    QTimer* m_mouseMoveTimer = nullptr;
    QMouseEvent* m_pendingMouseMoveEvent = nullptr;
    int m_mouseMoveInterval = 8;  // 8ms = ~125 FPS limit for responsiveness
    int m_droppedMouseEvents = 0;
    
    // Duplicate event filtering (Qt sometimes sends duplicate press events)
    qint64 m_lastMousePressTime = 0;
    QPoint m_lastMousePressPos;
    Qt::MouseButton m_lastPressButton = Qt::NoButton;
    
    // Flag to prevent double-processing when eventFilter handles events
    bool m_processingInEventFilter = false;
    
    // Cache the last calculated absolute position to reuse on press/release
    // This ensures press happens at the exact same coordinates as the last move
    int m_lastAbsoluteX = 0;
    int m_lastAbsoluteY = 0;
    bool m_hasLastAbsolutePosition = false;
    
    // Cache the last sent move position to use for press/release in absolute mode
    int m_lastMoveAbsX = 0;
    int m_lastMoveAbsY = 0;
    
    // Cache for double-click: Store coordinates from first press to reuse on second press
    int m_doubleClickCachedX = 0;
    int m_doubleClickCachedY = 0;
    bool m_hasDoubleClickCache = false;
    qint64 m_doubleClickCacheTime = 0;

    MouseEventDTO* calculateRelativePosition(QMouseEvent *event);
    MouseEventDTO* calculateAbsolutePosition(QMouseEvent *event);

    QSize getScreenResolution();

    void handleMouseMoveEvent(QMouseEvent *event);
    void handleMousePressEvent(QMouseEvent *event);
    void handleMouseReleaseEvent(QMouseEvent *event);
    
    // Helper methods for coordinate transformation
    QPoint transformMousePosition(QMouseEvent *event, QWidget* sourceWidget);
    QWidget* getEffectiveVideoWidget() const;
    
    // Timer slot for processing pending mouse move
    void processPendingMouseMove();
};

#endif // INPUTHANDLER_H