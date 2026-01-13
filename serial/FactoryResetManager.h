#ifndef FACTORYRESETMANAGER_H
#define FACTORYRESETMANAGER_H

#include <QObject>

class SerialPortManager;

// FactoryResetManager
// -------------------
// Extracted helper that encapsulates factory reset behaviour for different hardware:
//  - CH32V208: uses RTS-based reset (close/reopen at 115200)
//  - CH9329: supports RTS-based reset; V1.9.1 uses CMD_SET_DEFAULT_CFG command with optional baudrate fallback
//  - Unknown chips: attempt safe fallbacks and preserve behavior found in SerialPortManager
//
// This component keeps all factory reset logic in one place to make testing and compatibility
// decisions easier. It emits signals that are forwarded by SerialPortManager for backward
// compatibility with existing UI code.
class FactoryResetManager : public QObject
{
    Q_OBJECT
public:
    explicit FactoryResetManager(SerialPortManager* owner, QObject* parent = nullptr);

    // Asynchronous wrappers used by SerialPortManager (run in worker thread)
    bool handleFactoryResetInternal();
    bool handleFactoryResetV191Internal();

    // Synchronous variants for diagnostics (blocking)
    bool handleFactoryResetSyncInternal(int timeoutMs);
    bool handleFactoryResetV191SyncInternal(int timeoutMs);

signals:
    // Forwarded by SerialPortManager for compatibility
    void factoryReset(bool isStarted);
    void factoryResetCompleted(bool success);

private:
    SerialPortManager* m_owner; // non-owning pointer back to the manager
};

#endif // FACTORYRESETMANAGER_H
