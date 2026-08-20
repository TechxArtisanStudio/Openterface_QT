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
#include <QSet>

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

void GlobalSetting::saveCategoryStates(const QMap<QString, QPair<bool, QString>>& states)
{
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const QString& category = it.key();
        bool enabled = it.value().first;
        QString level = it.value().second;
        m_settings.setValue(QString("log/category/%1/enabled").arg(category), enabled);
        m_settings.setValue(QString("log/category/%1/level").arg(category), level);
    }
}

QMap<QString, QPair<bool, QString>> GlobalSetting::loadCategoryStates() const
{
    QMap<QString, QPair<bool, QString>> states;
    // Find all unique category names from keys like "log/category/<cat>/enabled"
    QSet<QString> categories;
    for (const QString& key : m_settings.allKeys()) {
        if (key.startsWith("log/category/") && key.endsWith("/enabled")) {
            QString cat = key.mid(QString("log/category/").size(),
                                  key.size() - QString("log/category/").size() - QString("/enabled").size());
            categories.insert(cat);
        }
    }
    for (const QString& category : categories) {
        bool enabled = m_settings.value(QString("log/category/%1/enabled").arg(category), true).toBool();
        QString level = m_settings.value(QString("log/category/%1/level").arg(category), "Info").toString();
        states.insert(category, qMakePair(enabled, level));
    }
    return states;
}

void GlobalSetting::loadLogSettings()
{
    auto states = loadCategoryStates();
    if (states.isEmpty()) {
        return;
    }

    QStringList rules;
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const QString& category = it.key();
        bool enabled = it.value().first;
        QString level = it.value().second;

        if (!enabled) {
            rules << QString("%1=false").arg(category);
        } else {
            QString levelLower = level.toLower().trimmed();
            if (levelLower == "off") {
                rules << QString("%1=false").arg(category);
            } else {
                rules << QString("%1.%2=true").arg(category, levelLower);
            }
        }
    }
    if (!rules.isEmpty()) {
        QLoggingCategory::setFilterRules(rules.join('\n'));
    }
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
#ifdef Q_OS_WIN
    return m_settings.value("video/mediaBackend", "ffmpeg").toString();
#elif defined(Q_PROCESSOR_ARM)
    return m_settings.value("video/mediaBackend", "gstreamer").toString();
#else
    return m_settings.value("video/mediaBackend", "ffmpeg").toString();
#endif
}

void GlobalSetting::setHardwareAcceleration(const QString &hwAccel) {
    m_settings.setValue("video/hardwareAcceleration", hwAccel);
    m_settings.sync();  // Ensure the setting is written immediately
}

QString GlobalSetting::getHardwareAcceleration() const {
    return m_settings.value("video/hardwareAcceleration", "CPU").toString();
}

void GlobalSetting::setScalingQuality(const QString &quality) {
    m_settings.setValue("video/scalingQuality", quality);
    m_settings.sync();
}

