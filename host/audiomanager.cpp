#include "audiomanager.h"
#include "audiothread.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"
#include "../ui/globalsetting.h"
#include "../global.h"
#include <QDebug>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(log_core_host_audio, "opf.core.host.audio");

AudioManager::AudioManager()
    : QObject(nullptr), m_audioThread(nullptr) {
    qCDebug(log_core_host_audio) << "AudioManager singleton initialized...";
    
    // Initialize current device to null state
    m_currentAudioDevice = QAudioDevice();
    m_currentAudioPortChain.clear();
    
    displayAllAudioDeviceIds();
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();
}

AudioManager::~AudioManager() {
    // Disconnect from hotplug monitoring
    disconnectFromHotplugMonitor();
    disconnect();
}

QAudioDevice AudioManager::findUvcCameraAudioDevice(QString deviceName) {
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        qCDebug(log_core_host_audio) << "Audio Input Device name:" << device.description() << ", ID:" << device.id();

        // Check if the device name or other properties match the UVC camera's audio device
        if (device.description().contains(deviceName)) {
            return device;
        }
    }
    return QAudioDevice(); // Return default device if not found
}

QAudioDevice AudioManager::findSystemAudioOuptutDevice(QString deviceName) {
    const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : devices) {
        qCDebug(log_core_host_audio) << "Audio Output Device name:" << device.description() << ", ID:" << device.id();

        if (device.description().contains(deviceName)) {
            return device;
        }
    }
    return QAudioDevice(); // Return default device if not found
}

void AudioManager::initializeAudio() {
    qCDebug(log_core_host_audio) << "Initializing audio...";
    
    QAudioDevice inputDevice;
    
    // Try to use device from current port chain if available
    if (!m_currentAudioPortChain.isEmpty()) {
        inputDevice = findAudioDeviceByPortChain(m_currentAudioPortChain);
        if (!inputDevice.isNull()) {
            qCDebug(log_core_host_audio) << "Using audio device from current port chain:" << m_currentAudioPortChain;
        }
    }
    
    // Fall back to finding by device name (legacy behavior)
    if (inputDevice.isNull()) {
        inputDevice = findUvcCameraAudioDevice("OpenterfaceA");
        if (!inputDevice.isNull()) {
            qCDebug(log_core_host_audio) << "Using audio device found by name: OpenterfaceA";
        }
    }
    
    // Use default input device as last resort
    if (inputDevice.isNull()) {
        inputDevice = QMediaDevices::defaultAudioInput();
        qCDebug(log_core_host_audio) << "Using default audio input device";
    }
    
    if (inputDevice.isNull()) {
        qCWarning(log_core_host_audio) << "No audio input device available.";
        return;
    }
    
    // Update current device tracking
    m_currentAudioDevice = inputDevice;
    
    // Initialize audio with the selected device
    initializeAudioWithDevice(inputDevice);
}

void AudioManager::fadeInVolume(int timeout, int durationInSeconds) {
    if (!m_audioThread) return;

    // Calculate the number of steps and the increment per step
    int steps = (durationInSeconds * 1000) / timeout;
    qreal increment = 1.0 / steps;

    // Create a QTimer to handle the volume fade-in
    QTimer *volumeTimer = new QTimer(this);
    connect(volumeTimer, &QTimer::timeout, [this, volumeTimer, increment]() {
        if (!m_audioThread) return;

        qreal currentVolume = m_audioThread->volume();

        if (currentVolume < 1.0) {
            m_audioThread->setVolume(currentVolume + increment);
        } else {
            m_audioThread->setVolume(1.0);
            volumeTimer->stop();
            volumeTimer->deleteLater();
        }
    });

    // Start the timer with the given interval
    volumeTimer->start(timeout);
}

