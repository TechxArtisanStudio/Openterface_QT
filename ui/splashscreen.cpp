#include "splashscreen.h"
#include <QApplication>
#include <QScreen>
#include <QFont>
#include <QPainter>

SplashScreen::SplashScreen(const QPixmap &pixmap, Qt::WindowFlags f)
    : QSplashScreen(pixmap, f)
    , m_dotCount(0)
    , m_baseMessage("Loading")
{
    // Set window flags for proper display on all platforms
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    
    // Set the font for messages - larger and bold for visibility
    QFont font("Arial", 16);
    setFont(font);
    
    // Initialize the loading animation timer
    m_loadingTimer = new QTimer(this);
    connect(m_loadingTimer, &QTimer::timeout, this, &SplashScreen::updateLoadingDots);
}

SplashScreen::~SplashScreen()
{
    if (m_loadingTimer) {
        m_loadingTimer->stop();
    }
}

void SplashScreen::showLoadingMessage()
{
    m_dotCount = 0;
    updateLoadingDots(); // Show immediately
    if (!m_loadingTimer->isActive()) {
        m_loadingTimer->start(500); // Update every 500ms
    }
    qInfo() << "Loading message timer started";
}

void SplashScreen::hideLoadingMessage()
{
    m_loadingTimer->stop();
    clearMessage();
}

void SplashScreen::updateMessage(const QString &message)
{
    m_baseMessage = message;
    m_dotCount = 0;
    updateLoadingDots();
}

void SplashScreen::updateLoadingDots()
{
    QString dots;
    int dotIndex = m_dotCount % 4;
    for (int i = 0; i < dotIndex; ++i) {
        dots += ".";
    }
    
    QString fullMessage = m_baseMessage + dots;
    qInfo() << "Updating splash message:" << fullMessage;
    // Display the message at the bottom center with black text for visibility
    showMessage(fullMessage, Qt::AlignBottom | Qt::AlignHCenter, Qt::black);
    m_dotCount++;
    
    // Force repaint to ensure message is visible
    repaint();
}

void SplashScreen::drawContents(QPainter *painter)
{
    // Call base implementation to draw loading message
    QSplashScreen::drawContents(painter);
    
    // Draw centered welcome message
    painter->save();
    
    // Set font for the centered text
    QFont font("Arial", 12);
    painter->setFont(font);
    painter->setPen(Qt::black);
    
    // Get the widget dimensions
    QRect rect = this->rect();
    
    // Define the text lines
    QString line1 = "Thank you for choosing Openterface.";
    QString line2 = "Discover more at our website: https://openterface.com/.";
    QString line3 = "For support & discussions with fellow users, please join our community.";
    QString line4 = "Enjoy a seamless direct interface control with us!";
    
    // Calculate vertical center position, moved lower
    int lineHeight = painter->fontMetrics().height();
    int totalHeight = lineHeight * 5; // 4 lines + spacing
    int startY = (rect.height() - totalHeight) / 2 + 50;  // Move down by 50 pixels
    
    // Draw each line centered
    QRect textRect = rect;
    textRect.setTop(startY);
    textRect.setHeight(lineHeight);
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, line1);
    
    textRect.moveTop(startY + lineHeight * 1.3);
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, line2);
    
    textRect.moveTop(startY + lineHeight * 2.6);
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, line3);
    
    textRect.moveTop(startY + lineHeight * 3.9);
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, line4);
    
    painter->restore();
}
