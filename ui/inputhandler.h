#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>

class InputHandler : public QObject
{
    Q_OBJECT

public:
    explicit InputHandler(QObject *parent = nullptr);

    void handleKeyPress(QKeyEvent *event);
    void handleKeyRelease(QKeyEvent *event);
    void handleMousePress(QMouseEvent *event);
    void handleMouseRelease(QMouseEvent *event);
    void handleMouseMove(QMouseEvent *event);

signals:
    void keyPressed(int key);
    void keyReleased(int key);
    void mousePressed(Qt::MouseButton button, const QPoint &pos);
    void mouseReleased(Qt::MouseButton button, const QPoint &pos);
    void mouseMoved(const QPoint &pos);

private:
    // Add any private members if needed
};

#endif // INPUTHANDLER_H