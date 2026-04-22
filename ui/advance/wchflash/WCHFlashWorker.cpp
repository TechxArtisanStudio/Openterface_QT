#include "WCHFlashWorker.h"

#include "wch/WCHUSBTransport.h"
#include "wch/WCHFlasher.h"
#include "wch/WCHHexParser.h"

#include <QFile>
#include <memory>

// ---------------------------------------------------------------------------
// Impl — holds backend objects
// ---------------------------------------------------------------------------
class WCHFlashWorker::Impl {
public:
    std::unique_ptr<WCHUSBTransport> transport;
    std::unique_ptr<WCHFlasher>      flasher;
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
WCHFlashWorker::WCHFlashWorker(QObject* parent)
    : QObject(parent)
    , m_impl(new Impl())
{
    try {
        m_impl->transport = std::make_unique<WCHUSBTransport>();
    } catch (const WCHTransportError& e) {
        // libusb init failed; transport stays null; errors will be reported on use
        m_impl->transport.reset();
        emit logMessage(QString("libusb init error: %1").arg(e.what()));
    }
}

WCHFlashWorker::~WCHFlashWorker()
{
    if (m_impl->transport && m_impl->transport->isOpen())
        m_impl->transport->close();
    delete m_impl;
}

// ---------------------------------------------------------------------------
// scanDevices
// ---------------------------------------------------------------------------
void WCHFlashWorker::scanDevices()
{
    if (!m_impl->transport) {
        emit devicesFound({});
        emit logMessage("USB transport not available (libusb init failed)");
        return;
    }

    try {
        auto names = m_impl->transport->scanDevices();
        QStringList list;
        for (const auto& n : names)
            list << QString::fromStdString(n);
        emit devicesFound(list);
        emit logMessage(QString("Found %1 WCH ISP device(s)").arg(list.size()));
    } catch (const WCHTransportError& e) {
        emit devicesFound({});
        emit logMessage(QString("Scan error: %1").arg(e.what()));
    }
}

// ---------------------------------------------------------------------------
// connectDevice
// ---------------------------------------------------------------------------
void WCHFlashWorker::connectDevice(int index)
{
    if (!m_impl->transport) {
        emit finished(false, "USB transport not available");
        return;
    }

    // Close any existing connection
    if (m_impl->transport->isOpen()) {
        m_impl->flasher.reset();
        m_impl->transport->close();
    }

    try {
        emit logMessage(QString("Opening device %1...").arg(index));
        m_impl->transport->open(index);

        emit logMessage("Identifying chip...");
        m_impl->flasher = std::make_unique<WCHFlasher>(m_impl->transport.get());

        QString info = QString::fromStdString(m_impl->flasher->getChipInfo());
        emit deviceConnected(info);
        emit logMessage("Device connected successfully");
    } catch (const std::exception& e) {
        m_impl->flasher.reset();
        if (m_impl->transport->isOpen())
            m_impl->transport->close();
        emit finished(false, QString("Connect failed: %1").arg(e.what()));
        emit logMessage(QString("Connect error: %1").arg(e.what()));
    }
}

// ---------------------------------------------------------------------------
// disconnectDevice
// ---------------------------------------------------------------------------
void WCHFlashWorker::disconnectDevice()
{
    m_impl->flasher.reset();
    if (m_impl->transport && m_impl->transport->isOpen())
        m_impl->transport->close();
    emit deviceDisconnected();
    emit logMessage("Device disconnected");
}

// ---------------------------------------------------------------------------
// flashFirmware
// ---------------------------------------------------------------------------
void WCHFlashWorker::flashFirmware(const QString& filePath)
{
    if (!m_impl->flasher) {
        emit finished(false, "No device connected. Connect first.");
        return;
    }

    try {
        emit logMessage(QString("Parsing firmware: %1").arg(filePath));
        emit progress(0, "Parsing firmware file...");

        // Use QFile to open the path so that Unicode / non-ASCII paths work
        // correctly on all platforms (std::ifstream may fail on Windows with
        // Chinese or other non-Latin directory names).
        QFile qfile(filePath);
        if (!qfile.open(QIODevice::ReadOnly))
            throw WCHHexParseError(
                "Cannot open file: " + filePath.toStdString());
        QByteArray qbytes = qfile.readAll();
        qfile.close();
        std::vector<uint8_t> rawBytes(
            reinterpret_cast<const uint8_t*>(qbytes.constData()),
            reinterpret_cast<const uint8_t*>(qbytes.constData()) + qbytes.size());

        auto firmware = WCHHexParser::parseData(rawBytes);

        emit logMessage(QString("Firmware size: %1 bytes").arg(firmware.size()));
        emit progress(2, QString("Firmware loaded: %1 bytes").arg(firmware.size()));

        // Flash with progress relay.
        // emit logMessage only for milestone messages, not every per-chunk
        // byte-count update (which would flood the log with thousands of lines).
        m_impl->flasher->flash(firmware, [this](int pct, const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            emit progress(pct, qmsg);
            if (!qmsg.startsWith("Programming:") && !qmsg.startsWith("Verifying:"))
                emit logMessage(qmsg);
        });

        // Device has reset — close the handle
        m_impl->flasher.reset();
        m_impl->transport->close();
        emit deviceDisconnected();

        emit progress(100, "Done!");
        emit finished(true, "Firmware flashed and verified successfully.\n"
                            "Please reconnect the device.");

    } catch (const WCHHexParseError& e) {
        emit finished(false, QString("File parse error: %1").arg(e.what()));
        emit logMessage(QString("Parse error: %1").arg(e.what()));
    } catch (const WCHFlashError& e) {
        emit finished(false, QString("Flash error: %1").arg(e.what()));
        emit logMessage(QString("Flash error: %1").arg(e.what()));
    } catch (const std::exception& e) {
        emit finished(false, QString("Unexpected error: %1").arg(e.what()));
        emit logMessage(QString("Error: %1").arg(e.what()));
    }
}
