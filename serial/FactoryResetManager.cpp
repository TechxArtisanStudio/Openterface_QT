#include "FactoryResetManager.h"
#include "SerialPortManager.h"
#include "protocol/SerialProtocol.h" // For CmdGetInfoResult usage

#include <QTimer>
#include <QEventLoop>
#include <QDebug>

// Use the same logging category as SerialPortManager - declared in SerialPortManager.cpp
Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

FactoryResetManager::FactoryResetManager(SerialPortManager* owner, QObject* parent)
    : QObject(parent), m_owner(owner)
{
}

bool FactoryResetManager::handleFactoryResetInternal()
{
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetInternal START ==================";
    
    if (!m_owner) {
        qCWarning(log_core_serial) << "FactoryResetManager: m_owner is null";
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetInternal END (FAILED - null owner) ==================";
        return false;
    }

    qCDebug(log_core_serial) << "FactoryResetManager: starting async RTS factory reset";
    qCDebug(log_core_serial) << "  - m_owner=" << static_cast<void*>(m_owner);
    qCDebug(log_core_serial) << "  - serialPort=" << static_cast<void*>(m_owner->serialPort);
    qCDebug(log_core_serial) << "  - isOpen=" << (m_owner->serialPort && m_owner->serialPort->isOpen());

    // Clear stored baudrate
    m_owner->clearStoredBaudrate();

    if (!m_owner->serialPort) {
        qCWarning(log_core_serial) << "FactoryResetManager: serial port is null, cannot factory reset";
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetInternal END (FAILED - null port) ==================";
        emit factoryResetCompleted(false);
        return false;
    }

    QString currentPortName = m_owner->serialPort->portName();
    qCDebug(log_core_serial) << "  - currentPortName=" << currentPortName;

    qCDebug(log_core_serial) << "  - Setting RTS to low...";
    if (m_owner->serialPort->setRequestToSend(true)) {
        qCDebug(log_core_serial) << "  - ✓ RTS set to low successfully";
        emit factoryReset(true);
        qCDebug(log_core_serial) << "FactoryResetManager: Set RTS to low";

        QTimer::singleShot(4000, this, [this, currentPortName]() {
            qCDebug(log_core_serial) << "  [4s timer] Setting RTS to high...";
            bool success = false;
            if (m_owner->serialPort && m_owner->serialPort->setRequestToSend(false)) {
                qCDebug(log_core_serial) << "  [4s timer] ✓ RTS set to high successfully";
                emit factoryReset(false);

                // Give device time to reboot and then reinitialize connection
                QTimer::singleShot(500, this, [this, currentPortName]() {
                    qCDebug(log_core_serial) << "  [4.5s timer] Reinitializing connection after factory reset";
                    if (m_owner->serialPort && m_owner->serialPort->isOpen()) {
                        qCDebug(log_core_serial) << "  [4.5s timer] Calling m_owner->closePort()...";
                        m_owner->closePort();
                        qCDebug(log_core_serial) << "  [4.5s timer] m_owner->closePort() completed";
                    }
                    QTimer::singleShot(2000, this, [this, currentPortName]() {
                        qCDebug(log_core_serial) << "  [6.5s timer] Reconnecting to port after factory reset:" << currentPortName;
                        // Use signal instead of direct call so onSerialPortConnected runs
                        // in the SerialWorkerThread via the existing QueuedConnection,
                        // preventing cross-thread QSerialPort creation and data races.
                        emit m_owner->serialPortConnected(currentPortName);
                        qCDebug(log_core_serial) << "  [6.5s timer] serialPortConnected signal emitted, waiting for reconnection...";
                        // Let SerialPortManager handle ready-state polling / retries
                    });
                });

                success = true;
            } else {
                qCWarning(log_core_serial) << "  [4s timer] Failed to set RTS to high";
                emit factoryResetCompleted(false);
            }
        });
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetInternal END (async initiated) ==================";
        return true;
    }

    qCWarning(log_core_serial) << "FactoryResetManager: Failed to set RTS to low";
    emit factoryResetCompleted(false);
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetInternal END (FAILED - RTS) ==================";
    return false;
}

