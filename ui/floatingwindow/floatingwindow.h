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
*    This program is distributed in the hope that it will be useful, but     *    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#ifndef FLOATINGWINDOW_H
#define FLOATINGWINDOW_H

#include <QWidget>
#include <QPushButton>
#include <QGridLayout>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QShowEvent>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QTimer>

class FloatingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FloatingWindow(QWidget *parent = nullptr);
    ~FloatingWindow() override;

signals:
    void zoomInRequested();
    void zoomOutRequested();
    void moveUpRequested();
    void moveDownRequested();
    void moveLeftRequested();
    void moveRightRequested();
    void fullscreenRequested();
    void fitToWindowRequested();
    void dragEnded();

public:
    void clampToScreen();
    void repositionNearMainWindow(const QRect &mainGeometry);
    void setWindowOpacityValue(double opacity);
    void moveToTopRight();
    void positionAtVideoPaneTopRight(QWidget *videoPane);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void setupUI();
    void setupConnections();
    void setButtonIconFromSvg(QPushButton *button, const QString &iconPath, int size = 32);
    void setButtonIconFromText(QPushButton *button, const QString &text);
    void installDragFilter(QWidget *widget);
    void toggleExpanded();
    void animateExpand();
    void animateCollapse();
    void startAutoMove(QPushButton *btn, void (FloatingWindow::*signal)());
    void stopAutoMove(QPushButton *btn);

    // Center button (always visible, acts as toggle)
    QPushButton *m_centerButton;

    // Surrounding buttons
    QPushButton *m_zoomInButton;
    QPushButton *m_zoomOutButton;
    QPushButton *m_moveUpButton;
    QPushButton *m_moveDownButton;
    QPushButton *m_moveLeftButton;
    QPushButton *m_moveRightButton;
    QPushButton *m_fullscreenButton;
    QPushButton *m_fitButton;

    QGridLayout *m_gridLayout;
    QList<QPushButton *> m_surroundButtons;
    QList<QGraphicsOpacityEffect *> m_opacityEffects;

    // Auto-move timers for hold-to-repeat
    QTimer *m_moveTimer = nullptr;
    void (FloatingWindow::*m_moveAction)() = nullptr;
    bool m_moveActive = false;
    int m_moveDelay;

    bool m_expanded;
    bool m_dragging = false;
    QPoint m_dragStartPos;
};

#endif // FLOATINGWINDOW_H
