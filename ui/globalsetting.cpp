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
    logFilter += settings.value("log/serial", true).toBool() ? "opf.core.serial=true\n" : "opf.core.serial=true\n";
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

void GlobalSetting::setVIDPID(QString vid, QString pid){
    settings.setValue("serial/vid", vid);
    settings.setValue("serial/pid", pid);
    
}

void GlobalSetting::setUSBFlag(QString flag){
    settings.setValue("serial/usbflag", flag);
}


/*
* Convert QString to ByteArray
*/
QByteArray GlobalSetting::convertStringToByteArray(QString str) {
    QStringList hexParts = str.split(" ", Qt::SkipEmptyParts);


    QString hexString = hexParts.join("");
    
    bool ok;
    int value = hexString.toInt(&ok, 16);
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