bool FactoryResetManager::handleFactoryResetV191Internal()
{
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191Internal START ==================";
    
    if (!m_owner) {
        qCWarning(log_core_serial) << "FactoryResetManager: m_owner is null";
        return false;
    }

    qCDebug(log_core_serial) << "FactoryResetManager: starting V1.9.1 factory reset (command method)";
    emit m_owner->statusUpdate("Factory reset Hid chip now.");

    m_owner->clearStoredBaudrate();

    if (!m_owner->serialPort) {
        qCWarning(log_core_serial) << "FactoryResetManager: serial port is null, cannot factory reset";
        emit factoryResetCompleted(false);
        return false;
    }

    // CH32V208 only supports 115200
    if (m_owner->isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip detected - attempting factory reset at 115200 only";
        QByteArray retByte = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
        if (retByte.size() > 0) {
            qCDebug(log_core_serial) << "Factory reset the hid chip success.";
            emit m_owner->statusUpdate("Factory reset the hid chip success.");
            emit factoryResetCompleted(true);
            qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191Internal END (SUCCESS) ==================";
            return true;
        } else {
            qCWarning(log_core_serial) << "CH32V208 chip factory reset failed - chip may not support this command";
            emit m_owner->statusUpdate("Factory reset the hid chip failure.");
            emit factoryResetCompleted(false);
            qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191Internal END (FAILED - CH32V208) ==================";
            return false;
        }
    }

    // CH9329 and unknown: try current baudrate, fallback to alternative
    qCDebug(log_core_serial) << "  - Attempting command-based factory reset at current baudrate...";
    QByteArray retByte = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (retByte.size() > 0) {
        qCDebug(log_core_serial) << "Factory reset the hid chip success.";
        emit m_owner->statusUpdate("Factory reset the hid chip success.");
        emit factoryResetCompleted(true);
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191Internal END (SUCCESS) ==================";
        return true;
    } else {
        qCDebug(log_core_serial) << "Factory reset the hid chip fail. Trying alternate baudrate...";
        if (m_owner->serialPort) {
            qCDebug(log_core_serial) << "  - Closing serial port...";
            m_owner->serialPort->close();
            qCDebug(log_core_serial) << "  - Setting baudrate to alternate...";
            m_owner->setBaudRate(m_owner->anotherBaudrate());
            emit m_owner->statusUpdate("Factory reset the hid chip@9600.");
            qCDebug(log_core_serial) << "  - Attempting to reopen serial port...";
            if (m_owner->serialPort->open(QIODevice::ReadWrite)) {
                qCDebug(log_core_serial) << "  - Serial port reopened, sending factory reset command again...";
                QByteArray retAlt = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
                if (retAlt.size() > 0) {
                    qCDebug(log_core_serial) << "Factory reset the hid chip success (alt).";
                    emit m_owner->statusUpdate("Factory reset the hid chip success@9600.");
                    emit factoryResetCompleted(true);
                    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191Internal END (SUCCESS - alt) ==================";
                    return true;
                }
            }
        }
    }

    emit m_owner->statusUpdate("Factory reset the hid chip failure.");
    emit factoryResetCompleted(false);
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191Internal END (FAILED) ==================";
    return false;
}

bool FactoryResetManager::handleFactoryResetSyncInternal(int timeoutMs)
{
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetSyncInternal START ==================";
    qCDebug(log_core_serial) << "  - timeoutMs=" << timeoutMs;
    
    if (!m_owner || !m_owner->serialPort) {
        qCWarning(log_core_serial) << "FactoryResetManager: m_owner or serialPort is null";
        return false;
    }

    if (!m_owner->serialPort->isOpen()) {
        qCWarning(log_core_serial) << "FactoryResetManager sync: serial port is not open, cannot factory reset";
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetSyncInternal END (FAILED - closed) ==================";
        return false;
    }

    QString currentPortName = m_owner->serialPort->portName();
    qCInfo(log_core_serial) << "FactoryResetManager sync: Factory reset on port:" << currentPortName;

    // Step 1: Set RTS low
    qCDebug(log_core_serial) << "  - Step 1: Setting RTS to low...";
    if (!m_owner->serialPort->setRequestToSend(true)) {
        qCWarning(log_core_serial) << "  - Step 1 FAILED: Failed to set RTS to low for factory reset";
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetSyncInternal END (FAILED - RTS low) ==================";
        return false;
    }
    emit factoryReset(true);
    qCDebug(log_core_serial) << "  - Step 1 SUCCESS: RTS set to low";

    // Wait 4 seconds
    qCDebug(log_core_serial) << "  - Step 2: Waiting 4 seconds for device reset...";
    QEventLoop waitLoop;
    QTimer::singleShot(4000, &waitLoop, &QEventLoop::quit);
    waitLoop.exec();
    qCDebug(log_core_serial) << "  - Step 2 COMPLETED: 4 second wait finished";

    // Step 3: Set RTS high
    qCDebug(log_core_serial) << "  - Step 3: Setting RTS to high...";
    if (!m_owner->serialPort->setRequestToSend(false)) {
        qCWarning(log_core_serial) << "  - Step 3 FAILED: Failed to set RTS to high after factory reset";
        emit factoryReset(false);
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetSyncInternal END (FAILED - RTS high) ==================";
        return false;
    }
    emit factoryReset(false);
    qCDebug(log_core_serial) << "  - Step 3 SUCCESS: RTS set to high";

    // Close and give time to reboot
    qCDebug(log_core_serial) << "  - Step 4: Closing serial port...";
    if (m_owner->serialPort && m_owner->serialPort->isOpen()) {
        m_owner->closePort();
    }
    qCDebug(log_core_serial) << "  - Step 4 COMPLETED: Serial port closed";
    
    qCDebug(log_core_serial) << "  - Step 5: Waiting 2 seconds for device stabilization...";
    QEventLoop stabilizeLoop;
    QTimer::singleShot(2000, &stabilizeLoop, &QEventLoop::quit);
    stabilizeLoop.exec();
    qCDebug(log_core_serial) << "  - Step 5 COMPLETED: 2 second stabilization wait finished";

    // Try to reconnect and verify
    qCDebug(log_core_serial) << "  - Step 6: Attempting to reconnect and verify...";
    bool reconnectSuccess = false;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        qCDebug(log_core_serial) << "    - Reconnect attempt" << attempt << "/5...";
        m_owner->onSerialPortConnected(currentPortName);
        QEventLoop connectWait;
        QTimer::singleShot(1000, &connectWait, &QEventLoop::quit);
        connectWait.exec();

        if (m_owner->ready && m_owner->serialPort && m_owner->serialPort->isOpen()) {
            qCDebug(log_core_serial) << "    - Device reconnected, sending CMD_GET_INFO...";
            QByteArray response = m_owner->sendSyncCommand(CMD_GET_INFO, true);
            if (!response.isEmpty() && response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(response);
                if (result.prefix == 0xAB57) {
                    qCDebug(log_core_serial) << "    - ✓ CMD_GET_INFO verification successful, prefix=0x" << QString::number(result.prefix, 16);
                    reconnectSuccess = true;
                    break;
                }
            }
            qCDebug(log_core_serial) << "    - ✗ CMD_GET_INFO verification failed";
        } else {
            qCDebug(log_core_serial) << "    - ✗ Device not reconnected yet";
        }

        if (attempt < 5) {
            qCDebug(log_core_serial) << "    - Waiting before next attempt...";
            QEventLoop retryWait;
            QTimer::singleShot(1000 * attempt, &retryWait, &QEventLoop::quit);
            retryWait.exec();
        }
    }

    qCDebug(log_core_serial) << "  - Step 6 COMPLETED: Reconnect attempts finished, success=" << reconnectSuccess;

    if (reconnectSuccess) {
        qCInfo(log_core_serial) << "Synchronous factory reset completed successfully";
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetSyncInternal END (SUCCESS) ==================";
        return true;
    }
    qCWarning(log_core_serial) << "Synchronous factory reset failed - device not responding after attempts";
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetSyncInternal END (FAILED) ==================";
    return false;
}

