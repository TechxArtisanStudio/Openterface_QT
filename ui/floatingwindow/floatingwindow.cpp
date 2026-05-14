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

#include "floatingwindow.h"
#include <QApplication>
#include <QStyle>
#include <QFile>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QScreen>
#include <QShowEvent>
#include <QFocusEvent>

static constexpr int kButtonSize = 48;
static constexpr int kIconSize = 24;
static constexpr int kExplodeDuration = 250;
static constexpr int kMoveStepSmall = 15;
static constexpr int kMoveStepLarge = 50;
static constexpr int kAutoMoveInterval = 80;

FloatingWindow::FloatingWindow(QWidget *parent)
    : QWidget(parent)
    , m_expanded(false)
    , m_moveDelay(kMoveStepSmall)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::BypassWindowManagerHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    m_moveTimer = new QTimer(this);
    m_moveTimer->setSingleShot(false);

    setupUI();
    setupConnections();

    for (auto *btn : m_surroundButtons) {
        btn->hide();
    }
    adjustSize();
}

FloatingWindow::~FloatingWindow() = default;

void FloatingWindow::setupUI()
{
    m_gridLayout = new QGridLayout(this);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setSpacing(0);

    // Center button
    m_centerButton = new QPushButton();
    m_centerButton->setFixedSize(kButtonSize, kButtonSize);
    m_centerButton->setCursor(Qt::PointingHandCursor);
    m_centerButton->setToolTip(tr("Toggle controls"));
    setButtonIconFromText(m_centerButton, QString::fromUtf8("⊕"));
    m_gridLayout->addWidget(m_centerButton, 1, 1);

    // Layout:
    // (0,0)Zoom+  (0,1)Up  (0,2)Zoom-
    // (1,0)Left   (1,1)O   (1,2)Right
    // (2,0)Fit    (2,1)Down (2,2)Fullscreen

    // Row 0
    m_zoomInButton = new QPushButton();
    m_zoomInButton->setFixedSize(kButtonSize, kButtonSize);
    m_zoomInButton->setCursor(Qt::PointingHandCursor);
    m_zoomInButton->setToolTip(tr("Zoom In"));
    setButtonIconFromText(m_zoomInButton, QString::fromUtf8("＋"));
    m_gridLayout->addWidget(m_zoomInButton, 0, 0);

    m_moveUpButton = new QPushButton();
    m_moveUpButton->setFixedSize(kButtonSize, kButtonSize);
    m_moveUpButton->setCursor(Qt::PointingHandCursor);
    m_moveUpButton->setToolTip(tr("Move Up"));
    m_moveUpButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_moveUpButton->setIconSize(QSize(kIconSize, kIconSize));
    m_gridLayout->addWidget(m_moveUpButton, 0, 1);

    m_zoomOutButton = new QPushButton();
    m_zoomOutButton->setFixedSize(kButtonSize, kButtonSize);
    m_zoomOutButton->setCursor(Qt::PointingHandCursor);
    m_zoomOutButton->setToolTip(tr("Zoom Out"));
    setButtonIconFromText(m_zoomOutButton, QString::fromUtf8("－"));
    m_gridLayout->addWidget(m_zoomOutButton, 0, 2);

    // Row 1
    m_moveLeftButton = new QPushButton();
    m_moveLeftButton->setFixedSize(kButtonSize, kButtonSize);
    m_moveLeftButton->setCursor(Qt::PointingHandCursor);
    m_moveLeftButton->setToolTip(tr("Move Left"));
    m_moveLeftButton->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    m_moveLeftButton->setIconSize(QSize(kIconSize, kIconSize));
    m_gridLayout->addWidget(m_moveLeftButton, 1, 0);

    m_moveRightButton = new QPushButton();
    m_moveRightButton->setFixedSize(kButtonSize, kButtonSize);
    m_moveRightButton->setCursor(Qt::PointingHandCursor);
    m_moveRightButton->setToolTip(tr("Move Right"));
    m_moveRightButton->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_moveRightButton->setIconSize(QSize(kIconSize, kIconSize));
    m_gridLayout->addWidget(m_moveRightButton, 1, 2);

    // Row 2
    m_fitButton = new QPushButton();
    m_fitButton->setFixedSize(kButtonSize, kButtonSize);
    m_fitButton->setCursor(Qt::PointingHandCursor);
    m_fitButton->setToolTip(tr("Fit to Window"));
    setButtonIconFromText(m_fitButton, QString::fromUtf8("⛶"));
    m_gridLayout->addWidget(m_fitButton, 2, 0);

    m_moveDownButton = new QPushButton();
    m_moveDownButton->setFixedSize(kButtonSize, kButtonSize);
    m_moveDownButton->setCursor(Qt::PointingHandCursor);
    m_moveDownButton->setToolTip(tr("Move Down"));
    m_moveDownButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_moveDownButton->setIconSize(QSize(kIconSize, kIconSize));
    m_gridLayout->addWidget(m_moveDownButton, 2, 1);

    m_fullscreenButton = new QPushButton();
    m_fullscreenButton->setFixedSize(kButtonSize, kButtonSize);
    m_fullscreenButton->setCursor(Qt::PointingHandCursor);
    m_fullscreenButton->setToolTip(tr("Fullscreen"));
    setButtonIconFromSvg(m_fullscreenButton, ":/images/fullscreen.svg");
    m_gridLayout->addWidget(m_fullscreenButton, 2, 2);

    // Collect surround buttons
    m_surroundButtons = { m_zoomInButton, m_zoomOutButton, m_moveUpButton,
                          m_moveDownButton, m_moveLeftButton, m_moveRightButton,
                          m_fullscreenButton, m_fitButton };

    // Create opacity effects
    for (auto *btn : m_surroundButtons) {
        auto *effect = new QGraphicsOpacityEffect(btn);
        effect->setOpacity(1.0);
        btn->setGraphicsEffect(effect);
        m_opacityEffects.append(effect);
        installDragFilter(btn);
    }

    installDragFilter(m_centerButton);
}