void AudioManager::disconnect() {
    qCDebug(log_core_host_audio) << "Disconnecting audio thread.";
    if (m_audioThread) {
        qCDebug(log_core_host_audio) << "AudioThread found - proceeding with cleanup";
        
        // Check if application is shutting down
        if (g_applicationShuttingDown.loadAcquire() == 1) {
            qCDebug(log_core_host_audio) << "Application shutting down - minimal cleanup only";
            
            // During shutdown, don't wait for thread - just mark it for deletion
            m_audioThread->disconnect(); // Disconnect signals
            m_audioThread->stop(); // Set the stop flag only
            
            // Don't call wait() during shutdown to prevent deadlocks
            // Just delete and let the destructor handle minimal cleanup
            delete m_audioThread;
            m_audioThread = nullptr;
            
            // Clear current device tracking immediately
            m_currentAudioDevice = QAudioDevice();
            m_currentAudioPortChain.clear();
            
            qCDebug(log_core_host_audio) << "AudioThread minimal cleanup completed";
            return;
        }
        
        // Normal cleanup path (not during application shutdown)
        // Disconnect all signals first to prevent any callbacks during cleanup
        m_audioThread->disconnect();
        
        // Stop the thread
        m_audioThread->stop();
        
        // Wait for thread to finish with timeout
        if (!m_audioThread->wait(3000)) {
            qCWarning(log_core_host_audio) << "Audio thread didn't stop gracefully, forcing termination";
            m_audioThread->terminate();
            m_audioThread->wait(1000);
        }
        
        // Delete the thread
        delete m_audioThread;
        m_audioThread = nullptr;
        
        // Clear current device tracking
        m_currentAudioDevice = QAudioDevice();
        m_currentAudioPortChain.clear();
        
        qCDebug(log_core_host_audio) << "AudioThread cleanup completed";
        emit audioDisconnected();
    } else {
        qCDebug(log_core_host_audio) << "No AudioThread to disconnect";
    }
}

void AudioManager::handleAudioError(const QString& error) {
    qCWarning(log_core_host_audio) << "Audio error:" << error;
}

void AudioManager::handleCleanupRequest() {
    qCDebug(log_core_host_audio) << "AudioManager received cleanup request - cleaning up multimedia objects on main thread";
    
    if (m_audioThread) {
        // This will be called on the main thread, which is safer for Qt Multimedia cleanup
        m_audioThread->cleanupMultimediaObjects();
    }
}

// Port chain based device selection methods

void AudioManager::start()
{
    qCDebug(log_core_host_audio) << "Starting AudioManager...";
    
    // Initialize current device tracking from global settings
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    if (!currentPortChain.isEmpty()) {
        m_currentAudioPortChain = currentPortChain;
        QAudioDevice audioDevice = findAudioDeviceByPortChain(currentPortChain);
        if (!audioDevice.isNull()) {
            m_currentAudioDevice = audioDevice;
            qCDebug(log_core_host_audio) << "Found audio device for current port chain:" << currentPortChain;
        }
    }
    
    // Initialize audio if we have a valid device
    initializeAudio();
}

void AudioManager::stop()
{
    static bool alreadyStopped = false;
    if (alreadyStopped) {
        qCDebug(log_core_host_audio) << "AudioManager::stop() called but already stopped - ignoring";
        return;
    }
    
    qCDebug(log_core_host_audio) << "Stopping AudioManager...";
    disconnect();
    alreadyStopped = true;
    qCDebug(log_core_host_audio) << "AudioManager stopped successfully";
}

bool AudioManager::switchToAudioDeviceByPortChain(const QString& portChain)
{
    qCDebug(log_core_host_audio) << "Switching to audio device by port chain:" << portChain;
    
    if (portChain.isEmpty()) {
        qCWarning(log_core_host_audio) << "Cannot switch to device with empty port chain";
        return false;
    }
    
    QAudioDevice targetDevice = findAudioDeviceByPortChain(portChain);
    if (targetDevice.isNull()) {
        qCWarning(log_core_host_audio) << "No audio device found for port chain:" << portChain;
        return false;
    }
    
    // Stop current audio if running
    if (m_audioThread) {
        disconnect();
    }
    
    // Update current device tracking
    m_currentAudioDevice = targetDevice;
    m_currentAudioPortChain = portChain;
    
    qCDebug(log_core_host_audio) << "Successfully switched to audio device:" 
                                << targetDevice.description() << "at port chain:" << portChain;
    
    // Reinitialize audio with new device
    initializeAudioWithDevice(targetDevice);
    
    emit audioDeviceChanged(targetDevice, portChain);
    return true;
}

