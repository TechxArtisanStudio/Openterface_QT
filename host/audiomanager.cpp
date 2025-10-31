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
    
    // Fall back to finding by device name (legacy behavior) - only for Openterface devices
    if (inputDevice.isNull()) {
        inputDevice = findUvcCameraAudioDevice("OpenterfaceA");
        if (!inputDevice.isNull()) {
            qCDebug(log_core_host_audio) << "Using audio device found by name: OpenterfaceA";
        }
    }
    
    // DO NOT use default input device - only use Openterface devices
    // If no Openterface device found, stay muted
    if (inputDevice.isNull()) {
        qCDebug(log_core_host_audio) << "No Openterface audio device found - staying muted";
        return;
    }
    
    // Verify it's an Openterface device
    if (!inputDevice.description().contains("Openterface", Qt::CaseInsensitive)) {
        qCWarning(log_core_host_audio) << "Found device is not an Openterface device - staying muted";
        return;
    }
    
    // Update current device tracking
    m_currentAudioDevice = inputDevice;
    
    qCInfo(log_core_host_audio) << "Starting audio capture for Openterface device:" << inputDevice.description();
    
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
        if (!m_audioThread) {
            volumeTimer->stop();
            volumeTimer->deleteLater();
            return;
        }
        
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

void AudioManager::setVolume(qreal volume)
{
    // Clamp volume to valid range [0.0, 1.0]
    volume = qBound(0.0, volume, 1.0);
    
    if (m_audioThread) {
        m_audioThread->setVolume(volume);
        qCDebug(log_core_host_audio) << "Volume set to:" << volume;
    } else {
        qCDebug(log_core_host_audio) << "Cannot set volume: no audio thread";
    }
}

qreal AudioManager::getVolume() const
{
    if (m_audioThread) {
        return m_audioThread->volume();
    }
    return 0.0;
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
    
    // Check if this is a device disconnection error
    if (error.contains("disconnected", Qt::CaseInsensitive) || 
        error.contains("invalidated", Qt::CaseInsensitive) ||
        error.contains("IOError", Qt::CaseInsensitive)) {
        qCWarning(log_core_host_audio) << "Audio device disconnection detected, cleaning up";
        
        // Disconnect the audio thread
        disconnect();
        
        // Emit signal to notify that audio is disconnected
        emit audioDisconnected();
    }
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
            
            // Only initialize audio if we found a valid Openterface device
            qCDebug(log_core_host_audio) << "Openterface audio device detected at startup, initializing audio capture";
            initializeAudio();
        } else {
            qCDebug(log_core_host_audio) << "No Openterface audio device found at startup, staying muted";
        }
    } else {
        qCDebug(log_core_host_audio) << "No port chain configured at startup, staying muted until device is plugged in";
    }
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
    
    // Additional verification: ensure the device is an Openterface device
    if (!targetDevice.description().contains("Openterface", Qt::CaseInsensitive)) {
        qCWarning(log_core_host_audio) << "Device at port chain" << portChain 
                                      << "is not an Openterface audio device. Description:" 
                                      << targetDevice.description();
        return false;
    }
    
    // Check if we're already using this device - avoid unnecessary switching
    if (!m_currentAudioDevice.isNull() && 
        QString::fromUtf8(m_currentAudioDevice.id()) == QString::fromUtf8(targetDevice.id())) {
        qCDebug(log_core_host_audio) << "Already using audio device:" << targetDevice.description() << "- skipping switch";
        return true;
    }
    
    QString previousDescription = m_currentAudioDevice.isNull() ? "None" : m_currentAudioDevice.description();
    
    // Stop current audio if running
    if (m_audioThread) {
        qCDebug(log_core_host_audio) << "Stopping current audio device before switch";
        disconnect();
    }
    
    // Update current device tracking
    m_currentAudioDevice = targetDevice;
    m_currentAudioPortChain = portChain;
    
    qCDebug(log_core_host_audio) << "Successfully switched to Openterface audio device:" 
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
    
    // Verify this is an Openterface device
    if (!deviceInfo.audioDeviceId.contains("Openterface", Qt::CaseInsensitive)) {
        qCDebug(log_core_host_audio) << "Device at port chain" << portChain << "is not an Openterface audio device";
        return QAudioDevice();
    }
    
    QString targetAudioId = deviceInfo.audioDeviceId;
    qCDebug(log_core_host_audio) << "Looking for Openterface audio device with ID:" << targetAudioId;
    
    // Find matching audio device from Qt's audio system
    const QList<QAudioDevice> audioDevices = QMediaDevices::audioInputs();
    for (const QAudioDevice &audioDevice : audioDevices) {
        QString audioDeviceId = QString::fromUtf8(audioDevice.id());
        
        qCDebug(log_core_host_audio) << "Comparing audio device ID:" << audioDeviceId 
                                    << "with target:" << targetAudioId;
        
        if (matchAudioDeviceId(audioDeviceId, targetAudioId)) {
            // Double-check that the matched device is an Openterface device
            if (audioDevice.description().contains("Openterface", Qt::CaseInsensitive)) {
                qCDebug(log_core_host_audio) << "Found matching Openterface audio device:" << audioDevice.description();
                return audioDevice;
            } else {
                qCWarning(log_core_host_audio) << "Device ID matched but description doesn't contain 'Openterface':" << audioDevice.description();
            }
        }
    }
    
    qCWarning(log_core_host_audio) << "No matching Openterface audio device found for port chain:" << portChain;
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

