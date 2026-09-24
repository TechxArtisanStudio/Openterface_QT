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

#ifndef EDIDSETTINGSMANAGER_H
#define EDIDSETTINGSMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <atomic>

#include "edididentity.h"

class FirmwareOperationManager;
class ResolutionModel;

namespace edid {

// UI-free orchestration of "read the EEPROM image / edit EDID name, serial
// (and optionally resolutions) / write it back / read it back and verify".
//
// This is the single owner of that sequence: UpdateDisplaySettingsDialog,
// EdidConfigPage and the MCP tools all drive this object and only render
// its signals. Every path that stops HID polling restarts it.
class EdidSettingsManager : public QObject
{
    Q_OBJECT
public:
    static constexpr int kMaxFieldLength = 13;   // EDID descriptor text length

    explicit EdidSettingsManager(QObject *parent = nullptr);
    ~EdidSettingsManager() override;

    // Shared input rule for display name / serial string: non-empty,
    // at most kMaxFieldLength characters, printable ASCII only.
    static bool validateField(const QString &text, const QString &fieldName, QString &errorMessage);

    bool isBusy() const { return m_state != State::Idle; }

    // True while ANY manager in this process has an operation in flight.
    // Two concurrent operations would stop/start HID polling underneath each
    // other, so a second one is refused ("already in progress") instead.
    static bool anyBusy() { return s_busy.load(); }

    // Path of the pre-write image saved by the last applySettings() call
    // (empty if none). This is the restore point.
    QString backupPath() const { return m_backupPath; }

public slots:
    // Read the EEPROM image and emit identityRead(). Stops HID polling for
    // the duration and restarts it afterwards.
    void readIdentity();

    // Read, apply the requested changes (empty string = leave unchanged;
    // null resolutions = no timing changes), write, read back, verify,
    // emit settingsApplied(). Polling is restarted afterwards; the device
    // itself only picks the new EDID up after a power cycle.
    void applySettings(const QString &newName, const QString &newSerial,
                       const ResolutionModel *resolutions = nullptr);

    // Abort an operation in flight (polling is restarted).
    void cancel();

signals:
    void progress(int percent);
    void identityRead(bool ok, const edid::EdidIdentity &identity, const QString &error);
    void settingsApplied(bool ok, bool verified,
                         const edid::EdidIdentity &before, const edid::EdidIdentity &after,
                         const QString &error);

private:
    enum class State { Idle, Reading, ReadingForApply, Writing, Verifying };

    bool startRead();
    void onReadCompleted(bool success, const QByteArray &image, const QString &error);
    void onWriteCompleted(bool success);
    void finishRead(bool ok, const EdidIdentity &id, const QString &error);
    void finishApply(bool ok, bool verified, const QString &error);
    void restartPolling();
    QString tempPath(const QString &name) const;

    void setState(State s);

    static std::atomic<bool> s_busy;
    FirmwareOperationManager *m_fom = nullptr;
    State m_state = State::Idle;

    // apply() context
    QString m_newName;
    QString m_newSerial;
    const ResolutionModel *m_resolutions = nullptr;
    EdidIdentity m_before;
    QByteArray m_modifiedImage;
    QString m_backupPath;
};

} // namespace edid

#endif // EDIDSETTINGSMANAGER_H
