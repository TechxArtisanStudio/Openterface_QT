#include "FactoryResetManager.h"
#include "SerialPortManager.h"
#include "protocol/SerialProtocol.h" // For CmdGetInfoResult usage

#include <QTimer>
#include <QEventLoop>
#include <QDebug>

FactoryResetManager::FactoryResetManager(SerialPortManager* owner, QObject* parent)
    : QObject(parent), m_owner(owner)
{
}

bool FactoryResetManager::handleFactoryResetInternal()
{
    if (!m_owner) return false;

    qCDebug(log_core_serial) << "FactoryResetManager: starting async RTS factory reset";

    // Clear stored baudrate
    m_owner->clearStoredBaudrate();

    if (!m_owner->serialPort) {
        qCWarning(log_core_serial) << "FactoryResetManager: serial port is null, cannot factory reset";
        emit factoryResetCompleted(false);
        return false;
    }

    QString currentPortName = m_owner->serialPort->portName();

    if (m_owner->serialPort->setRequestToSend(true)) {
        emit factoryReset(true);
        qCDebug(log_core_serial) << "FactoryResetManager: Set RTS to low";

        QTimer::singleShot(4000, this, [this, currentPortName]() {
            bool success = false;
            if (m_owner->serialPort && m_owner->serialPort->setRequestToSend(false)) {
                qCDebug(log_core_serial) << "FactoryResetManager: Set RTS to high";
                emit factoryReset(false);

                // Give device time to reboot and then reinitialize connection
                QTimer::singleShot(500, this, [this, currentPortName]() {
                    qCDebug(log_core_serial) << "FactoryResetManager: Reinitializing connection after factory reset";
                    if (m_owner->serialPort && m_owner->serialPort->isOpen()) {
                        m_owner->closePort();
                    }
                    QTimer::singleShot(2000, this, [this, currentPortName]() {
                        qCDebug(log_core_serial) << "FactoryResetManager: Reconnecting to port after factory reset:" << currentPortName;
                        m_owner->onSerialPortConnected(currentPortName);
                        // Let SerialPortManager handle ready-state polling / retries
                    });
                });

                success = true;
            } else {
                emit factoryResetCompleted(false);
            }
        });
        return true;
    }

    emit factoryResetCompleted(false);
    return false;
}

bool FactoryResetManager::handleFactoryResetV191Internal()
{
    if (!m_owner) return false;

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
            return true;
        } else {
            qCWarning(log_core_serial) << "CH32V208 chip factory reset failed - chip may not support this command";
            emit m_owner->statusUpdate("Factory reset the hid chip failure.");
            emit factoryResetCompleted(false);
            return false;
        }
    }

    // CH9329 and unknown: try current baudrate, fallback to alternative
    QByteArray retByte = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (retByte.size() > 0) {
        qCDebug(log_core_serial) << "Factory reset the hid chip success.";
        emit m_owner->statusUpdate("Factory reset the hid chip success.");
        emit factoryResetCompleted(true);
        return true;
    } else {
        qCDebug(log_core_serial) << "Factory reset the hid chip fail. Trying alternate baudrate...";
        if (m_owner->serialPort) {
            m_owner->serialPort->close();
            m_owner->setBaudRate(m_owner->anotherBaudrate());
            emit m_owner->statusUpdate("Factory reset the hid chip@9600.");
            if (m_owner->serialPort->open(QIODevice::ReadWrite)) {
                QByteArray retAlt = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
                if (retAlt.size() > 0) {
                    qCDebug(log_core_serial) << "Factory reset the hid chip success (alt).";
                    emit m_owner->statusUpdate("Factory reset the hid chip success@9600.");
                    emit factoryResetCompleted(true);
                    return true;
                }
            }
        }
    }

    emit m_owner->statusUpdate("Factory reset the hid chip failure.");
    emit factoryResetCompleted(false);
    return false;
}

bool FactoryResetManager::handleFactoryResetSyncInternal(int timeoutMs)
{
    if (!m_owner || !m_owner->serialPort) return false;

    if (!m_owner->serialPort->isOpen()) {
        qCWarning(log_core_serial) << "FactoryResetManager sync: serial port is not open, cannot factory reset";
        return false;
    }

    QString currentPortName = m_owner->serialPort->portName();
    qCInfo(log_core_serial) << "FactoryResetManager sync: Factory reset on port:" << currentPortName;

    // Step 1: Set RTS low
    if (!m_owner->serialPort->setRequestToSend(true)) {
        qCWarning(log_core_serial) << "Failed to set RTS to low for factory reset";
        return false;
    }
    emit factoryReset(true);

    // Wait 4 seconds
    QEventLoop waitLoop;
    QTimer::singleShot(4000, &waitLoop, &QEventLoop::quit);
    waitLoop.exec();

    // Step 3: Set RTS high
    if (!m_owner->serialPort->setRequestToSend(false)) {
        qCWarning(log_core_serial) << "Failed to set RTS to high after factory reset";
        emit factoryReset(false);
        return false;
    }
    emit factoryReset(false);

    // Close and give time to reboot
    if (m_owner->serialPort && m_owner->serialPort->isOpen()) {
        m_owner->closePort();
    }
    QEventLoop stabilizeLoop;
    QTimer::singleShot(2000, &stabilizeLoop, &QEventLoop::quit);
    stabilizeLoop.exec();

    // Try to reconnect and verify
    bool reconnectSuccess = false;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        m_owner->onSerialPortConnected(currentPortName);
        QEventLoop connectWait;
        QTimer::singleShot(1000, &connectWait, &QEventLoop::quit);
        connectWait.exec();

        if (m_owner->ready && m_owner->serialPort && m_owner->serialPort->isOpen()) {
            QByteArray response = m_owner->sendSyncCommand(CMD_GET_INFO, true);
            if (!response.isEmpty() && response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(response);
                if (result.prefix == 0xAB57) {
                    reconnectSuccess = true;
                    break;
                }
            }
        }

        if (attempt < 5) {
            QEventLoop retryWait;
            QTimer::singleShot(1000 * attempt, &retryWait, &QEventLoop::quit);
            retryWait.exec();
        }
    }

    if (reconnectSuccess) {
        qCInfo(log_core_serial) << "Synchronous factory reset completed successfully";
        return true;
    }
    qCWarning(log_core_serial) << "Synchronous factory reset failed - device not responding after attempts";
    return false;
}

bool FactoryResetManager::handleFactoryResetV191SyncInternal(int timeoutMs)
{
    if (!m_owner || !m_owner->serialPort) return false;

    if (!m_owner->serialPort->isOpen()) {
        qCWarning(log_core_serial) << "FactoryResetManager V191 sync: serial port is not open, cannot factory reset";
        return false;
    }

    QString currentPortName = m_owner->serialPort->portName();
    qCInfo(log_core_serial) << "V191 Factory reset on port:" << currentPortName;

    // Send CMD_SET_DEFAULT_CFG
    QByteArray retByte = m_owner->sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (retByte.size() > 0) {
        // Wait and verify
        QEventLoop waitLoop;
        QTimer::singleShot(2000, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();

        bool verificationSuccess = false;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            QByteArray verifyByte = m_owner->sendSyncCommand(CMD_GET_INFO, true);
            if (!verifyByte.isEmpty() && verifyByte.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(verifyByte);
                if (result.prefix == 0xAB57) {
                    verificationSuccess = true;
                    break;
                }
            }
            if (attempt < 3) {
                QEventLoop retryWait;
                QTimer::singleShot(500, &retryWait, &QEventLoop::quit);
                retryWait.exec();
            }
        }

        return verificationSuccess;
    }

    return false;
}
