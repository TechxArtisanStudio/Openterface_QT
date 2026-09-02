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
#include <QMap>
#include <QPair>
class GlobalSetting : public QObject
{
    Q_OBJECT
public:
    explicit GlobalSetting(QObject *parent = nullptr);

    static GlobalSetting& instance();

    // Log category states: category -> {enabled, level}
    void saveCategoryStates(const QMap<QString, QPair<bool, QString>>& states);
    QMap<QString, QPair<bool, QString>> loadCategoryStates() const;
    void loadLogSettings();  // Apply saved category filter rules at startup

    /// AI Chat log toggle (separate from the main log settings to keep API stable)
    void setAILogEnabled(bool enabled);
    bool getAILogEnabled() const;

    void setFilterSettings(bool Chipinfo, bool keyboardPress, bool mideaKeyboard, bool mouseMoveABS, bool mouseMoveREL, bool HID);

    void getFilterSettings(bool &Chipinfo, bool &keyboardPress, bool &mideaKeyboard, bool &mouseMoveABS, bool &mouseMoveREL, bool &HID);

    void setLogStoreSettings(bool storeLog, QString logFilePath);

    void setVideoSettings(int width, int height, int fps);

    void loadVideoSettings();

    void setMediaBackend(const QString &backend);
    QString getMediaBackend() const;
    
    void setHardwareAcceleration(const QString &hwAccel);
    QString getHardwareAcceleration() const;
    
    void setScalingQuality(const QString &quality);
    QString getScalingQuality() const;
    
    void setGStreamerPipelineTemplate(const QString &pipelineTemplate);
    QString getGStreamerPipelineTemplate() const;

    void setGStreamerSinkPriority(const QStringList &priorityList);
    QStringList getGStreamerSinkPriority() const;
    
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

    void setHideKeyboardInput(bool hide);
    bool getHideKeyboardInput() const;

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
    
    // Video rendering quality settings
    void setVideoAntialiasing(bool enabled);
    bool getVideoAntialiasing() const;
    void setVideoTextAntialiasing(bool enabled);
    bool getVideoTextAntialiasing() const;
    void setVideoSmoothTransform(bool enabled);
    bool getVideoSmoothTransform() const;

    // Custom key import path persistence
    void setLastCustomKeyImportPath(const QString& path);
    QString getLastCustomKeyImportPath() const;

    // Floating window
    void setFloatingWindowEnabled(bool enabled);
    bool getFloatingWindowEnabled() const;
    void setFloatingWindowOpacity(double opacity);
    double getFloatingWindowOpacity() const;

    // System key blocker
    void setSystemKeyBlockerEnabled(bool enabled);
    bool getSystemKeyBlockerEnabled() const;

    // Update reminder settings
    // Stores the epoch seconds of the last update check (used for 30-day throttle)
    void setUpdateLastChecked(qint64 secsSinceEpoch);
    qint64 getUpdateLastChecked() const;

    // If true, the user opted to never be reminded about updates
    void setUpdateNeverRemind(bool never);
    bool getUpdateNeverRemind() const;

    // ---- MCP Server ----
    void setMcpEnabled(bool enabled);
    bool getMcpEnabled() const;
    void setMcpTransport(const QString& transport);
    QString getMcpTransport() const;
    void setMcpSsePort(int port);
    int getMcpSsePort() const;
    void setMcpSseBindAddress(const QString& address);
    QString getMcpSseBindAddress() const;
    void setMcpSsePathSse(const QString& path);
    QString getMcpSsePathSse() const;
    void setMcpSsePathMessages(const QString& path);
    QString getMcpSsePathMessages() const;
    void setMcpSseKeepaliveInterval(int ms);
    int getMcpSseKeepaliveInterval() const;
    void setMcpSseSessionTimeout(int ms);
    int getMcpSseSessionTimeout() const;
    void setMcpSseCleanupInterval(int ms);
    int getMcpSseCleanupInterval() const;
    void setMcpSseMaxSessions(int max);
    int getMcpSseMaxSessions() const;
    void setMcpScreenToMarkdown(bool enabled);
    bool getMcpScreenToMarkdown() const;

    // ---- AI Chat ----
    void setChatApiBaseURL(const QString &url);
    QString getChatApiBaseURL() const;
    void setChatApiKey(const QString &key);
    QString getChatApiKey() const;
    void setChatModel(const QString &model);
    QString getChatModel() const;
    void setChatTargetSystem(const QString &system);
    QString getChatTargetSystem() const;
    void setChatAgentMaxIterations(int max);
    int getChatAgentMaxIterations() const;
    void setChatAgenticModeEnabled(bool enabled);
    bool getChatAgenticModeEnabled() const;
    void setChatPlannerModeEnabled(bool enabled);
    bool getChatPlannerModeEnabled() const;
    void setChatGuideModeEnabled(bool enabled);
    bool getChatGuideModeEnabled() const;
    void setChatSystemPrompt(const QString &prompt);
    QString getChatSystemPrompt() const;
    void setChatPlannerPrompt(const QString &prompt);
    QString getChatPlannerPrompt() const;
    void setChatScreenTaskPrompt(const QString &prompt);
    QString getChatScreenTaskPrompt() const;
    void setChatTypingTaskPrompt(const QString &prompt);
    QString getChatTypingTaskPrompt() const;
    void setChatGuidePrompt(const QString &prompt);
    QString getChatGuidePrompt() const;
    void setChatWindowVisible(bool visible);
    bool getChatWindowVisible() const;
    void setChatWindowWidth(int width);
    int getChatWindowWidth() const;
    void setChatDockSide(const QString &side);
    QString getChatDockSide() const;

    // AI Chat typing/paste delay settings
    void setChatTypingDelayMs(int ms);
    int getChatTypingDelayMs() const;
    void setChatBatchSize(int size);
    int getChatBatchSize() const;

    // AI Chat timing delay settings (for USB HID synchronization)
    void setChatMouseToKeyboardDelayMs(int ms);
    int getChatMouseToKeyboardDelayMs() const;
    void setChatPostKeyboardSettleMs(int ms);
    int getChatPostKeyboardSettleMs() const;
    void setChatPreCaptureDelayMs(int ms);
    int getChatPreCaptureDelayMs() const;
    void setChatInitialTypingDelayMs(int ms);
    int getChatInitialTypingDelayMs() const;

    // AI Chat tool enable/disable settings
    void setChatToolEnabled(const QString &tool, bool enabled);
    bool getChatToolEnabled(const QString &tool) const;
    QMap<QString, bool> getChatAllToolsEnabled() const;
    void setChatAllToolsEnabled(const QMap<QString, bool> &tools);

signals:
    /// Emitted when the target OS for the AI agent changes (e.g. via the chat
    /// header button or the set_target_system tool). Listeners can refresh any
    /// UI that displays the current target OS.
    void chatTargetSystemChanged(const QString &system);

private:
    QSettings m_settings;
};

#endif // GLOBALSETTING_H