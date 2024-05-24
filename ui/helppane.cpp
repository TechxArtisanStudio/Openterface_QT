#include "helppane.h"
#include <QPainter>
#include <QColor>

HelpPane::HelpPane(QWidget *parent) : QWidget(parent)
{

}

void HelpPane::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QColor color("#222222");
    painter.fillRect(this->rect(), color);
    QPixmap pixmap(":/images/content_dark_eng.png"); // Replace with the path to your image in the resource file

    int paddingWidth = this->width() * 0.05;
    int paddingHeight = this->height() * 0.05;

    // Adjust the rectangle
    QRect paddedRect = this->rect().adjusted(paddingWidth, paddingHeight, -paddingWidth, -paddingHeight);

    painter.drawPixmap(paddedRect, pixmap);
}

