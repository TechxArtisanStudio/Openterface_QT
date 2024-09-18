#ifndef TOGGLESWITCH_H
#define TOGGLESWITCH_H

#include <QCheckBox>
#include <QPainter>
#include <QPropertyAnimation>

class ToggleSwitch : public QCheckBox
{
    Q_OBJECT
    Q_PROPERTY(float handlePosition READ handlePosition WRITE setHandlePosition NOTIFY handlePositionChanged)

public:
    explicit ToggleSwitch(QWidget *parent = nullptr,
                          QColor barColor = QColor(242, 145, 58),      // Replace "#F2913A"
                          QColor checkedColor = QColor(242, 145, 58),      // Replace "#F2913A"
                          QColor handleColor = QColor(252, 241, 230),
                          float hScale = 1.0f,
                          float vScale = 1.1f,
                          int fontSize = 9);

    QSize sizeHint() const override;
    bool hitButton(const QPoint &pos) const override;

    float handlePosition() const { return m_handlePosition; }
    void setHandlePosition(float pos);

    void setHScale(float value);
    void setVScale(float value);
    void setFontSize(int value);

signals:
    void handlePositionChanged(float position);

protected:
    void paintEvent(QPaintEvent *e) override;

private slots:
    void handleStateChange(int value);

private:
    QBrush m_barBrush;
    QBrush m_barCheckedBrush;
    QBrush m_handleBrush;
    QBrush m_handleCheckedBrush;
    float m_handlePosition;
    float m_hScale;
    float m_vScale;
    int m_fontSize;
};

#endif // TOGGLESWITCH_H
