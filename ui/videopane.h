/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#ifndef VIDEOPANE_H
#define VIDEOPANE_H

#include "target/mouseeventdto.h"
#include "inputhandler.h"

#include <QtWidgets>
#include <QtMultimedia>
#include <QtMultimediaWidgets>


class VideoPane : public QGraphicsView
{
    Q_OBJECT

public:
    explicit VideoPane(QWidget *parent = nullptr);
    ~VideoPane();

    void showHostMouse();
    void hideHostMouse();
    void moveMouseToCenter();
    void startEscTimer();
    void stopEscTimer();

    bool focusNextPrevChild(bool next) override;

    bool isRelativeModeEnabled() const { return relativeModeEnable; }
    void setRelativeModeEnabled(bool enable) { relativeModeEnable = enable; }

    // QVideoWidget compatibility methods
    void setAspectRatioMode(Qt::AspectRatioMode mode);
    Qt::AspectRatioMode aspectRatioMode() const;
    
    // QGraphicsView enhancement methods
    void setVideoItem(QGraphicsVideoItem* videoItem);
    QGraphicsVideoItem* videoItem() const;
    QGraphicsVideoItem* getVideoItem() const { return m_videoItem; } // Convenience method for external access
    void resetZoom();
    void zoomIn(double factor = 1.25);
    void zoomOut(double factor = 0.8);
    void fitToWindow();
    void actualSize();
    
    // Direct GStreamer support methods (based on widgets_main.cpp approach)
    void enableDirectGStreamerMode(bool enable = true);
    bool isDirectGStreamerModeEnabled() const { return m_directGStreamerMode; }
    WId getVideoOverlayWindowId() const;
    void setupForGStreamerOverlay();
    QWidget* getOverlayWidget() const { return m_overlayWidget; }
        
    // FFmpeg direct video frame support
    void updateVideoFrame(const QPixmap& frame);
    void enableDirectFFmpegMode(bool enable = true);
    bool isDirectFFmpegModeEnabled() const { return m_directFFmpegMode; }

    // Mouse position transformation for InputHandler
    QPoint getTransformedMousePosition(const QPoint& viewportPos);
    
    // Debug helper to validate coordinate transformation consistency
    void validateMouseCoordinates(const QPoint& original, const QString& eventType);

signals:
    void mouseMoved(const QPoint& position, const QString& event);

public slots:
    void onCameraDeviceSwitching(const QString& fromDevice, const QString& toDevice);
    void onCameraDeviceSwitchComplete(const QString& device);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    int lastX=0;
    int lastY=0;
    bool relativeModeEnable;
    
    InputHandler *m_inputHandler;

    QTimer *escTimer;
    bool holdingEsc=false;
    
    // Frame preservation during camera switching
    QPixmap m_lastFrame;
    bool m_isCameraSwitching;
    
    // Graphics framework components
    QGraphicsScene *m_scene;
    QGraphicsVideoItem *m_videoItem;
    QGraphicsPixmapItem *m_pixmapItem;  // For displaying static frames
    
    // Aspect ratio and zoom control
    Qt::AspectRatioMode m_aspectRatioMode;
    double m_scaleFactor;
    QSize m_originalVideoSize;
    bool m_maintainAspectRatio;
    
    // Direct GStreamer mode support
    bool m_directGStreamerMode;
    QWidget* m_overlayWidget; // Widget for direct video overlay
    
    // Direct FFmpeg mode support
    bool m_directFFmpegMode;
    
    MouseEventDTO* calculateRelativePosition(QMouseEvent *event);
    MouseEventDTO* calculateAbsolutePosition(QMouseEvent *event);
    MouseEventDTO* calculateMouseEventDto(QMouseEvent *event);
    
    void captureCurrentFrame();
    void updateVideoItemTransform();
    void centerVideoItem();
    void setupScene();
};

#endif
