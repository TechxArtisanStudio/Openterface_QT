#ifndef SERIAL_PORT_RACE_TEST_H
#define SERIAL_PORT_RACE_TEST_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QSignalSpy>
#include <QSerialPort>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

/**
 * @brief Test helper that simulates rapid plug/unplug cycles to trigger race conditions.
 *
 * This replicates the crash scenario from the Windows 11 logs:
 * - Device disconnected (Error code 9 - ResourceError)
 * - Multiple rapid error events
 * - New connection attempt initiated during cleanup
 * - Serial port deleted while I/O operations still in flight
 */
class SerialPortRaceSimulator : public QObject {
    Q_OBJECT
public:
    explicit SerialPortRaceSimulator(QObject* parent = nullptr)
        : QObject(parent)
        , m_simulatedPort(nullptr)
        , m_errorInjectionEnabled(false)
        , m_unplugAfterMs(0)
    {}

    /**
     * @brief Enable error injection to simulate device disconnect.
     * @param unplugAfterMs Simulate unplug after this many ms from open
     */
    void enableErrorInjection(int unplugAfterMs = 100) {
        m_errorInjectionEnabled = true;
        m_unplugAfterMs = unplugAfterMs;
    }

    /**
     * @brief Create a simulated serial port that will inject errors.
     */
    QSerialPort* createSimulatedPort(const QString& portName) {
        m_simulatedPort = new QSerialPort(this);
        m_simulatedPort->setPortName(portName);

        if (m_errorInjectionEnabled) {
            // Schedule error injection after port is opened
            QTimer::singleShot(m_unplugAfterMs, this, [this]() {
                injectResourceError();
            });
        }

        return m_simulatedPort;
    }

    /**
     * @brief Simulate QSerialPort::ResourceError (error code 9).
     */
    void injectResourceError() {
        if (!m_simulatedPort) return;

        // Emit errorOccurred signal with ResourceError
        emit m_simulatedPort->errorOccurred(QSerialPort::ResourceError);

        // Also simulate the port being closed by the OS
        if (m_simulatedPort->isOpen()) {
            m_simulatedPort->close();
        }
    }

signals:
    void errorInjected();

private:
    QSerialPort* m_simulatedPort;
    bool m_errorInjectionEnabled;
    int m_unplugAfterMs;
};

/**
 * @brief Test worker that performs rapid open/close cycles in a separate thread.
 */
class RapidOpenCloseWorker : public QObject {
    Q_OBJECT
public:
    explicit RapidOpenCloseWorker(QObject* parent = nullptr)
        : QObject(parent)
        , m_running(false)
        , m_cycleCount(0)
        , m_errorCount(0)
    {}

public slots:
    void startRapidCycles(const QString& portName, int cycles = 50, int intervalMs = 10) {
        m_running = true;
        m_cycleCount = 0;
        m_errorCount = 0;

        QElapsedTimer timer;
        timer.start();

        for (int i = 0; i < cycles && m_running; ++i) {
            // Try to open
            QSerialPort port;
            port.setPortName(portName);
            port.setBaudRate(QSerialPort::Baud9600);

            bool opened = port.open(QIODevice::ReadWrite);
            if (opened) {
                port.close();
                m_cycleCount++;
            } else {
                m_errorCount++;
            }

            // Brief delay between cycles
            QThread::msleep(intervalMs);
        }

        emit cyclesCompleted(m_cycleCount, m_errorCount, timer.elapsed());
    }

    void stop() {
        m_running = false;
    }

signals:
    void cyclesCompleted(int successCount, int errorCount, qint64 elapsedMs);

private:
    std::atomic<bool> m_running;
    int m_cycleCount;
    int m_errorCount;
};

#endif // SERIAL_PORT_RACE_TEST_H
