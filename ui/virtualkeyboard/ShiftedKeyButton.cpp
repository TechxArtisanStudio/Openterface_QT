#include "ShiftedKeyButton.h"
#include <QFont>
#include <QFontDatabase>

ShiftedKeyButton::ShiftedKeyButton(const QString& baseChar, const QString& shiftedChar, int keyCode, QWidget* parent)
    : QWidget(parent)
    , m_keyCode(keyCode)
{
    setFixedHeight(40);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Create the main button (invisible, just for click handling and styling)
    m_button = new QPushButton(this);
    m_button->setFlat(true);
    m_button->setStyleSheet(
        "QPushButton { "
        "   border: 1px solid #999; border-radius: 4px; "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "       stop:0 #ffffff, stop:0.5 #f5f5f5, stop:1 #e8e8e8); "
        "   color: transparent; padding: 4px 6px; margin: 1px; "
        "   font-size: 11px; font-weight: 500; "
        "} "
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f0f0f0, stop:0.5 #e8e8e8, stop:1 #d8d8d8); } "
        "QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d0d0d0, stop:0.5 #c0c0c0, stop:1 #b0b0b0); }"
    );
    m_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_button->setGeometry(0, 0, width(), height());

    // Create label for base character (centered)
    m_baseLabel = new QLabel(baseChar, this);
    m_baseLabel->setAlignment(Qt::AlignCenter);
    m_baseLabel->setFrameShape(QFrame::NoFrame);
    m_baseLabel->setStyleSheet("background: transparent; border: none; color: #222; font-size: 11px; font-weight: 500;");
    m_baseLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Create label for shifted character (top-right)
    m_shiftedLabel = new QLabel(shiftedChar, this);
    m_shiftedLabel->setAlignment(Qt::AlignCenter);
    m_shiftedLabel->setFrameShape(QFrame::NoFrame);
    m_shiftedLabel->setStyleSheet("background: transparent; border: none; color: #222; font-size: 8px; font-weight: 500;");
    m_shiftedLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Set font that supports Japanese characters on labels
    QFont labelFont;
    QStringList japaneseFontFamilies = {
        "Noto Sans CJK JP",
        "Takao Gothic",
        "IPAGothic",
        "Droid Sans Japanese",
        "Noto Sans Mono CJK JP",
        "Droid Sans Fallback"
    };

    for (const QString& family : japaneseFontFamilies) {
        if (QFontDatabase::families().contains(family)) {
            labelFont = QFont(family);
            break;
        }
    }

    if (!labelFont.family().isEmpty()) {
        m_baseLabel->setFont(labelFont);
        m_shiftedLabel->setFont(labelFont);
    }

    // Set the keyCode property so it can be retrieved by onKeyClicked
    setProperty("keyCode", keyCode);

    // Connect button click to our signal
    connect(m_button, &QPushButton::clicked, this, &ShiftedKeyButton::clicked);
}

void ShiftedKeyButton::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Position the button to fill the widget
    m_button->setGeometry(0, 0, width(), height());

    // Position the base label in the center
    int baseLabelWidth = width() * 0.7;
    int baseLabelHeight = height() * 0.6;
    int baseLabelX = (width() - baseLabelWidth) / 2;
    int baseLabelY = (height() - baseLabelHeight) / 2 + 5; // Slightly lower to make room for shifted char
    m_baseLabel->setGeometry(baseLabelX, baseLabelY, baseLabelWidth, baseLabelHeight);

    // Position the shifted label at top-right
    int shiftedLabelWidth = width() * 0.4;
    int shiftedLabelHeight = height() * 0.4;
    int shiftedLabelX = width() - shiftedLabelWidth - 2;
    int shiftedLabelY = 2;
    m_shiftedLabel->setGeometry(shiftedLabelX, shiftedLabelY, shiftedLabelWidth, shiftedLabelHeight);
}

void ShiftedKeyButton::mousePressEvent(QMouseEvent* event)
{
    m_button->setDown(true);
    QWidget::mousePressEvent(event);
}

void ShiftedKeyButton::mouseReleaseEvent(QMouseEvent* event)
{
    m_button->setDown(false);
    QWidget::mouseReleaseEvent(event);
}

void ShiftedKeyButton::setCharacters(const QString& baseChar, const QString& shiftedChar)
{
    if (m_baseLabel) {
        m_baseLabel->setText(baseChar);
    }
    if (m_shiftedLabel) {
        m_shiftedLabel->setText(shiftedChar);
    }
}