QString AudioManager::getCurrentAudioPortChain() const
{
    return m_currentAudioPortChain;
}

QAudioDevice AudioManager::getCurrentAudioDevice() const
{
    return m_currentAudioDevice;
}

QList<QAudioDevice> AudioManager::getAvailableAudioDevices() const
{
    return QMediaDevices::audioInputs();
}

QStringList AudioManager::getAvailableAudioDeviceIds() const
{
    QStringList deviceIds;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        deviceIds << QString::fromUtf8(device.id());
    }
    return deviceIds;
}

QStringList AudioManager::getAvailableAudioDeviceDescriptions() const
{
    QStringList descriptions;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        descriptions << device.description();
    }
    return descriptions;
}

QAudioDevice AudioManager::findAudioDeviceByPortChain(const QString& portChain) const
{
    if (portChain.isEmpty()) {
        return QAudioDevice();
    }
    
    // Get device info from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
    
    if (devices.isEmpty()) {
        qCDebug(log_core_host_audio) << "No device info found for port chain:" << portChain;
        return QAudioDevice();
    }
    
    DeviceInfo deviceInfo = devices.first();
    if (!deviceInfo.hasAudioDevice()) {
        qCDebug(log_core_host_audio) << "Device at port chain" << portChain << "has no audio interface";
        return QAudioDevice();
    }
    
    QString targetAudioId = deviceInfo.audioDeviceId;
    qCDebug(log_core_host_audio) << "Looking for audio device with ID:" << targetAudioId;
    
    // Find matching audio device from Qt's audio system
    const QList<QAudioDevice> audioDevices = QMediaDevices::audioInputs();
    for (const QAudioDevice &audioDevice : audioDevices) {
        QString audioDeviceId = QString::fromUtf8(audioDevice.id());
        
        qCDebug(log_core_host_audio) << "Comparing audio device ID:" << audioDeviceId 
                                    << "with target:" << targetAudioId;
        
        if (matchAudioDeviceId(audioDeviceId, targetAudioId)) {
            qCDebug(log_core_host_audio) << "Found matching audio device:" << audioDevice.description();
            return audioDevice;
        }
    }
    
    qCWarning(log_core_host_audio) << "No matching audio device found for port chain:" << portChain;
    return QAudioDevice();
}

QAudioDevice AudioManager::findAudioDeviceById(const QString& deviceId) const
{
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        if (QString::fromUtf8(device.id()) == deviceId) {
            return device;
        }
    }
    return QAudioDevice();
}

bool AudioManager::isAudioDeviceAvailable(const QString& deviceId) const
{
    return !findAudioDeviceById(deviceId).isNull();
}

QString AudioManager::extractAudioDeviceGuid(const QString& deviceId) const
{
    // Extract GUID pattern like {066429B6-13A5-4869-8029-DED24018DB36} from device ID
    // Audio device ID format: {0.0.1.00000000}.{066429b6-13a5-4869-8029-ded24018db36}
    // Hotplug device ID format: SWD\MMDEVAPI\{0.0.1.00000000}.{066429B6-13A5-4869-8029-DED24018DB36}
    
    QRegularExpression guidRegex(R"(\{([0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12})\})", 
                                QRegularExpression::CaseInsensitiveOption);
    
    QRegularExpressionMatchIterator matches = guidRegex.globalMatch(deviceId);
    
    // Look for the second GUID (the actual device GUID, not the category GUID)
    QString lastGuid;
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        lastGuid = match.captured(1);
    }
    
    if (!lastGuid.isEmpty()) {
        qCDebug(log_core_host_audio) << "Extracted GUID:" << lastGuid << "from:" << deviceId;
        return lastGuid;
    }
    
    qCDebug(log_core_host_audio) << "No GUID found in:" << deviceId;
    return QString();
}

