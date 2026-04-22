#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// WCHFlashWorker
//
// QObject-based worker that wraps the WCH backend library.
// Move to a QThread before calling any slot.
//
// Typical usage:
//   QThread* t = new QThread();
//   WCHFlashWorker* w = new WCHFlashWorker();
//   w->moveToThread(t);
//   t->start();
//   connect(btn, &QPushButton::clicked, w, &WCHFlashWorker::scanDevices);
//   connect(w, &WCHFlashWorker::devicesFound, dialog, &WCHFlashDialog::onDevicesFound);
// ---------------------------------------------------------------------------
class WCHFlashWorker : public QObject {
    Q_OBJECT

public:
    explicit WCHFlashWorker(QObject* parent = nullptr);
    ~WCHFlashWorker() override;

public slots:
    // Scan for WCH ISP devices; emits devicesFound()
    void scanDevices();

    // Open device by index (from last scan); emits connected() or error()
    void connectDevice(int index);

    // Close current device; emits disconnected()
    void disconnectDevice();

    // Flash firmware file (.hex or .bin); emits progress() then finished()
    void flashFirmware(const QString& filePath);

signals:
    // Scan result — list of human-readable device descriptions
    void devicesFound(const QStringList& devices);

    // Device connected; chipInfo is a multi-line info string
    void deviceConnected(const QString& chipInfo);

    // Device disconnected
    void deviceDisconnected();

    // Flash/verify progress (0–100) + status message
    void progress(int percent, const QString& message);

    // Operation finished; success==true means all stages passed
    void finished(bool success, const QString& message);

    // Non-fatal log message (info/warning)
    void logMessage(const QString& message);

private:
    // pImpl to avoid pulling libusb into the header
    class Impl;
    Impl* m_impl = nullptr;
};
