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
    logFilter += settings.value("Log/Core", true).toBool() ? "opf.core.*=true\n" : "opf.core.*=false\n";
    logFilter += settings.value("Log/Ui", true).toBool() ? "opf.ui.*=true\n" : "opf.ui.*=false\n";
    logFilter += settings.value("Log/Host", true).toBool() ? "opf.host.*=true\n" : "opf.host.*=false\n";
    logFilter += settings.value("Log/Serial", true).toBool() ? "opf.core.serial=true\n" : "opf.core.serial=false\n";
    QLoggingCategory::setFilterRules(logFilter);
}

void GlobalSetting::setVideoSettings(int width, int height, int fpd){
    settings.setValue("video/width", width);
    settings.setValue("video/height", height);
    settings.setValue("video/fps", fpd);
}

void GlobalSetting::loadVideoSettings(){
    GlobalVar::instance().setCaptureWidth(settings.value("video/width", 1920).toInt());
    GlobalVar::instance().setCaptureHeight(settings.value("video/height", 1080).toInt());
    GlobalVar::instance().setCaptureFps(settings.value("video/fps", 1920).toInt());
}