#include "edidsettingsmanager.h"
#include "edidprocessor.h"
#include "firmwareutils.h"
#include "resolutionmodel.h"
#include "../../../video/videohid.h"
#include "../../../video/firmwareoperationmanager.h"
#include "../../../video/ms2109.h"

#include <QDateTime>
#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

Q_LOGGING_CATEGORY(log_edid_manager, "opf.edid.manager")

namespace edid {

EdidSettingsManager::EdidSettingsManager(QObject *parent)
    : QObject(parent)
    , m_fom(new FirmwareOperationManager(&VideoHid::getInstance(), ADDR_EEPROM, this))
{
    static const int metaTypeId = qRegisterMetaType<edid::EdidIdentity>("edid::EdidIdentity");
    Q_UNUSED(metaTypeId);

    connect(m_fom, &FirmwareOperationManager::progress, this, &EdidSettingsManager::progress);
    // readCompleted / writeCompleted fire after the worker thread has finished,
    // so it is safe to start the next EEPROM operation from these handlers.
    connect(m_fom, &FirmwareOperationManager::readCompleted, this, &EdidSettingsManager::onReadCompleted);
    connect(m_fom, &FirmwareOperationManager::writeCompleted, this, &EdidSettingsManager::onWriteCompleted);
}

EdidSettingsManager::~EdidSettingsManager()
{
    if (m_fom) {
        m_fom->cancel();
    }
}

bool EdidSettingsManager::validateField(const QString &text, const QString &fieldName, QString &errorMessage)
{
    if (text.isEmpty()) {
        errorMessage = tr("%1 cannot be empty when enabled.").arg(fieldName);
        return false;
    }
    if (text.length() > kMaxFieldLength) {
        errorMessage = tr("%1 cannot exceed %2 characters.").arg(fieldName).arg(kMaxFieldLength);
        return false;
    }
    for (const QChar &ch : text) {
        if (ch.unicode() < 32 || ch.unicode() > 126) {
            errorMessage = tr("%1 must contain only printable ASCII characters.").arg(fieldName);
            return false;
        }
    }
    errorMessage.clear();
    return true;
}

QString EdidSettingsManager::tempPath(const QString &name) const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QDir::separator() + name;
}

void EdidSettingsManager::restartPolling()
{
    // Same delay the dialogs used before the polling thread is brought back.
    QTimer::singleShot(500, &VideoHid::getInstance(), []() {
        VideoHid::getInstance().start();
    });
}

bool EdidSettingsManager::startRead()
{
    VideoHid &hid = VideoHid::getInstance();
    hid.stopPollingOnly();
    QThread::msleep(100);   // let the polling thread release the bus

    const quint32 firmwareSize = hid.readFirmwareSize();
    if (firmwareSize == 0) {
        qCWarning(log_edid_manager) << "Failed to read firmware size";
        return false;
    }
    // The temp path is a legacy parameter of FirmwareOperationManager; data
    // comes back through the signal.
    m_fom->readFirmware(firmwareSize, tempPath(QStringLiteral("temp_firmware_read.bin")));
    return true;
}

void EdidSettingsManager::readIdentity()
{
    if (isBusy()) {
        emit identityRead(false, EdidIdentity(), tr("An EDID operation is already in progress."));
        return;
    }
    m_state = State::Reading;
    if (!startRead()) {
        finishRead(false, EdidIdentity(), tr("Failed to read firmware size from the device."));
    }
}

void EdidSettingsManager::applySettings(const QString &newName, const QString &newSerial,
                                        const ResolutionModel *resolutions)
{
    if (isBusy()) {
        emit settingsApplied(false, false, EdidIdentity(), EdidIdentity(),
                             tr("An EDID operation is already in progress."));
        return;
    }
    QString err;
    if (!newName.isEmpty() && !validateField(newName, tr("Display name"), err)) {
        emit settingsApplied(false, false, EdidIdentity(), EdidIdentity(), err);
        return;
    }
    if (!newSerial.isEmpty() && !validateField(newSerial, tr("Serial number"), err)) {
        emit settingsApplied(false, false, EdidIdentity(), EdidIdentity(), err);
        return;
    }
    const bool resolutionChanges = resolutions && resolutions->hasChanges();
    if (newName.isEmpty() && newSerial.isEmpty() && !resolutionChanges) {
        emit settingsApplied(false, false, EdidIdentity(), EdidIdentity(), tr("Nothing to update."));
        return;
    }

    m_newName = newName;
    m_newSerial = newSerial;
    m_resolutions = resolutions;
    m_before = EdidIdentity();
    m_modifiedImage.clear();
    m_backupPath.clear();

    m_state = State::ReadingForApply;
    if (!startRead()) {
        finishApply(false, false, tr("Failed to read firmware size from the device."));
    }
}

