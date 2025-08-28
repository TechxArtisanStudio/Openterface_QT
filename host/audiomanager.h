#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <QObject>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QLoggingCategory>
#include <QTimer>
#include <QRegularExpression>

class AudioThread;

Q_DECLARE_LOGGING_CATEGORY(log_core_host_audio)

class AudioManager : public QObject {
    Q_OBJECT

public:
    // Singleton pattern
    static AudioManager& getInstance()
    {
        static AudioManager instance; // Guaranteed to be destroyed.
        return instance;
    }

    AudioManager(AudioManager const&) = delete;             // Copy construct
    AudioManager(AudioManager&&) = delete;                  // Move construct
    AudioManager& operator=(AudioManager const&) = delete;  // Copy assign
    AudioManager& operator=(AudioManager &&) = delete;      // Move assign

    ~AudioManager();

    void initializeAudio();
    void disconnect();
    
    // Audio device management
    void start(); // Similar to VideoHid::start() for initialization
    void stop();  // Similar to VideoHid::stop() for cleanup
    
    // Port chain based device selection
    bool switchToAudioDeviceByPortChain(const QString& portChain);
    QString getCurrentAudioPortChain() const;
    QAudioDevice getCurrentAudioDevice() const;
    
    // Device discovery and management
    QList<QAudioDevice> getAvailableAudioDevices() const;
    QStringList getAvailableAudioDeviceIds() const;
    QStringList getAvailableAudioDeviceDescriptions() const;
    
    // Device finding methods
    QAudioDevice findAudioDeviceByPortChain(const QString& portChain) const;
    QAudioDevice findAudioDeviceById(const QString& deviceId) const;
    bool isAudioDeviceAvailable(const QString& deviceId) const;
    
    // Hotplug support methods
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    void clearAudioDeviceCache();
    void refreshAudioDevice();

signals:
    void audioDeviceChanged(const QAudioDevice& device, const QString& portChain);
    void audioInitialized();
    void audioDisconnected();

private slots:
    void handleAudioError(const QString& error);

private:
    // Private constructor for singleton
    AudioManager();
    
    // Legacy methods (kept for compatibility)
    QAudioDevice findUvcCameraAudioDevice(QString deviceName);
    QAudioDevice findSystemAudioOuptutDevice(QString deviceName);
    
    // Helper methods
    void fadeInVolume(int timeout, int durationInSeconds);
    void initializeAudioWithDevice(const QAudioDevice& inputDevice);
    QString extractAudioDeviceGuid(const QString& deviceId) const;
    bool matchAudioDeviceId(const QString& audioDeviceId, const QString& hotplugDeviceId) const;
    void displayAllAudioDeviceIds() const;

    AudioThread* m_audioThread;
    QAudioDevice m_currentAudioDevice;
    QString m_currentAudioPortChain;
};

#endif // AUDIOMANAGER_H
