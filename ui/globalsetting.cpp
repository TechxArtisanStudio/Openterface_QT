#include "globalsetting.h"

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