void FloatingWindow::setupConnections()
{
    connect(m_centerButton, &QPushButton::clicked, this, &FloatingWindow::toggleExpanded);

    connect(m_zoomInButton, &QPushButton::clicked, this, &FloatingWindow::zoomInRequested);
    connect(m_zoomOutButton, &QPushButton::clicked, this, &FloatingWindow::zoomOutRequested);
    connect(m_fullscreenButton, &QPushButton::clicked, this, &FloatingWindow::fullscreenRequested);
    connect(m_fitButton, &QPushButton::clicked, this, &FloatingWindow::fitToWindowRequested);

    // Move buttons: press for single small step, hold for continuous fast moves
    auto setupMoveBtn = [this](QPushButton *btn, void (FloatingWindow::*sig)()) {
        connect(btn, &QPushButton::pressed, this, [this, btn, sig]() {
            (this->*sig)();  // immediate small move
            startAutoMove(btn, sig);
        });
        connect(btn, &QPushButton::released, this, [this]() {
            stopAutoMove(nullptr);
        });
        connect(btn, &QPushButton::clicked, this, [this]() {
            stopAutoMove(nullptr);
        });
    };

    setupMoveBtn(m_moveUpButton, &FloatingWindow::moveUpRequested);
    setupMoveBtn(m_moveDownButton, &FloatingWindow::moveDownRequested);
    setupMoveBtn(m_moveLeftButton, &FloatingWindow::moveLeftRequested);
    setupMoveBtn(m_moveRightButton, &FloatingWindow::moveRightRequested);

    connect(m_moveTimer, &QTimer::timeout, this, [this]() {
        if (m_moveAction) {
            (this->*m_moveAction)();
        }
    });
}

void FloatingWindow::setButtonIconFromSvg(QPushButton *button, const QString &iconPath, int size)
{
    QFile svgFile(iconPath);
    if (!svgFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open SVG resource:" << iconPath;
        button->setText("FS");
        return;
    }

    QByteArray svgData = svgFile.readAll();
    svgFile.close();

    QSvgRenderer svgRenderer(svgData);
    if (!svgRenderer.isValid()) {
        qWarning() << "Failed to parse SVG:" << iconPath;
        button->setText("FS");
        return;
    }

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    svgRenderer.render(&painter);
    painter.end();

    QIcon icon(pixmap);
    button->setIcon(icon);
    button->setIconSize(QSize(size, size));
}

void FloatingWindow::setButtonIconFromText(QPushButton *button, const QString &text)
{
    QFont font = button->font();
    font.setPixelSize(kIconSize);
    font.setBold(true);
    button->setFont(font);
    button->setText(text);
}

void FloatingWindow::installDragFilter(QWidget *widget)
{
    widget->installEventFilter(this);
}

