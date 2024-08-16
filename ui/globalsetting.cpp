#include "globalsetting.h"
#include "global.h"

GlobalSetting::GlobalSetting(QObject *parent)
    : QObject(parent),
      settings("Techxartisan", "Openterface")
{
}

GlobalSetting& GlobalSetting::instance()
{
    static GlobalSetting instance;
    return instance;
}

void GlobalSetting::setLogSettings(bool core, bool serial, bool ui, bool host)
{
    settings.setValue("log/core", core);
    settings.setValue("log/serial", serial);
    settings.setValue("log/ui", ui);
    settings.setValue("log/host", host);
}

void GlobalSetting::loadLogSettings()
{
    QString logFilter = "";
    logFilter += settings.value("log/core", true).toBool() ? "opf.core.*=true\n" : "opf.core.*=false\n";
    logFilter += settings.value("log/ui", true).toBool() ? "opf.ui.*=true\n" : "opf.ui.*=false\n";
    logFilter += settings.value("log/host", true).toBool() ? "opf.host.*=true\n" : "opf.host.*=false\n";
    logFilter += settings.value("log/serial", true).toBool() ? "opf.core.serial=true\n" : "opf.core.serial=false\n";
    QLoggingCategory::setFilterRules(logFilter);
}

void GlobalSetting::setVideoSettings(int width, int height, int fps){
    settings.setValue("video/width", width);
    settings.setValue("video/height", height);
    settings.setValue("video/fps", fps);
}

void GlobalSetting::loadVideoSettings(){
    GlobalVar::instance().setCaptureWidth(settings.value("video/width", 1920).toInt());
    GlobalVar::instance().setCaptureHeight(settings.value("video/height", 1080).toInt());
    GlobalVar::instance().setCaptureFps(settings.value("video/fps", 1920).toInt());
}

void GlobalSetting::setCameraDeviceSetting(QString deviceDescription){
    settings.setValue("camera/device", deviceDescription);
}