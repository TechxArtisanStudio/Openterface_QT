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

void GlobalSetting::setAILogEnabled(bool enabled)
{
    m_settings.setValue("log/ai", enabled);
}

bool GlobalSetting::getAILogEnabled() const
{
    return m_settings.value("log/ai", false).toBool();
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

void GlobalSetting::setMcpScreenToMarkdown(bool enabled) {
    m_settings.setValue("mcp/screenToMarkdown", enabled);
}

bool GlobalSetting::getMcpScreenToMarkdown() const {
    return m_settings.value("mcp/screenToMarkdown", false).toBool();
}

// ============================================================================
// AI Chat Settings
// ============================================================================

void GlobalSetting::setChatApiBaseURL(const QString &url) {
    m_settings.setValue("chat/apiBaseURL", url);
}

QString GlobalSetting::getChatApiBaseURL() const {
    return m_settings.value("chat/apiBaseURL", "https://api.openai.com/v1").toString();
}

void GlobalSetting::setChatApiKey(const QString &key) {
    m_settings.setValue("chat/apiKey", key);
}

QString GlobalSetting::getChatApiKey() const {
    // Also check environment variable as fallback
    QString key = m_settings.value("chat/apiKey", "").toString();
    if (key.isEmpty()) {
        key = QString::fromUtf8(qgetenv("OPENAI_API_KEY"));
    }
    return key;
}

void GlobalSetting::setChatModel(const QString &model) {
    m_settings.setValue("chat/model", model);
}

QString GlobalSetting::getChatModel() const {
    return m_settings.value("chat/model", "gpt-4o-mini").toString();
}

void GlobalSetting::setChatTargetSystem(const QString &system) {
    const QString current = m_settings.value("chat/targetSystem", "linux").toString();
    if (current != system) {
        m_settings.setValue("chat/targetSystem", system);
        emit chatTargetSystemChanged(system);
    }
}

QString GlobalSetting::getChatTargetSystem() const {
    return m_settings.value("chat/targetSystem", "linux").toString();
}

void GlobalSetting::setChatAgentMaxIterations(int max) {
    m_settings.setValue("chat/agentMaxIterations", qBound(1, max, 30));
}

int GlobalSetting::getChatAgentMaxIterations() const {
    return qBound(1, m_settings.value("chat/agentMaxIterations", 10).toInt(), 30);
}

void GlobalSetting::setChatAgenticModeEnabled(bool enabled) {
    m_settings.setValue("chat/agenticMode", enabled);
}

bool GlobalSetting::getChatAgenticModeEnabled() const {
    return m_settings.value("chat/agenticMode", false).toBool();
}

void GlobalSetting::setChatPlannerModeEnabled(bool enabled) {
    m_settings.setValue("chat/plannerMode", enabled);
}

bool GlobalSetting::getChatPlannerModeEnabled() const {
    return m_settings.value("chat/plannerMode", false).toBool();
}

void GlobalSetting::setChatGuideModeEnabled(bool enabled) {
    m_settings.setValue("chat/guideMode", enabled);
}

bool GlobalSetting::getChatGuideModeEnabled() const {
    return m_settings.value("chat/guideMode", false).toBool();
}

void GlobalSetting::setChatSystemPrompt(const QString &prompt) {
    m_settings.setValue("chat/systemPrompt", prompt);
}

QString GlobalSetting::getChatSystemPrompt() const {
    return m_settings.value("chat/systemPrompt",
        "You are Openterface Assistant, an on-device KVM copilot.\n\n"
        "Capabilities:\n"
        "- You can analyze the latest shared screen image from the target computer.\n"
        "- You can execute actions on the target through Openterface tools.\n\n"
        "Operating style:\n"
        "- Be concise, practical, and step-by-step.\n"
        "- When you complete a task, provide a brief summary and STOP. Do not continue iterating.\n"
        "- Only issue tool calls when you need to perform an action. If the task is done, just respond with text.\n"
        "- If screen details are unclear, ask for a fresh screenshot or zoomed area.\n"
        "- State assumptions explicitly when uncertain.\n\n"
        "Control guidance:\n"
        "- Provide exact key names and mouse actions (click, double-click, right-click, drag).\n"
        "- For text entry, provide the exact text to type.\n"
        "- For risky actions (delete, reset, install, security changes), ask for confirmation first.\n\n"
        "Safety and scope:\n"
        "- Do not invent screen content you cannot see.\n"
        "- Prioritize non-destructive troubleshooting before invasive changes.\n"
        "- Protect privacy: avoid requesting secrets unless absolutely required.\n"
    ).toString();
}

void GlobalSetting::setChatPlannerPrompt(const QString &prompt) {
    m_settings.setValue("chat/plannerPrompt", prompt);
}

QString GlobalSetting::getChatPlannerPrompt() const {
    return m_settings.value("chat/plannerPrompt",
        "You are the Openterface Main Agent.\n\n"
        "Your job is to understand the user's intent, inspect the current target screen when available, "
        "and produce a structured execution plan before any task runs.\n\n"
        "Rules:\n"
        "- Return ONLY JSON.\n"
        "- Build a short, concrete plan that can be reviewed by the user.\n"
        "- Keep tasks simple and independent.\n"
        "- Available task agents/tools:\n"
        "    - screen + capture_screen (AI vision analysis - sends image to AI model)\n"
        "    - screen + screen_to_markdown (OCR - extracts text using Tesseract, no vision needed)\n"
        "    - typing + type_text\n"
        "    - mouse + move_mouse\n"
        "    - mouse + left_click\n"
        "    - mouse + right_click\n"
        "    - mouse + double_click\n"
        "- Use capture_screen when the AI model supports vision and you need visual understanding.\n"
        "- Use screen_to_markdown when you need text extraction without vision, or when vision is unavailable.\n"
        "- Use typing tasks when the user intent requires entering text or keystrokes on target.\n"
        "- Use mouse tasks when the user intent requires cursor movement or clicks on target.\n"
        "- Do not execute tasks yourself.\n"
        "- Do not invent screen details that are not visible.\n\n"
        "Schema:\n"
        "{\n"
        "    \"summary\": \"one short sentence about the plan\",\n"
        "    \"tasks\": [\n"
        "        {\"title\": \"...\", \"detail\": \"...\", \"agent\": \"screen\", \"tool\": \"capture_screen\"},\n"
        "        {\"title\": \"...\", \"detail\": \"...\", \"agent\": \"screen\", \"tool\": \"screen_to_markdown\"},\n"
        "        {\"title\": \"...\", \"detail\": \"...\", \"agent\": \"typing\", \"tool\": \"type_text\"},\n"
        "        {\"title\": \"...\", \"detail\": \"...\", \"agent\": \"mouse\", \"tool\": \"left_click\"}\n"
        "    ]\n"
        "}\n"
    ).toString();
}

void GlobalSetting::setChatScreenTaskPrompt(const QString &prompt) {
    m_settings.setValue("chat/screenTaskPrompt", prompt);
}

QString GlobalSetting::getChatScreenTaskPrompt() const {
    return m_settings.value("chat/screenTaskPrompt",
        "You are the Openterface Screen Task Agent.\n\n"
        "You are responsible for exactly one task and may rely on the latest target screen image as your only tool context.\n\n"
        "Rules:\n"
        "- Return ONLY JSON.\n"
        "- Focus only on the assigned task.\n"
        "- Do not plan future tasks.\n"
        "- Do not claim actions were executed.\n"
        "- If the screen is unclear, report that directly.\n\n"
        "Schema:\n"
        "{\n"
        "    \"status\": \"completed\" | \"failed\",\n"
        "    \"result_summary\": \"short result for the user\"\n"
        "}\n"
    ).toString();
}

void GlobalSetting::setChatTypingTaskPrompt(const QString &prompt) {
    m_settings.setValue("chat/typingTaskPrompt", prompt);
}

QString GlobalSetting::getChatTypingTaskPrompt() const {
    return m_settings.value("chat/typingTaskPrompt",
        "You are the Openterface Typing Task Agent.\n\n"
        "You are responsible for one typing task and one tool only: type_text.\n\n"
        "Rules:\n"
        "- Return ONLY JSON.\n"
        "- Focus only on the current task.\n"
        "- For plain typing, provide text_to_type.\n"
        "- For keyboard/function keys, use angle-bracket format (example: <ctrl>l, <cmd><space>, <enter>, <f1>).\n"
        "- Provide either text_to_type or shortcut.\n\n"
        "Schema:\n"
        "{\n"
        "    \"status\": \"completed\" | \"failed\",\n"
        "    \"text_to_type\": \"exact text to type on target (optional)\",\n"
        "    \"shortcut\": \"keyboard combo like Ctrl+L (optional)\",\n"
        "    \"result_summary\": \"short summary for the user\"\n"
        "}\n"
    ).toString();
}

void GlobalSetting::setChatGuidePrompt(const QString &prompt) {
    m_settings.setValue("chat/guidePrompt", prompt);
}

QString GlobalSetting::getChatGuidePrompt() const {
    return m_settings.value("chat/guidePrompt",
        "You are the Openterface Guide Mode Agent.\n\n"
        "Provide turn-by-turn guidance for the user to accomplish their goal on the target screen.\n\n"
        "Rules:\n"
        "- Return ONLY JSON.\n"
        "- Provide one clear step at a time.\n"
        "- If a clickable target is identified, set target_box with normalized coordinates (0.0-1.0).\n"
        "- If a keyboard shortcut is more appropriate, set tool and tool_input.\n"
        "- Prefer keyboard shortcuts over mouse clicks when both are viable.\n"
        "- If the target is unclear, set needs_clarification=true.\n\n"
        "Schema:\n"
        "{\n"
        "    \"next_step\": \"clear instruction for the next step\",\n"
        "    \"tool\": \"left_click|right_click|double_click|shortcut (optional)\",\n"
        "    \"tool_input\": \"shortcut like Ctrl+S or key combo (optional)\",\n"
        "    \"target_box\": {\"x\": 0.0-1.0, \"y\": 0.0-1.0, \"width\": 0.0-1.0, \"height\": 0.0-1.0},\n"
        "    \"needs_clarification\": true|false,\n"
        "    \"clarification\": \"what to clarify (if needed)\"\n"
        "}\n"
    ).toString();
}

void GlobalSetting::setChatWindowVisible(bool visible) {
    m_settings.setValue("chat/windowVisible", visible);
}

bool GlobalSetting::getChatWindowVisible() const {
    return m_settings.value("chat/windowVisible", false).toBool();
}

void GlobalSetting::setChatWindowWidth(int width) {
    m_settings.setValue("chat/windowWidth", qBound(320, width, 800));
}

int GlobalSetting::getChatWindowWidth() const {
    return qBound(320, m_settings.value("chat/windowWidth", 420).toInt(), 800);
}

void GlobalSetting::setChatDockSide(const QString &side) {
    m_settings.setValue("chat/dockSide", side);
}

QString GlobalSetting::getChatDockSide() const {
    return m_settings.value("chat/dockSide", "right").toString();
}

void GlobalSetting::setChatTypingDelayMs(int ms) {
    m_settings.setValue("chat/typingDelayMs", qBound(0, ms, 1000));
}

int GlobalSetting::getChatTypingDelayMs() const {
    return qBound(0, m_settings.value("chat/typingDelayMs", 20).toInt(), 1000);
}

void GlobalSetting::setChatBatchSize(int size) {
    m_settings.setValue("chat/batchSize", qBound(1, size, 50));
}

int GlobalSetting::getChatBatchSize() const {
    return qBound(1, m_settings.value("chat/batchSize", 5).toInt(), 50);
}

void GlobalSetting::setChatMouseToKeyboardDelayMs(int ms) {
    m_settings.setValue("chat/mouseToKeyboardDelayMs", qBound(0, ms, 5000));
}

int GlobalSetting::getChatMouseToKeyboardDelayMs() const {
    return qBound(0, m_settings.value("chat/mouseToKeyboardDelayMs", 800).toInt(), 5000);
}

void GlobalSetting::setChatPostKeyboardSettleMs(int ms) {
    m_settings.setValue("chat/postKeyboardSettleMs", qBound(0, ms, 5000));
}

int GlobalSetting::getChatPostKeyboardSettleMs() const {
    return qBound(0, m_settings.value("chat/postKeyboardSettleMs", 400).toInt(), 5000);
}

void GlobalSetting::setChatPreCaptureDelayMs(int ms) {
    m_settings.setValue("chat/preCaptureDelayMs", qBound(0, ms, 5000));
}

int GlobalSetting::getChatPreCaptureDelayMs() const {
    return qBound(0, m_settings.value("chat/preCaptureDelayMs", 400).toInt(), 5000);
}

void GlobalSetting::setChatInitialTypingDelayMs(int ms) {
    m_settings.setValue("chat/initialTypingDelayMs", qBound(0, ms, 5000));
}

int GlobalSetting::getChatInitialTypingDelayMs() const {
    return qBound(0, m_settings.value("chat/initialTypingDelayMs", 500).toInt(), 5000);
}
