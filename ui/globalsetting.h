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


#ifndef GLOBALSETTING_H
#define GLOBALSETTING_H

#include <QObject>
#include <QSettings>
#include <QSize>
#include <QLoggingCategory>
#include <QByteArray>
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
    
    void setCameraDeviceSetting(QString deviceDescription);

    void setVIDPID(QString vid, QString pid);
    
    QByteArray convertStringToByteArray(QString str);

    void setUSBFlag(QString flag);

private:
    QSettings settings;
};

#endif // GLOBALSETTING_H