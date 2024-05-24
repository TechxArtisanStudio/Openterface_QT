#include <QVideoWidget>
#include <QMouseEvent>
#include "host/HostManager.h"
#include "global.h"

class VideoPane : public QVideoWidget
{
    Q_OBJECT

public:
    explicit VideoPane(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QPoint calculateRelativePosition(QMouseEvent *event);
    int getMouseButton(QMouseEvent *event);
    int lastMouseButton = 0;
    bool isDragging = false;
    int lastX=0;
    int lastY=0;
};
