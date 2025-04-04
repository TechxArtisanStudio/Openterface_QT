#include "toggleswitch.h"

ToggleSwitch::ToggleSwitch(QWidget *parent, QColor barColor, QColor checkedColor,
                           QColor handleColor, float hScale, float vScale, int fontSize)
    : QCheckBox(parent),
      m_barBrush(barColor),
      m_barCheckedBrush(checkedColor),
      m_handleBrush(handleColor),
      m_handleCheckedBrush(checkedColor),
      m_handlePosition(0),
      m_hScale(hScale),
      m_vScale(vScale),
      m_fontSize(fontSize)
{
    setContentsMargins(7, 0, 7, 0);
    connect(this, &QCheckBox::stateChanged, this, &ToggleSwitch::handleStateChange);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(58, 45);
}

bool ToggleSwitch::hitButton(const QPoint &pos) const
{
    return contentsRect().contains(pos);
}

void ToggleSwitch::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect contRect = contentsRect();
    qreal width = contRect.width() * m_hScale;
    qreal height = contRect.height() * m_vScale;
    qreal handleRadius = qRound(0.24 * height);

    p.setPen(Qt::NoPen);
    QRectF barRect(0, 0, width - handleRadius, 0.50 * height);
    barRect.moveCenter(contRect.center());
    qreal rounding = barRect.height() / 2;

    qreal trailLength = contRect.width() * m_hScale - 2 * handleRadius;
    qreal xLeft = contRect.center().x() - (trailLength + handleRadius) / 2;
    qreal xPos = xLeft + handleRadius + trailLength * m_handlePosition - 3;

    if (isChecked()) {
        p.setBrush(m_barCheckedBrush);
        p.drawRoundedRect(barRect, rounding, rounding);

        p.setPen(Qt::black);
        p.setFont(QFont("Helvetica", m_fontSize, QFont::Bold));
        p.drawText(QRectF(xLeft, contRect.top(), trailLength, contRect.height()), Qt::AlignCenter, "Target");
    } else {
        p.setBrush(m_barBrush);
        p.drawRoundedRect(barRect, rounding, rounding);

        p.setPen(Qt::black);
        p.setFont(QFont("Helvetica", m_fontSize, QFont::Bold));
        p.drawText(QRectF(xLeft, contRect.top(), trailLength, contRect.height()), Qt::AlignCenter, "   Host");
    }

    // Use m_handleBrush for both checked and unchecked states
    p.setBrush(m_handleBrush);
    p.setPen(Qt::lightGray);
    p.drawEllipse(QPointF(xPos, barRect.center().y()), handleRadius, handleRadius);
}

void ToggleSwitch::handleStateChange(int value)
{
    m_handlePosition = value ? 1 : 0;
    update();
}

void ToggleSwitch::setHandlePosition(float pos)
{
    if (m_handlePosition != pos) {
        m_handlePosition = pos;
        emit handlePositionChanged(pos);
        update();
    }
}

void ToggleSwitch::setHScale(float value)
{
    if (m_hScale != value) {
        m_hScale = value;
        update();
    }
}

void ToggleSwitch::setVScale(float value)
{
    if (m_vScale != value) {
        m_vScale = value;
        update();
    }
}

void ToggleSwitch::setFontSize(int value)
{
    if (m_fontSize != value) {
        m_fontSize = value;
        update();
    }
}
