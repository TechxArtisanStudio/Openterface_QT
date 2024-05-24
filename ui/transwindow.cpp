#include "transwindow.h"
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QRegion>
#include <QEvent>

TransWindow::TransWindow(QWidget *parent) :
    QDialog(parent),
    escTimer(new QTimer(this))
{
    this->setMouseTracking(true);
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Set up the timer
    connect(escTimer, &QTimer::timeout, this, &TransWindow::close);
}

TransWindow::~TransWindow()
{
}

void TransWindow::updateGeometry(QRect *geometry)
{


    this->setWindowOpacity(0.5);
    QRegion region(this->geometry());
    qDebug() << "geometry:  " << region << " mask: " << geometry;
    // Subtract the entire window area from the region
    region = region.subtracted(*geometry);

    // Set the mask
    this->setMask(region);
}

void TransWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Handle mouse move event here
    // For example, you can print the mouse position:
    qDebug() << "Transparent Window mouse moved to position:" << event->pos();
}

void TransWindow::keyPressEvent(QKeyEvent *event)
{

    if(!holdingEsc && event->key() == Qt::Key_Escape) {
        qDebug() << "Esc Pressed, timer started";
        holdingEsc = true;
        escTimer->start(500); // 0.5 seconds
    }
}

void TransWindow::keyReleaseEvent(QKeyEvent *event)
{
    if(holdingEsc && event->key() == Qt::Key_Escape) {
        qDebug() << "Esc Released, timer stop";
        escTimer->stop();
        holdingEsc = false;
    }
}


void TransWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange) {
        if (this->isActiveWindow()) {
            qDebug() << "Window activated";
        } else {
            qDebug() << "Window deactivated";
            this->activateWindow();
        }
    }
}