void EdidSettingsManager::cancel()
{
    if (!isBusy()) {
        return;
    }
    const State was = m_state;
    m_state = State::Idle;
    m_fom->cancel();
    restartPolling();
    if (was == State::Reading) {
        emit identityRead(false, EdidIdentity(), tr("Cancelled."));
    } else {
        emit settingsApplied(false, false, m_before, EdidIdentity(), tr("Cancelled."));
    }
}

void EdidSettingsManager::onReadCompleted(bool success, const QByteArray &image, const QString &error)
{
    switch (m_state) {
    case State::Reading: {
        if (!success) {
            finishRead(false, EdidIdentity(), error.isEmpty() ? tr("Failed to read firmware.") : error);
            return;
        }
        EdidIdentity id = EdidIdentity::fromImage(image);
        finishRead(id.valid, id, id.valid ? QString() : tr("EDID block 0 not found in the firmware image."));
        return;
    }

    case State::ReadingForApply: {
        if (!success) {
            finishApply(false, false, error.isEmpty() ? tr("Failed to read firmware.") : error);
            return;
        }
        m_before = EdidIdentity::fromImage(image);
        if (!m_before.valid) {
            finishApply(false, false, tr("EDID block 0 not found in the firmware image."));
            return;
        }

        // Keep the pre-write image: it is the restore point.
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        m_backupPath = tempPath(QStringLiteral("edid_backup_%1.bin").arg(stamp));
        if (!FirmwareUtils::backupFirmware(image, m_backupPath)) {
            qCWarning(log_edid_manager) << "Could not save pre-write backup to" << m_backupPath;
            m_backupPath.clear();
        }

        ResolutionModel emptyModel;
        EdidProcessor processor(m_resolutions ? *m_resolutions : emptyModel);
        m_modifiedImage = processor.processDisplaySettings(image, m_newName, m_newSerial);
        if (m_modifiedImage.isEmpty()) {
            finishApply(false, false, tr("Failed to process EDID settings."));
            return;
        }
        if (m_modifiedImage == image) {
            // Nothing would change on the device; do not wear the EEPROM.
            finishApply(true, true, QString());
            return;
        }

        m_state = State::Writing;
        qCInfo(log_edid_manager) << "Writing EDID settings: name=" << m_newName << "serial=" << m_newSerial
                                 << "image bytes=" << m_modifiedImage.size();
        m_fom->writeFirmware(m_modifiedImage, tempPath(QStringLiteral("temp_firmware_update.bin")));
        return;
    }

    case State::Verifying: {
        if (!success) {
            // Written, but we could not confirm it.
            finishApply(true, false, error.isEmpty() ? tr("Write completed but read-back failed.") : error);
            return;
        }
        const bool verified = (image == m_modifiedImage);
        if (!verified) {
            qCWarning(log_edid_manager) << "Read-back differs from the written image"
                                        << "(read" << image.size() << "bytes, wrote" << m_modifiedImage.size() << ")";
        }
        m_modifiedImage = image;   // what is actually on the device now
        finishApply(true, verified, verified ? QString() : tr("Write completed but the read-back does not match the written image."));
        return;
    }

    case State::Writing:
    case State::Idle:
        break;
    }
}

void EdidSettingsManager::onWriteCompleted(bool success)
{
    if (m_state != State::Writing) {
        return;
    }
    if (!success) {
        finishApply(false, false, tr("Failed to write firmware to the device."));
        return;
    }
    // Read back and compare; "write returned success" is not evidence.
    m_state = State::Verifying;
    m_fom->readFirmware(static_cast<quint32>(m_modifiedImage.size()),
                        tempPath(QStringLiteral("temp_firmware_verify.bin")));
}

void EdidSettingsManager::finishRead(bool ok, const EdidIdentity &id, const QString &error)
{
    m_state = State::Idle;
    restartPolling();
    emit identityRead(ok, id, error);
}

void EdidSettingsManager::finishApply(bool ok, bool verified, const QString &error)
{
    m_state = State::Idle;
    restartPolling();
    EdidIdentity after = m_modifiedImage.isEmpty() ? EdidIdentity() : EdidIdentity::fromImage(m_modifiedImage);
    emit settingsApplied(ok, verified, m_before, after, error);
}

} // namespace edid
