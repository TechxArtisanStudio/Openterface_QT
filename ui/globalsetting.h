#ifndef GLOBALSETTING_H
#define GLOBALSETTING_H

#include <QObject>
#include <QSettings>
#include <QSize>
#include <QLoggingCategory>
class GlobalSetting : public QObject
{
    Q_OBJECT
public:
    explicit GlobalSetting(QObject *parent = nullptr);

    static GlobalSetting& instance();

    void setLogSettings(bool core, bool serial, bool ui, bool host);
    void loadLogSettings();

    void setVideoSettings(int width, int height, int fps);
    void loadVideoSettings();

    
private:
    QSettings settings;
};

#endif // GLOBALSETTING_H