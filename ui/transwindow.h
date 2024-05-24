#ifndef TRANSWINDOWS_H
#define TRANSWINDOWS_H

#include <QDialog>
#include <QTimer>
#include <QMouseEvent>
#include <QRect>

class TransWindow: public QDialog
{
    Q_OBJECT

public:
    explicit TransWindow(QWidget *parent=0);

    ~TransWindow();

    void updateGeometry(QRect *videoPaneGeometry);

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;


private:
    QTimer *escTimer;
    bool holdingEsc=false;
};

#endif // TRANSPARENT_H
