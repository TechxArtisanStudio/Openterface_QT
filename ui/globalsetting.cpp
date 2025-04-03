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


#include "globalsetting.h"
#include "global.h"
#include <QMutex>
#include <QFile>
#include <QDateTime>
#include <QSettings>

GlobalSetting::GlobalSetting(QObject *parent)
    : QObject(parent),
      m_settings("Techxartisan", "Openterface")
{
}

GlobalSetting& GlobalSetting::instance()
{
    static GlobalSetting instance;
    return instance;
}

void GlobalSetting::setFilterSettings(bool Chipinfo, bool keyboardPress, bool mideaKeyboard, bool mouseMoveABS, bool mouseMoveREL, bool HID)
{
    m_settings.setValue("filter/Chipinfo", Chipinfo);
    m_settings.setValue("filter/keyboardPress", keyboardPress);
    m_settings.setValue("filter/mideaKeyboard", mideaKeyboard);
    m_settings.setValue("filter/mouseMoveABS", mouseMoveABS);
    m_settings.setValue("filter/mouseMoveREL", mouseMoveREL);
    m_settings.setValue("filter/HID", HID);
}

void GlobalSetting::getFilterSettings(bool &Chipinfo, bool &keyboardPress, bool &mideaKeyboard, bool &mouseMoveABS, bool &mouseMoveREL, bool &HID)
{
    Chipinfo = m_settings.value("filter/Chipinfo", true).toBool();
    keyboardPress = m_settings.value("filter/keyboardPress", true).toBool();
    mideaKeyboard = m_settings.value("filter/mideaKeyboard", true).toBool();
    mouseMoveABS = m_settings.value("filter/mouseMoveABS", true).toBool();
    mouseMoveREL = m_settings.value("filter/mouseMoveREL", true).toBool();
    HID = m_settings.value("filter/HID", true).toBool();
}

void GlobalSetting::setLogSettings(bool core, bool serial, bool ui, bool host)
{
    m_settings.setValue("log/core", core);
    m_settings.setValue("log/serial", serial);
    m_settings.setValue("log/ui", ui);
    m_settings.setValue("log/host", host);
}

void GlobalSetting::loadLogSettings()
{
    QString logFilter = "";
    logFilter += m_settings.value("log/core", true).toBool() ? "opf.core.*=true\n" : "opf.core.*=false\n";
    logFilter += m_settings.value("log/ui", true).toBool() ? "opf.ui.*=true\n" : "opf.ui.*=false\n";
    logFilter += m_settings.value("log/host", true).toBool() ? "opf.host.*=true\n" : "opf.host.*=false\n";
    logFilter += m_settings.value("log/serial", true).toBool() ? "opf.core.serial=true\n" : "opf.core.serial=false\n";
    QLoggingCategory::setFilterRules(logFilter);
}

void GlobalSetting::setLogStoreSettings(bool storeLog, QString logFilePath){
    m_settings.setValue("log/storeLog", storeLog);
    m_settings.setValue("log/logFilePath", logFilePath);
}

void GlobalSetting::setVideoSettings(int width, int height, int fps){
    m_settings.setValue("video/width", width);
    m_settings.setValue("video/height", height);
    m_settings.setValue("video/fps", fps);
}

void GlobalSetting::loadVideoSettings(){
    GlobalVar::instance().setCaptureWidth(m_settings.value("video/width", 1920).toInt());
    GlobalVar::instance().setCaptureHeight(m_settings.value("video/height", 1080).toInt());
    GlobalVar::instance().setCaptureFps(m_settings.value("video/fps", 30).toInt());
}

void GlobalSetting::setCameraDeviceSetting(QString deviceDescription){
    m_settings.setValue("camera/device", deviceDescription);
}

void GlobalSetting::setVID(QString vid){
    m_settings.setValue("serial/vid", vid);
}

void GlobalSetting::setPID(QString pid){
    m_settings.setValue("serial/pid", pid);
}


void GlobalSetting::setSerialNumber(QString serialNumber){
    m_settings.setValue("serial/serialnumber", serialNumber);
}


void GlobalSetting::setUSBEnabelFlag(QString enableflag){
    m_settings.setValue("serial/enableflag", enableflag);
}

void GlobalSetting::setCustomStringDescriptor(QString customStringDisctriptor){
    m_settings.setValue("serial/customStringDescriptor", customStringDisctriptor);
}

void GlobalSetting::setCustomPIDDescriptor(QString customPIDDescriptor){
    m_settings.setValue("serial/customPIDDescriptor",customPIDDescriptor);
}

void GlobalSetting::setCustomVIDDescriptor(QString customVIDDescriptor){
    m_settings.setValue("serial/customVIDDescriptor", customVIDDescriptor);
}

void GlobalSetting::setKeyboardLayout(QString keyboardLayout){
    m_settings.setValue("keyboard/keyboardLayout", keyboardLayout);
}

void GlobalSetting::getKeyboardLayout(QString &keyboardLayout){
    keyboardLayout = m_settings.value("keyboard/keyboardLayout", "US QWERTY").toString();
}


