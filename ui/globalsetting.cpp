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

void GlobalSetting::setLogSettings(bool core, bool serial, bool ui, bool hostLayout, bool device, bool backend)
{
    m_settings.setValue("log/core", core);
    m_settings.setValue("log/serial", serial);
    m_settings.setValue("log/ui", ui);
    m_settings.setValue("log/hostLayout", hostLayout);
    m_settings.setValue("log/device", device);
    m_settings.setValue("log/backend", backend); 
}

void GlobalSetting::loadLogSettings()
{
    QString logFilter = "";
    logFilter += m_settings.value("log/core", false).toBool() ? "opf.core.*=true\n" : "opf.core.*=false\n";
    logFilter += m_settings.value("log/ui", false).toBool() ? "opf.ui.*=true\n" : "opf.ui.*=false\n";
    logFilter += m_settings.value("log/host", false).toBool() ? "opf.host.*=true\n" : "opf.host.*=false\n";
    logFilter += m_settings.value("log/serial", false).toBool() ? "opf.core.serial=true\n" : "opf.core.serial=false\n";
    logFilter += m_settings.value("log/device", false).toBool() ? "opf.device.*=true\n" : "opf.device.*=false\n";
    logFilter += m_settings.value("log/backend", false).toBool() ? "opf.backend.*=true\n" : "opf.backend.*=false\n";
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

void GlobalSetting::setMediaBackend(const QString &backend) {
    m_settings.setValue("video/mediaBackend", backend);
}

QString GlobalSetting::getMediaBackend() const {
#if defined(Q_PROCESSOR_ARM)
    return m_settings.value("video/mediaBackend", "gstreamer").toString();
#else
    return m_settings.value("video/mediaBackend", "ffmpeg").toString();
#endif
}

void GlobalSetting::setGStreamerPipelineTemplate(const QString &pipelineTemplate) {
    m_settings.setValue("video/gstreamerPipelineTemplate", pipelineTemplate);
}

QString GlobalSetting::getGStreamerPipelineTemplate() const {
    // Default GStreamer pipeline template with placeholders, tee, and valve for recording support
    QString defaultTemplate = "v4l2src device=%DEVICE% do-timestamp=true ! "
                             "image/jpeg,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! "
                             "jpegdec ! "
                             "videoconvert ! "
                             "identity sync=true ! "
                             "tee name=t allow-not-linked=true "
                             "t. ! queue max-size-buffers=2 leaky=downstream ! xvimagesink name=videosink sync=true "
                             "t. ! valve name=recording-valve drop=true ! queue name=recording-queue ! identity name=recording-ready";
    return m_settings.value("video/gstreamerPipelineTemplate", defaultTemplate).toString();
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

void GlobalSetting::setScreenRatio(double ratio) {
    m_settings.setValue("screen/ratio", ratio);
}

double GlobalSetting::getScreenRatio() const {
    return m_settings.value("screen/ratio", 1.7778).toDouble();
}

// Port chain management for Openterface devices
void GlobalSetting::setOpenterfacePortChain(const QString& portChain) {
    qDebug() << "Logging Openterface port chain:" << portChain;
    m_settings.setValue("openterface/portChain", portChain);
    m_settings.sync(); // Ensure immediate write to storage
}

QString GlobalSetting::getOpenterfacePortChain() const {
    return m_settings.value("openterface/portChain", "").toString();
}

void GlobalSetting::clearOpenterfacePortChain() {
    qDebug() << "Clearing Openterface port chain";
    m_settings.remove("openterface/portChain");
    m_settings.sync();
}

// Serial port baudrate management
void GlobalSetting::setSerialPortBaudrate(int baudrate) {
    qDebug() << "Storing serial port baudrate:" << baudrate;
    m_settings.setValue("serial/baudrate", baudrate);
    m_settings.sync(); // Ensure immediate write to storage
}

int GlobalSetting::getSerialPortBaudrate() const {
    return m_settings.value("serial/baudrate", -1).toInt(); // -1 means no stored baudrate
}

void GlobalSetting::clearSerialPortBaudrate() {
    qDebug() << "Clearing stored serial port baudrate";
    m_settings.remove("serial/baudrate");
    m_settings.sync();
}

// ARM architecture baudrate performance prompt
void GlobalSetting::setArmBaudratePromptDisabled(bool disabled) {
    qDebug() << "Setting ARM baudrate prompt disabled:" << disabled;
    m_settings.setValue("serial/armBaudratePromptDisabled", disabled);
    m_settings.sync();
}

bool GlobalSetting::getArmBaudratePromptDisabled() const {
    return m_settings.value("serial/armBaudratePromptDisabled", false).toBool();
}

void GlobalSetting::resetArmBaudratePrompt() {
    qDebug() << "Resetting ARM baudrate prompt setting";
    m_settings.remove("serial/armBaudratePromptDisabled");
    m_settings.sync();
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

// Video recording settings
void GlobalSetting::setRecordingVideoCodec(const QString& codec)
{
    m_settings.setValue("recording/videoCodec", codec);
}

QString GlobalSetting::getRecordingVideoCodec() const
{
    return m_settings.value("recording/videoCodec", "mjpeg").toString();
}

void GlobalSetting::setRecordingVideoBitrate(int bitrate)
{
    m_settings.setValue("recording/videoBitrate", bitrate);
}

int GlobalSetting::getRecordingVideoBitrate() const
{
    return m_settings.value("recording/videoBitrate", 2000000).toInt();
}

void GlobalSetting::setRecordingPixelFormat(const QString& format)
{
    m_settings.setValue("recording/pixelFormat", format);
}

QString GlobalSetting::getRecordingPixelFormat() const
{
    return m_settings.value("recording/pixelFormat", "yuv420p").toString();
}

void GlobalSetting::setRecordingKeyframeInterval(int interval)
{
    m_settings.setValue("recording/keyframeInterval", interval);
}

int GlobalSetting::getRecordingKeyframeInterval() const
{
    return m_settings.value("recording/keyframeInterval", 30).toInt();
}

void GlobalSetting::setRecordingAudioCodec(const QString& codec)
{
    m_settings.setValue("recording/audioCodec", codec);
}

QString GlobalSetting::getRecordingAudioCodec() const
{
    return m_settings.value("recording/audioCodec", "aac").toString();
}

void GlobalSetting::setRecordingAudioBitrate(int bitrate)
{
    m_settings.setValue("recording/audioBitrate", bitrate);
}

int GlobalSetting::getRecordingAudioBitrate() const
{
    return m_settings.value("recording/audioBitrate", 128000).toInt();
}

void GlobalSetting::setRecordingAudioSampleRate(int sampleRate)
{
    m_settings.setValue("recording/audioSampleRate", sampleRate);
}

int GlobalSetting::getRecordingAudioSampleRate() const
{
    return m_settings.value("recording/audioSampleRate", 44100).toInt();
}

void GlobalSetting::setRecordingOutputFormat(const QString& format)
{
    m_settings.setValue("recording/outputFormat", format);
}

QString GlobalSetting::getRecordingOutputFormat() const
{
    return m_settings.value("recording/outputFormat", "avi").toString();
}

void GlobalSetting::setRecordingOutputPath(const QString& path)
{
    m_settings.setValue("recording/outputPath", path);
}

QString GlobalSetting::getRecordingOutputPath() const
{
    return m_settings.value("recording/outputPath", "").toString();
}
