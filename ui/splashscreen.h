#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QSplashScreen>
#include <QTimer>

class SplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    explicit SplashScreen(const QPixmap &pixmap = QPixmap(), Qt::WindowFlags f = Qt::WindowFlags());
    ~SplashScreen();
    
    void showLoadingMessage();
    void hideLoadingMessage();
    void updateMessage(const QString &message);

protected:
    void drawContents(QPainter *painter) override;

private slots:
    void updateLoadingDots();

private:
    QTimer *m_loadingTimer;
    int m_dotCount;
    QString m_baseMessage;
};

#endif // SPLASHSCREEN_H