bool AudioManager::matchAudioDeviceId(const QString& audioDeviceId, const QString& hotplugDeviceId) const
{
    // Extract GUIDs from both IDs and compare them (case-insensitive)
    QString audioGuid = extractAudioDeviceGuid(audioDeviceId);
    QString hotplugGuid = extractAudioDeviceGuid(hotplugDeviceId);
    
    if (audioGuid.isEmpty() || hotplugGuid.isEmpty()) {
        qCDebug(log_core_host_audio) << "Failed to extract GUIDs for comparison";
        return false;
    }
    
    bool match = audioGuid.compare(hotplugGuid, Qt::CaseInsensitive) == 0;
    qCDebug(log_core_host_audio) << "GUID comparison result:" << match 
                                << "(" << audioGuid << "vs" << hotplugGuid << ")";
    
    return match;
}

void AudioManager::displayAllAudioDeviceIds() const
{
    try {
        QList<QAudioDevice> devices = getAvailableAudioDevices();
        
        qCDebug(log_core_host_audio) << "=== Available Audio Input Devices ===";
        qCDebug(log_core_host_audio) << "Total devices found:" << devices.size();
        
        if (devices.isEmpty()) {
            qCDebug(log_core_host_audio) << "No audio input devices available";
            return;
        }
        
        for (int i = 0; i < devices.size(); ++i) {
            const QAudioDevice& device = devices[i];
            QString deviceId = QString::fromUtf8(device.id());
            QString extractedGuid = extractAudioDeviceGuid(deviceId);
            
            qCDebug(log_core_host_audio) << QString("Device %1:").arg(i + 1);
            qCDebug(log_core_host_audio) << "  Description:" << device.description();
            qCDebug(log_core_host_audio) << "  Full ID:" << deviceId;
            qCDebug(log_core_host_audio) << "  Extracted GUID:" << extractedGuid;
            qCDebug(log_core_host_audio) << "  Is Default:" << device.isDefault();
            qCDebug(log_core_host_audio) << "  ---";
        }
        
        qCDebug(log_core_host_audio) << "=== End Audio Device List ===";
    } catch (const std::exception& e) {
        qCWarning(log_core_host_audio) << "Exception while displaying audio device IDs:" << e.what();
    } catch (...) {
        qCWarning(log_core_host_audio) << "Unknown exception while displaying audio device IDs";
    }
}

void AudioManager::initializeAudioWithDevice(const QAudioDevice& inputDevice)
{
    qCDebug(log_core_host_audio) << "Initializing audio with specific input device:" << inputDevice.description();
    
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    
    if (outputDevice.isNull()) {
        qCWarning(log_core_host_audio) << "No audio output device found.";
        return;
    }
    
    try {
        QAudioFormat format = outputDevice.preferredFormat();
        
        // Log the input device and format details
        qCDebug(log_core_host_audio) << "Audio format details:";
        qCDebug(log_core_host_audio) << "Sample rate:" << format.sampleRate();
        qCDebug(log_core_host_audio) << "Channel count:" << format.channelCount();
        qCDebug(log_core_host_audio) << "Sample size:" << format.bytesPerSample();
        
        // Create and start the audio thread
        m_audioThread = new AudioThread(inputDevice, outputDevice, format, this);
        connect(m_audioThread, &AudioThread::error, this, &AudioManager::handleAudioError);
        connect(m_audioThread, &AudioThread::cleanupRequested, this, &AudioManager::handleCleanupRequest, Qt::QueuedConnection);
        m_audioThread->start();
        
        // Initialize volume to 0 and start fade-in
        m_audioThread->setVolume(0.0);
        fadeInVolume(100, 3);
        
        emit audioInitialized();
        
    } catch (const std::exception &e) {
        qCWarning(log_core_host_audio) << "Exception occurred during audio initialization:" << e.what();
        return;
    }
}