#ifdef Q_OS_WIN
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

bool AudioManager::matchWindowsAudioDevice(const QString& audioDeviceId, const QString& hotplugDeviceId) const
{
    qCDebug(log_core_host_audio) << "Windows audio device matching - Audio ID:" << audioDeviceId 
                                << "Hotplug ID:" << hotplugDeviceId;
    
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
#else

bool AudioManager::matchLinuxAudioDevice(const QString& audioDeviceId, const QString& devicePath) const
{
    qCDebug(log_core_host_audio) << "Linux audio device matching - ALSA ID:" << audioDeviceId 
                                << "Device path:" << devicePath;
    
    // Extract card number from device path
    // Path format: /sys/devices/.../sound/card3/controlC3
    QRegularExpression cardRegex(R"(/sound/card(\d+)/control)");
    QRegularExpressionMatch cardMatch = cardRegex.match(devicePath);
    
    if (!cardMatch.hasMatch()) {
        qCDebug(log_core_host_audio) << "No card number found in device path:" << devicePath;
        return false;
    }
    
    QString cardNumber = cardMatch.captured(1);
    qCDebug(log_core_host_audio) << "Extracted card number:" << cardNumber;
    
    // Check if ALSA device ID contains the card reference
    // ALSA ID format: alsa_input.usb-MACROSILICON_Openterface_________-02.iec958-stereo
    // The "02" part often corresponds to the USB interface, but we need to check for the device name
    
    // First, check if the ALSA ID contains "Openterface" (device-specific matching)
    if (audioDeviceId.contains("Openterface", Qt::CaseInsensitive)) {
        qCDebug(log_core_host_audio) << "Found Openterface device in ALSA ID";
        
        // Also check if device path contains USB identifiers that match
        if (devicePath.contains("usb") && devicePath.contains("1-1")) {
            qCDebug(log_core_host_audio) << "Device path indicates USB device on expected port";
            return true;
        }
    }
    
    // Alternative: Try to match based on USB vendor/product info in the path
    // Extract USB device info from both IDs if possible
    if (matchLinuxUsbAudioDevice(audioDeviceId, devicePath)) {
        return true;
    }
    
    qCDebug(log_core_host_audio) << "No match found between ALSA ID and device path";
    return false;
}

bool AudioManager::matchLinuxUsbAudioDevice(const QString& audioDeviceId, const QString& devicePath) const
{
    // Extract USB bus and device info from the device path
    // Path format: /sys/devices/platform/soc/fe980000.usb/usb1/1-1/1-1.3/1-1.3.1/...
    QRegularExpression usbPathRegex(R"(usb\d+/([\d\-\.]+))");
    QRegularExpressionMatch pathMatch = usbPathRegex.match(devicePath);
    
    if (!pathMatch.hasMatch()) {
        return false;
    }
    
    QString usbPath = pathMatch.captured(1);
    qCDebug(log_core_host_audio) << "USB path from device path:" << usbPath;
    
    // For Openterface devices, we can use device name matching as primary method
    if (audioDeviceId.contains("MACROSILICON", Qt::CaseInsensitive) && 
        audioDeviceId.contains("Openterface", Qt::CaseInsensitive)) {
        qCDebug(log_core_host_audio) << "Matched based on Openterface device identifier";
        return true;
    }
    
    return false;
}
#endif

bool AudioManager::matchAudioDeviceId(const QString& audioDeviceId, const QString& hotplugDeviceId) const
{
#ifdef Q_OS_WIN
    return matchWindowsAudioDevice(audioDeviceId, hotplugDeviceId);
#else
    return matchLinuxAudioDevice(audioDeviceId, hotplugDeviceId);
#endif
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
            
            qCDebug(log_core_host_audio) << QString("Device %1:").arg(i + 1);
            qCDebug(log_core_host_audio) << "  Description:" << device.description();
            qCDebug(log_core_host_audio) << "  Full ID:" << deviceId;
#ifdef Q_OS_WIN
            QString extractedGuid = extractAudioDeviceGuid(deviceId);
            qCDebug(log_core_host_audio) << "  Extracted GUID:" << extractedGuid;
#endif
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
    
    // Verify this is an Openterface device
    if (!inputDevice.description().contains("Openterface", Qt::CaseInsensitive)) {
        qCWarning(log_core_host_audio) << "Refusing to initialize non-Openterface audio device:" << inputDevice.description();
        return;
    }
    
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    
    if (outputDevice.isNull()) {
        qCWarning(log_core_host_audio) << "No audio output device found.";
        return;
    }
    
    try {
        // First, try to use the input device's preferred format
        QAudioFormat format = inputDevice.preferredFormat();
        
        qCDebug(log_core_host_audio) << "Input device preferred format:";
        qCDebug(log_core_host_audio) << "Sample rate:" << format.sampleRate();
        qCDebug(log_core_host_audio) << "Channel count:" << format.channelCount();
        qCDebug(log_core_host_audio) << "Sample format:" << format.sampleFormat();
        qCDebug(log_core_host_audio) << "Bytes per frame:" << format.bytesPerFrame();
        
        // Check if the input device actually supports this format
        if (!inputDevice.isFormatSupported(format)) {
            qCWarning(log_core_host_audio) << "Input device preferred format is not supported, trying alternatives";
            
            // Try common ALSA formats that work with hw:3,0
            QAudioFormat alternativeFormat;
            alternativeFormat.setSampleRate(48000);
            alternativeFormat.setChannelCount(2);
            alternativeFormat.setSampleFormat(QAudioFormat::Int16);
            
            if (inputDevice.isFormatSupported(alternativeFormat)) {
                format = alternativeFormat;
                qCDebug(log_core_host_audio) << "Using alternative format: 48000Hz, 2ch, Int16";
            } else {
                // Try 44100 Hz
                alternativeFormat.setSampleRate(44100);
                if (inputDevice.isFormatSupported(alternativeFormat)) {
                    format = alternativeFormat;
                    qCDebug(log_core_host_audio) << "Using alternative format: 44100Hz, 2ch, Int16";
                } else {
                    qCWarning(log_core_host_audio) << "No compatible format found for input device";
                }
            }
        }
        
        // Check if output device supports the same format
        if (!outputDevice.isFormatSupported(format)) {
            qCWarning(log_core_host_audio) << "Output device does not support the input format";
            QAudioFormat outputFormat = outputDevice.preferredFormat();
            qCDebug(log_core_host_audio) << "Output device preferred format:";
            qCDebug(log_core_host_audio) << "Sample rate:" << outputFormat.sampleRate();
            qCDebug(log_core_host_audio) << "Channel count:" << outputFormat.channelCount();
            qCDebug(log_core_host_audio) << "Sample format:" << outputFormat.sampleFormat();
        }
        
        // Log the final format details
        qCDebug(log_core_host_audio) << "Final audio format details:";
        qCDebug(log_core_host_audio) << "Sample rate:" << format.sampleRate();
        qCDebug(log_core_host_audio) << "Channel count:" << format.channelCount();
        qCDebug(log_core_host_audio) << "Sample format:" << format.sampleFormat();
        qCDebug(log_core_host_audio) << "Bytes per frame:" << format.bytesPerFrame();
        
        // Create and start the audio thread
        qCDebug(log_core_host_audio) << "Creating AudioThread with input device:" << inputDevice.description();
        qCDebug(log_core_host_audio) << "Input device ID:" << QString::fromUtf8(inputDevice.id());
        
        // Debug: Check if this is the ALSA device that corresponds to hw:3,0
        QString deviceId = QString::fromUtf8(inputDevice.id());
        if (deviceId.contains("usb-MACROSILICON") || deviceId.contains("card3")) {
            qCDebug(log_core_host_audio) << "This appears to be the Openterface device (card3/hw:3,0 equivalent)";
        }
        
        // Try to find alternative ALSA device names for better compatibility
        QList<QAudioDevice> allInputDevices = QMediaDevices::audioInputs();
        qCDebug(log_core_host_audio) << "Searching for alternative device names...";
        
        QAudioDevice alsaDevice;
        bool foundAlsaDevice = false;
        
        for (const QAudioDevice& device : allInputDevices) {
            QString altDeviceId = QString::fromUtf8(device.id());
            qCDebug(log_core_host_audio) << "Available device:" << device.description() << "ID:" << altDeviceId;
            
            // Look for hw:3,0 or card3 references, or different naming for same hardware
            if (altDeviceId.contains("hw:3") || altDeviceId.contains("card3") || 
                (altDeviceId.contains("3") && (altDeviceId.contains("hw") || altDeviceId.contains("USB Audio"))) ||
                (device.description().contains("MS2109") || device.description().contains("USB Audio"))) {
                qCDebug(log_core_host_audio) << "Found potential alternative ALSA device:" << device.description();
                alsaDevice = device;
                foundAlsaDevice = true;
                // Don't break - let's see all potential devices
            }
        }
        
        // Use ALSA device if found and different from current
        QAudioDevice deviceToUse = inputDevice;
        if (foundAlsaDevice && QString::fromUtf8(alsaDevice.id()) != deviceId) {
            qCDebug(log_core_host_audio) << "Found alternative ALSA device:" << alsaDevice.description();
            qCDebug(log_core_host_audio) << "Alternative device ID:" << QString::fromUtf8(alsaDevice.id());
            
            // Test if the alternative device has a different preferred format
            QAudioFormat altFormat = alsaDevice.preferredFormat();
            qCDebug(log_core_host_audio) << "Alternative device preferred format:";
            qCDebug(log_core_host_audio) << "Sample rate:" << altFormat.sampleRate();
            qCDebug(log_core_host_audio) << "Channel count:" << altFormat.channelCount();
            qCDebug(log_core_host_audio) << "Sample format:" << altFormat.sampleFormat();
            
            // For now, let's try the alternative device
            deviceToUse = alsaDevice;
            format = altFormat; // Use the alternative device's preferred format
            qCDebug(log_core_host_audio) << "Switching to alternative device for testing";
        } else {
            qCDebug(log_core_host_audio) << "No better alternative ALSA device found, using original device";
        }
        m_audioThread = new AudioThread(deviceToUse, outputDevice, format, this);
        connect(m_audioThread, &AudioThread::error, this, &AudioManager::handleAudioError);
        connect(m_audioThread, &AudioThread::cleanupRequested, this, &AudioManager::handleCleanupRequest, Qt::QueuedConnection);
        
        qCDebug(log_core_host_audio) << "Starting AudioThread...";
        m_audioThread->start();
        
        qCDebug(log_core_host_audio) << "AudioThread started, checking if running...";
        // Give it a moment to start
        QThread::msleep(50);
        if (m_audioThread->isRunning()) {
            qCDebug(log_core_host_audio) << "AudioThread is running successfully";
        } else {
            qCWarning(log_core_host_audio) << "AudioThread failed to start or exited immediately";
        }
        
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
                qCDebug(log_core_host_audio) << "AudioManager: Device unplugged at port:" << device.portChain;
                
                // Only deactivate audio device if the device has an audio component
                if (!device.hasAudioDevice()) {
                    qCDebug(log_core_host_audio) << "Device at port" << device.portChain << "has no audio component, skipping audio deactivation";
                    return;
                }
                
                // Verify this is an Openterface device by checking the audio device ID
                if (!device.audioDeviceId.contains("Openterface", Qt::CaseInsensitive)) {
                    qCDebug(log_core_host_audio) << "Device at port" << device.portChain << "is not an Openterface audio device, skipping audio deactivation";
                    return;
                }
                
                // Check if the unplugged device matches the current audio device
                if (m_currentAudioPortChain == device.portChain) {
                    qCInfo(log_core_host_audio) << "Current Openterface audio device unplugged, stopping audio capture";
                    disconnect();
                    qCInfo(log_core_host_audio) << "✓ Audio capture stopped for unplugged device at port:" << device.portChain;
                    emit audioDisconnected();
                } else {
                    qCDebug(log_core_host_audio) << "Audio device deactivation skipped - port chain mismatch. Current:" << m_currentAudioPortChain << "Unplugged:" << device.portChain;
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_core_host_audio) << "AudioManager: New device plugged in at port:" << device.portChain;
                
                // Only attempt auto-switch if the device has an audio component
                if (!device.hasAudioDevice()) {
                    qCDebug(log_core_host_audio) << "Device at port" << device.portChain << "has no audio component, skipping audio auto-start";
                    return;
                }
                
                // Verify this is an Openterface device by checking the audio device ID
                if (!device.audioDeviceId.contains("Openterface", Qt::CaseInsensitive)) {
                    qCDebug(log_core_host_audio) << "Device at port" << device.portChain << "is not an Openterface audio device, skipping audio auto-start";
                    return;
                }
                
                // Check if there's currently an active audio device
                if (m_audioThread != nullptr) {
                    qCDebug(log_core_host_audio) << "Audio device already active, skipping auto-start for port:" << device.portChain;
                    return;
                }
                
                qCInfo(log_core_host_audio) << "Openterface audio device detected, starting audio capture automatically";
                
                // Give the newly plugged device some time to fully initialize
                // Audio devices often need a moment to become ready after hotplug
                qCDebug(log_core_host_audio) << "Waiting 500ms for new audio device to initialize...";
                QThread::msleep(500);
                
                // Switch to the new audio device and start capturing
                bool switchSuccess = switchToAudioDeviceByPortChain(device.portChain);
                if (switchSuccess) {
                    qCInfo(log_core_host_audio) << "✓ Successfully started audio capture for new Openterface device at port:" << device.portChain;
                } else {
                    qCWarning(log_core_host_audio) << "Failed to start audio capture for new Openterface device at port:" << device.portChain;
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