bool FactoryResetManager::handleFactoryResetV191SyncInternal(int timeoutMs)
{
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191SyncInternal START ==================";
    qCDebug(log_core_serial) << "  - timeoutMs=" << timeoutMs;
    
    if (!m_owner || !m_owner->serialPort) {
        qCWarning(log_core_serial) << "FactoryResetManager: m_owner or serialPort is null";
        return false;
    }

    if (!m_owner->serialPort->isOpen()) {
        qCWarning(log_core_serial) << "FactoryResetManager V191 sync: serial port is not open, cannot factory reset";
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191SyncInternal END (FAILED - closed) ==================";
        return false;
    }

    QString currentPortName = m_owner->serialPort->portName();
    qCInfo(log_core_serial) << "V191 Factory reset on port:" << currentPortName;

    // Send CMD_SET_DEFAULT_CFG
    qCDebug(log_core_serial) << "  - Sending CMD_SET_DEFAULT_CFG command...";
    QByteArray retByte = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (retByte.size() > 0) {
        qCDebug(log_core_serial) << "  - ✓ CMD_SET_DEFAULT_CFG command sent successfully, response size=" << retByte.size();
        
        // Wait and verify
        qCDebug(log_core_serial) << "  - Waiting 2 seconds for device reset...";
        QEventLoop waitLoop;
        QTimer::singleShot(2000, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
        qCDebug(log_core_serial) << "  - 2 second wait finished";

        bool verificationSuccess = false;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            qCDebug(log_core_serial) << "    - Verification attempt" << attempt << "/3...";
            QByteArray verifyByte = m_owner->sendSyncCommand(CMD_GET_INFO, true);
            if (!verifyByte.isEmpty() && verifyByte.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(verifyByte);
                if (result.prefix == 0xAB57) {
                    qCDebug(log_core_serial) << "    - ✓ CMD_GET_INFO verification successful";
                    verificationSuccess = true;
                    break;
                }
            }
            qCDebug(log_core_serial) << "    - ✗ CMD_GET_INFO verification failed";
            
            if (attempt < 3) {
                qCDebug(log_core_serial) << "    - Waiting before next verification attempt...";
                QEventLoop retryWait;
                QTimer::singleShot(500, &retryWait, &QEventLoop::quit);
                retryWait.exec();
            }
        }

        qCDebug(log_core_serial) << "  - Verification phase completed, success=" << verificationSuccess;
        qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191SyncInternal END (result=" << verificationSuccess << ") ==================";
        return verificationSuccess;
    }

    qCWarning(log_core_serial) << "  - ✗ CMD_SET_DEFAULT_CFG command failed, no response";
    qCDebug(log_core_serial) << "================== FactoryResetManager::handleFactoryResetV191SyncInternal END (FAILED) ==================";
    return false;
}