// Hotplug support methods

void AudioManager::connectToHotplugMonitor()
{
    qCDebug(log_core_host_audio) << "Connecting AudioManager to hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!hotplugMonitor) {
        qCWarning(log_core_host_audio) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_core_host_audio) << "AudioManager: Attempting audio device deactivation for unplugged device port:" << device.portChain;
                
                // Only deactivate audio device if the device has an audio component
                if (!device.hasAudioDevice()) {
                    qCDebug(log_core_host_audio) << "Device at port" << device.portChain << "has no audio component";
                    return;
                }
                
                // Check if the unplugged device matches the current audio device
                if (m_currentAudioPortChain == device.portChain) {
                    qCInfo(log_core_host_audio) << "Current audio device unplugged, stopping audio";
                    disconnect();
                    emit audioDisconnected();
                } else {
                    qCDebug(log_core_host_audio) << "Unplugged device port (" << device.portChain 
                                                << ") does not match current audio port (" << m_currentAudioPortChain << ")";
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_core_host_audio) << "AudioManager: Attempting audio device auto-switch for new device port:" << device.portChain;
                
                // Only attempt auto-switch if the device has an audio component
                if (!device.hasAudioDevice()) {
                    qCDebug(log_core_host_audio) << "Device at port" << device.portChain << "has no audio component";
                    return;
                }
                
                // Check if there's currently an active audio device
                if (m_audioThread != nullptr) {
                    qCDebug(log_core_host_audio) << "Audio device already active, not auto-switching";
                    return;
                }
                
                qCDebug(log_core_host_audio) << "No active audio device found, attempting to switch to new device";
                
                // Switch to the new audio device
                bool switchSuccess = switchToAudioDeviceByPortChain(device.portChain);
                if (switchSuccess) {
                    qCInfo(log_core_host_audio) << "✓ Successfully auto-switched to new audio device at port:" << device.portChain;
                } else {
                    qCWarning(log_core_host_audio) << "Failed to auto-switch to new audio device at port:" << device.portChain;
                }
            });
            
    qCDebug(log_core_host_audio) << "AudioManager successfully connected to hotplug monitor";
}

void AudioManager::disconnectFromHotplugMonitor()
{
    qCDebug(log_core_host_audio) << "Disconnecting AudioManager from hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (hotplugMonitor) {
        QObject::disconnect(hotplugMonitor, nullptr, this, nullptr);
        qCDebug(log_core_host_audio) << "AudioManager disconnected from hotplug monitor";
    }
}

void AudioManager::clearAudioDeviceCache()
{
    qCDebug(log_core_host_audio) << "Clearing audio device cache";
    // AudioManager doesn't use device path caching like VideoHid, but this method
    // is provided for consistency with the VideoHid interface
    // If we add caching in the future, the cache clearing logic would go here
}

void AudioManager::refreshAudioDevice()
{
    qCDebug(log_core_host_audio) << "Refreshing audio device connection";
    
    // Clear any potential cached device information
    clearAudioDeviceCache();
    
    // If we're currently active with an audio device, restart it
    if (m_audioThread != nullptr && !m_currentAudioPortChain.isEmpty()) {
        qCDebug(log_core_host_audio) << "Restarting current audio device for port chain:" << m_currentAudioPortChain;
        QString currentPortChain = m_currentAudioPortChain;
        disconnect();
        
        // Try to reconnect to the same device
        if (!switchToAudioDeviceByPortChain(currentPortChain)) {
            qCWarning(log_core_host_audio) << "Failed to restart audio device for port chain:" << currentPortChain;
        }
    }
}
