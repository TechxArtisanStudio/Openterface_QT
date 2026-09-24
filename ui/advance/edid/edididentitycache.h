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

#ifndef EDIDIDENTITYCACHE_H
#define EDIDIDENTITYCACHE_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>

#include "edididentity.h"

namespace edid {

class EdidSettingsManager;

// In-memory map "port chain -> EDID identity" of the units this process has
// had selected. VideoHid is bound to one unit at a time, so a unit's name is
// learned when it becomes the current device (a few seconds after
// VideoHid::hidDeviceConnected, off the start-up path) and kept until the
// app exits. Nothing is persisted.
class EdidIdentityCache : public QObject
{
    Q_OBJECT
public:
    static EdidIdentityCache& instance();

    QString displayName(const QString &portChain) const;
    EdidIdentity identity(const QString &portChain) const;
    bool hasIdentity(const QString &portChain) const { return m_byPortChain.contains(portChain); }

    // Port chain of the currently selected unit, as the device menu knows it
    // (set by refresh(); empty before the first selection).
    QString currentPortChain() const { return m_currentPortChain; }
    // Name of the currently selected unit (empty if unknown).
    QString currentDisplayName() const { return displayName(m_currentPortChain); }

public slots:
    // A unit was selected: remember its port chain (the device menu's key --
    // VideoHid's and GlobalSetting's notions of "port chain" differ on some
    // topologies) and read its identity shortly.
    void refresh(const QString &portChain);
    // Re-read the currently selected unit (startup, and after edid_set so the
    // UI follows a rename without a restart). Falls back to the stored
    // selection when none was made in this process yet.
    void refreshCurrent();

signals:
    void identityChanged(const QString &portChain, const edid::EdidIdentity &identity);
    // The selected unit changed; consumers should re-render even though no
    // identity is known yet (drop a stale name from the title immediately).
    void currentChanged(const QString &portChain);

private slots:
    void onHidDeviceConnected(const QString &devicePath);
    void startRead();
    void onIdentityRead(bool ok, const edid::EdidIdentity &identity, const QString &error);

private:
    explicit EdidIdentityCache(QObject *parent = nullptr);
    Q_DISABLE_COPY(EdidIdentityCache)

    static constexpr int kInitialDelayMs = 3000;   // let start-up HID traffic finish first
    static constexpr int kRetryDelayMs   = 10000;
    static constexpr int kMaxAttempts    = 2;

    EdidSettingsManager *m_manager = nullptr;
    QTimer m_timer;
    QHash<QString, EdidIdentity> m_byPortChain;
    QString m_currentPortChain;
    QString m_pendingPortChain;
    QString m_pendingDevicePath;   // VideoHid device path when the read started
    bool m_readInFlight = false;   // a manager read we started is running
    int m_attempt = 0;
};

} // namespace edid

#endif // EDIDIDENTITYCACHE_H