void GlobalSetting::setMouseAutoHideEnable(bool enable){
    m_settings.setValue("mouse/autoHide", enable);
}

bool GlobalSetting::getMouseAutoHideEnable(){
    return m_settings.value("mouse/autoHide", true).toBool();
}

void GlobalSetting::setLangeuage(QString language){
    m_settings.setValue("language/language", language);
}

void GlobalSetting::getLanguage(QString &language){
    language = m_settings.value("language/language", "en").toString(); 
}

void GlobalSetting::setOperatingMode(int mode) {
    m_settings.setValue("hardware/operatingMode", mode);
}

int GlobalSetting::getOperatingMode() const {
    return m_settings.value("hardware/operatingMode", 2).toInt();
}

void GlobalSetting::setScreenSaverInhibited(bool inhibit) {
    m_settings.setValue("ScreenSaver/Inhibited", inhibit);
}

bool GlobalSetting::getScreenSaverInhibited() const {
    return m_settings.value("ScreenSaver/Inhibited", false).toBool();
}

/*
* Convert QString to ByteArray
*/
QByteArray GlobalSetting::convertStringToByteArray(QString str) {
    QStringList hexParts = str.split(" ", Qt::SkipEmptyParts);


    QString hexString = hexParts.join("");
    
    bool ok;
    int64_t value = hexString.toInt(&ok, 16);
    if (!ok) {
        // Handle the error, e.g., by returning an empty QByteArray or throwing an exception
        qDebug() << str << "Error converting string";
        return QByteArray();
    }

    QByteArray result;
    int hexLength = str.length();

    switch (hexLength) {
        case 1:
        case 2:
            result.append(static_cast<char>(value & 0xFF));
            break;
        case 3:
        case 4:
            result.append(static_cast<char>((value >> 8) & 0xFF));
            result.append(static_cast<char>(value & 0xFF));
            break;
        case 5:
        case 6:
            result.append(static_cast<char>((value >> 16) & 0xFF)); 
            result.append(static_cast<char>((value >> 8) & 0xFF)); 
            result.append(static_cast<char>(value & 0xFF)); 
            break;
        case 7:
        case 8:
            result.append(static_cast<char>((value >> 24) & 0xFF)); 
            result.append(static_cast<char>((value >> 16) & 0xFF)); 
            result.append(static_cast<char>((value >> 8) & 0xFF)); 
            result.append(static_cast<char>(value & 0xFF)); 
            break;
        case 9:
        case 10:
            result.append(static_cast<char>((value >> 32) & 0xFF)); 
            result.append(static_cast<char>((value >> 24) & 0xFF)); 
            result.append(static_cast<char>((value >> 16) & 0xFF));
            result.append(static_cast<char>((value >> 8) & 0xFF));
            result.append(static_cast<char>(value & 0xFF));
            break;
        case 11:
        case 12:
            result.append(static_cast<char>((value >> 40) & 0xFF)); 
            result.append(static_cast<char>((value >> 32) & 0xFF)); 
            result.append(static_cast<char>((value >> 24) & 0xFF)); 
            result.append(static_cast<char>((value >> 16) & 0xFF)); 
            result.append(static_cast<char>((value >> 8) & 0xFF)); 
            result.append(static_cast<char>(value & 0xFF));
            break;
        case 13:
        case 14:
            result.append(static_cast<char>((value >> 48) & 0xFF));
            result.append(static_cast<char>((value >> 40) & 0xFF));
            result.append(static_cast<char>((value >> 32) & 0xFF));
            result.append(static_cast<char>((value >> 24) & 0xFF));
            result.append(static_cast<char>((value >> 16) & 0xFF));
            result.append(static_cast<char>((value >> 8) & 0xFF)); 
            result.append(static_cast<char>(value & 0xFF)); 
            break;
        case 15:
        case 16:
            result.append(static_cast<char>((value >> 56) & 0xFF));
            result.append(static_cast<char>((value >> 48) & 0xFF));
            result.append(static_cast<char>((value >> 40) & 0xFF));
            result.append(static_cast<char>((value >> 32) & 0xFF));
            result.append(static_cast<char>((value >> 24) & 0xFF));
            result.append(static_cast<char>((value >> 16) & 0xFF));
            result.append(static_cast<char>((value >> 8) & 0xFF)); 
            result.append(static_cast<char>(value & 0xFF));
            break;
        default:
            result.append(static_cast<char>((value >> 56) & 0xFF));
            result.append(static_cast<char>((value >> 48) & 0xFF));
            result.append(static_cast<char>((value >> 40) & 0xFF));
            result.append(static_cast<char>((value >> 32) & 0xFF));
            result.append(static_cast<char>((value >> 24) & 0xFF));
            result.append(static_cast<char>((value >> 16) & 0xFF));
            result.append(static_cast<char>((value >> 8) & 0xFF)); 
            result.append(static_cast<char>(value & 0xFF)); 
            break;
    }
    return result;
}