bool FloatingWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragStartPos = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        break;
    }
    case QEvent::MouseMove: {
        if (m_dragging) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            move(mouseEvent->globalPosition().toPoint() - m_dragStartPos);
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        m_dragging = false;
        emit dragEnded();
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void FloatingWindow::toggleExpanded()
{
    m_expanded = !m_expanded;
    if (m_expanded) {
        animateExpand();
    } else {
        animateCollapse();
    }
}

void FloatingWindow::animateExpand()
{
    for (auto *eff : m_opacityEffects) {
        eff->setOpacity(0.0);
    }
    for (auto *btn : m_surroundButtons) {
        btn->show();
    }
    adjustSize();

    auto *anim = new QParallelAnimationGroup(this);
    for (auto *eff : m_opacityEffects) {
        auto *opAnim = new QPropertyAnimation(eff, "opacity");
        opAnim->setDuration(kExplodeDuration);
        opAnim->setStartValue(0.0);
        opAnim->setEndValue(1.0);
        opAnim->setEasingCurve(QEasingCurve::OutCubic);
        anim->addAnimation(opAnim);
    }
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FloatingWindow::animateCollapse()
{
    auto *anim = new QParallelAnimationGroup(this);
    for (auto *eff : m_opacityEffects) {
        auto *opAnim = new QPropertyAnimation(eff, "opacity");
        opAnim->setDuration(kExplodeDuration);
        opAnim->setStartValue(1.0);
        opAnim->setEndValue(0.0);
        opAnim->setEasingCurve(QEasingCurve::InCubic);
        anim->addAnimation(opAnim);
    }
    connect(anim, &QAbstractAnimation::finished, this, [this]() {
        for (auto *btn : m_surroundButtons) {
            btn->hide();
        }
        adjustSize();
    }, Qt::SingleShotConnection);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FloatingWindow::startAutoMove(QPushButton *btn, void (FloatingWindow::*sig)())
{
    stopAutoMove(btn);
    m_moveAction = sig;
    m_moveActive = true;
    m_moveTimer->start(kAutoMoveInterval);
}

void FloatingWindow::stopAutoMove(QPushButton *btn)
{
    if (m_moveTimer->isActive()) {
        m_moveTimer->stop();
    }
    m_moveAction = nullptr;
    m_moveActive = false;
}

void FloatingWindow::clampToScreen()
{
    QScreen *screen = this->screen();
    if (!screen) screen = QApplication::primaryScreen();
    QRect geo = screen->availableGeometry();
    QRect winGeo = geometry();

    int x = winGeo.x();
    int y = winGeo.y();

    if (x < geo.left()) x = geo.left();
    if (y < geo.top()) y = geo.top();
    if (x + winGeo.width() > geo.right()) x = geo.right() - winGeo.width();
    if (y + winGeo.height() > geo.bottom()) y = geo.bottom() - winGeo.height();

    move(x, y);
}

void FloatingWindow::repositionNearMainWindow(const QRect &mainGeometry)
{
    QRect screen = this->screen()->availableGeometry();
    int targetX = mainGeometry.right() + 10;
    int targetY = mainGeometry.top() + 10;

    // Ensure within screen bounds
    if (targetX + width() > screen.right()) {
        targetX = mainGeometry.left() - width() - 10;
    }
    if (targetX < screen.left()) {
        targetX = screen.right() - width() - 10;
    }
    if (targetY + height() > screen.bottom()) {
        targetY = screen.bottom() - height() - 10;
    }
    if (targetY < screen.top()) {
        targetY = screen.top();
    }

    move(targetX, targetY);
}

void FloatingWindow::setWindowOpacityValue(double opacity)
{
    setWindowOpacity(opacity);
}

void FloatingWindow::moveToTopRight()
{
    QScreen *screen = this->screen();
    if (!screen) screen = QApplication::primaryScreen();
    QRect geo = screen->availableGeometry();

    int x = geo.right() - width() - 10;
    int y = geo.top() + 10;

    if (x < geo.left()) x = geo.left() + 10;
    if (y < geo.top()) y = geo.top() + 10;

    move(x, y);
}

void FloatingWindow::positionAtVideoPaneTopRight(QWidget *videoPane)
{
    QPoint videoPaneGlobal = videoPane->mapToGlobal(QPoint(0, 0));
    int x = videoPaneGlobal.x() + videoPane->width() - width() - 10;
    int y = videoPaneGlobal.y() + 10;
    move(x, y);
}

void FloatingWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    raise();
}

void FloatingWindow::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    raise();
}