QString GlobalSetting::getScalingQuality() const {
    return m_settings.value("video/scalingQuality", "balanced").toString();
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

void GlobalSetting::setGStreamerSinkPriority(const QStringList &priorityList) {
    m_settings.setValue("video/gstreamerSinkPriority", priorityList);
}

QStringList GlobalSetting::getGStreamerSinkPriority() const {
    return m_settings.value("video/gstreamerSinkPriority", QStringList() << "qt6videosink" << "qtvideosink" << "qtsink" << "xvimagesink" << "ximagesink" << "autovideosink").toStringList();
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

void GlobalSetting::setHideKeyboardInput(bool hide){
    m_settings.setValue("keyboard/hideInput", hide);
}

bool GlobalSetting::getHideKeyboardInput() const {
    return m_settings.value("keyboard/hideInput", false).toBool();
}

void GlobalSetting::setSystemKeyBlockerEnabled(bool enabled) {
    m_settings.setValue("keyboard/systemKeyBlocker", enabled);
}

bool GlobalSetting::getSystemKeyBlockerEnabled() const {
    return m_settings.value("keyboard/systemKeyBlocker", false).toBool();
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

void GlobalSetting::setFloatingWindowEnabled(bool enabled) {
    m_settings.setValue("floatingWindow/show", enabled);
}

bool GlobalSetting::getFloatingWindowEnabled() const {
    return m_settings.value("floatingWindow/show", false).toBool();
}

void GlobalSetting::setFloatingWindowOpacity(double opacity) {
    m_settings.setValue("floatingWindow/opacity", opacity);
}

double GlobalSetting::getFloatingWindowOpacity() const {
    return m_settings.value("floatingWindow/opacity", 0.85).toDouble();
}

// Persist the time (seconds since epoch) when update was last checked.
void GlobalSetting::setUpdateLastChecked(qint64 secsSinceEpoch)
{
    m_settings.setValue("update/lastChecked", secsSinceEpoch);
    m_settings.sync();
}

qint64 GlobalSetting::getUpdateLastChecked() const
{
    return m_settings.value("update/lastChecked", 0).toLongLong();
}

// Persist the user's "never remind" choice for updates.
void GlobalSetting::setUpdateNeverRemind(bool never)
{
    m_settings.setValue("update/neverRemind", never);
    m_settings.sync();
}

bool GlobalSetting::getUpdateNeverRemind() const
{
    return m_settings.value("update/neverRemind", false).toBool();
}

// Port chain management for Openterface devices
void GlobalSetting::setOpenterfacePortChain(const QString& portChain) {
    m_settings.setValue("openterface/portChain", portChain);
    m_settings.sync(); // Ensure immediate write to storage
}

QString GlobalSetting::getOpenterfacePortChain() const {
    return m_settings.value("openterface/portChain", "").toString();
}

void GlobalSetting::clearOpenterfacePortChain() {
    m_settings.remove("openterface/portChain");
    m_settings.sync();
}

// Serial port baudrate management
void GlobalSetting::setSerialPortBaudrate(int baudrate) {
    m_settings.setValue("serial/baudrate", baudrate);
    m_settings.sync(); // Ensure immediate write to storage
}

int GlobalSetting::getSerialPortBaudrate() const {
    return m_settings.value("serial/baudrate", -1).toInt(); // -1 means no stored baudrate
}

void GlobalSetting::clearSerialPortBaudrate() {
    m_settings.remove("serial/baudrate");
    m_settings.sync();
}

// ARM architecture baudrate performance prompt
void GlobalSetting::setArmBaudratePromptDisabled(bool disabled) {
    m_settings.setValue("serial/armBaudratePromptDisabled", disabled);
    m_settings.sync();
}

bool GlobalSetting::getArmBaudratePromptDisabled() const {
    return m_settings.value("serial/armBaudratePromptDisabled", false).toBool();
}

void GlobalSetting::resetArmBaudratePrompt() {
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

void GlobalSetting::setAudioMuted(bool muted)
{
    m_settings.setValue("audio/muted", muted);
}

bool GlobalSetting::getAudioMuted() const
{
    return m_settings.value("audio/muted", true).toBool();
}

// Video rendering quality settings implementation
void GlobalSetting::setVideoAntialiasing(bool enabled)
{
    m_settings.setValue("video/antialiasing", enabled);
}

bool GlobalSetting::getVideoAntialiasing() const
{
    return m_settings.value("video/antialiasing", true).toBool();
}

void GlobalSetting::setVideoTextAntialiasing(bool enabled)
{
    m_settings.setValue("video/textAntialiasing", enabled);
}

bool GlobalSetting::getVideoTextAntialiasing() const
{
    return m_settings.value("video/textAntialiasing", true).toBool();
}

void GlobalSetting::setVideoSmoothTransform(bool enabled)
{
    m_settings.setValue("video/smoothTransform", enabled);
}

bool GlobalSetting::getVideoSmoothTransform() const
{
    return m_settings.value("video/smoothTransform", true).toBool();
}

// Custom key import path settings
void GlobalSetting::setLastCustomKeyImportPath(const QString& path)
{
    m_settings.setValue("customkeys/lastImportPath", path);
}

QString GlobalSetting::getLastCustomKeyImportPath() const
{
    return m_settings.value("customkeys/lastImportPath", QString()).toString();
}

// ===========================================================================
// MCP Server settings
// ===========================================================================

void GlobalSetting::setMcpEnabled(bool enabled) {
    m_settings.setValue("mcp/enabled", enabled);
}

bool GlobalSetting::getMcpEnabled() const {
    return m_settings.value("mcp/enabled", false).toBool();
}

void GlobalSetting::setMcpTransport(const QString& transport) {
    m_settings.setValue("mcp/transport", transport);
}

QString GlobalSetting::getMcpTransport() const {
    return m_settings.value("mcp/transport", "stdio").toString();
}

void GlobalSetting::setMcpSsePort(int port) {
    m_settings.setValue("mcp/ssePort", port);
}

int GlobalSetting::getMcpSsePort() const {
    return m_settings.value("mcp/ssePort", 8080).toInt();
}

void GlobalSetting::setMcpSseBindAddress(const QString& address) {
    m_settings.setValue("mcp/sseBindAddress", address);
}

QString GlobalSetting::getMcpSseBindAddress() const {
    return m_settings.value("mcp/sseBindAddress", "0.0.0.0").toString();
}

void GlobalSetting::setMcpSsePathSse(const QString& path) {
    m_settings.setValue("mcp/ssePathSse", path);
}

QString GlobalSetting::getMcpSsePathSse() const {
    return m_settings.value("mcp/ssePathSse", "/sse").toString();
}

void GlobalSetting::setMcpSsePathMessages(const QString& path) {
    m_settings.setValue("mcp/ssePathMessages", path);
}

QString GlobalSetting::getMcpSsePathMessages() const {
    return m_settings.value("mcp/ssePathMessages", "/messages").toString();
}

void GlobalSetting::setMcpSseKeepaliveInterval(int ms) {
    m_settings.setValue("mcp/sseKeepaliveInterval", ms);
}

int GlobalSetting::getMcpSseKeepaliveInterval() const {
    return m_settings.value("mcp/sseKeepaliveInterval", 15000).toInt();
}

void GlobalSetting::setMcpSseSessionTimeout(int ms) {
    m_settings.setValue("mcp/sseSessionTimeout", ms);
}

int GlobalSetting::getMcpSseSessionTimeout() const {
    return m_settings.value("mcp/sseSessionTimeout", 1800000).toInt();
}

void GlobalSetting::setMcpSseCleanupInterval(int ms) {
    m_settings.setValue("mcp/sseCleanupInterval", ms);
}

int GlobalSetting::getMcpSseCleanupInterval() const {
    return m_settings.value("mcp/sseCleanupInterval", 60000).toInt();
}

void GlobalSetting::setMcpSseMaxSessions(int max) {
    m_settings.setValue("mcp/sseMaxSessions", max);
}

int GlobalSetting::getMcpSseMaxSessions() const {
    return m_settings.value("mcp/sseMaxSessions", 16).toInt();
}
