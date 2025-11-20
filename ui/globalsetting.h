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

    void setLogSettings(bool core, bool serial, bool ui, bool hostLayout, bool device, bool backend);

    void setFilterSettings(bool Chipinfo, bool keyboardPress, bool mideaKeyboard, bool mouseMoveABS, bool mouseMoveREL, bool HID);

    void getFilterSettings(bool &Chipinfo, bool &keyboardPress, bool &mideaKeyboard, bool &mouseMoveABS, bool &mouseMoveREL, bool &HID);
    
    void loadLogSettings();

    void setLogStoreSettings(bool storeLog, QString logFilePath);

    void setVideoSettings(int width, int height, int fps);

    void loadVideoSettings();

    void setMediaBackend(const QString &backend);
    QString getMediaBackend() const;
    
    void setHardwareAcceleration(const QString &hwAccel);
    QString getHardwareAcceleration() const;
    
    void setGStreamerPipelineTemplate(const QString &pipelineTemplate);
    QString getGStreamerPipelineTemplate() const;
    
    void setCameraDeviceSetting(QString deviceDescription);

    void setVID(QString vid);

    void setPID(QString pid);

    void setUSBEnabelFlag(QString enableflag);

    QByteArray convertStringToByteArray(QString str);

    void setSerialNumber(QString serialNumber);

    void setCustomStringDescriptor(QString customStringDisctriptor);

    void setCustomPIDDescriptor(QString customPIDDescriptor);

    void setCustomVIDDescriptor(QString customVIDDescriptor);

    void setKeyboardLayout(QString keyboardLayout);

    void getKeyboardLayout(QString &keyboardLayout);

    void setMouseAutoHideEnable(bool enable);

    bool getMouseAutoHideEnable();

    void setLangeuage(QString language);

    void getLanguage(QString &language);

    void setOperatingMode(int mode);
    int getOperatingMode() const;

    void setScreenSaverInhibited(bool inhibit);
    bool getScreenSaverInhibited() const;

    void setScreenRatio(double ratio);
    double getScreenRatio() const;

    // Port chain management for Openterface devices
    void setOpenterfacePortChain(const QString& portChain);
    QString getOpenterfacePortChain() const;
    void clearOpenterfacePortChain();

    // Serial port baudrate management
    void setSerialPortBaudrate(int baudrate);
    int getSerialPortBaudrate() const;
    void clearSerialPortBaudrate();
    
    // ARM architecture baudrate performance prompt
    void setArmBaudratePromptDisabled(bool disabled);
    bool getArmBaudratePromptDisabled() const;
    void resetArmBaudratePrompt(); // Reset the prompt setting
    
    // Video recording settings
    void setRecordingVideoCodec(const QString& codec);
    QString getRecordingVideoCodec() const;
    void setRecordingVideoBitrate(int bitrate);
    int getRecordingVideoBitrate() const;
    void setRecordingPixelFormat(const QString& format);
    QString getRecordingPixelFormat() const;
    void setRecordingKeyframeInterval(int interval);
    int getRecordingKeyframeInterval() const;
    
    void setRecordingAudioCodec(const QString& codec);
    QString getRecordingAudioCodec() const;
    void setRecordingAudioBitrate(int bitrate);
    int getRecordingAudioBitrate() const;
    void setRecordingAudioSampleRate(int sampleRate);
    int getRecordingAudioSampleRate() const;
    
    void setRecordingOutputFormat(const QString& format);
    QString getRecordingOutputFormat() const;
    void setRecordingOutputPath(const QString& path);
    QString getRecordingOutputPath() const;
    
    // Audio mute setting
    void setAudioMuted(bool muted);
    bool getAudioMuted() const;

private:
    QSettings m_settings;
};

#endif // GLOBALSETTING_H