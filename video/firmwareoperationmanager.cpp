#include "firmwareoperationmanager.h"
#include "firmwarereader.h"
#include "firmwarewriter.h"
#include "videohid.h"
#include <QDebug>

FirmwareOperationManager::FirmwareOperationManager(VideoHid* videoHid, quint16 address, QObject* parent)
    : QObject(parent), m_videoHid(videoHid), m_address(address), m_thread(nullptr), m_reader(nullptr), m_writer(nullptr), m_writeSuccess(false), m_readSuccess(false), m_readError(), m_isWriteOperation(false)
{
}

FirmwareOperationManager::~FirmwareOperationManager()
{
    cancel();
}

void FirmwareOperationManager::readFirmware(quint32 firmwareSize, const QString& tempFirmwarePath)
{
    if (m_thread && m_thread->isRunning()) {
        if (!m_thread->wait(5000)) {
            qWarning() << "Existing firmware thread did not finish before starting read";
            emit readFinished(false, QByteArray(), tr("Firmware operation timeout"));
            return;
        }
    }
    cleanupWorker();

    if (!m_videoHid || firmwareSize == 0) {
        emit readFinished(false, QByteArray(), tr("Invalid firmware read parameters"));
        return;
    }

    m_tempFirmwarePath = tempFirmwarePath;
    m_isWriteOperation = false;
    m_thread = new QThread(this);
    m_reader = new FirmwareReader(m_videoHid, m_address, firmwareSize, tempFirmwarePath);
    m_reader->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_reader, &FirmwareReader::process);
    connect(m_reader, &FirmwareReader::progress, this, &FirmwareOperationManager::onReaderProgress);
    connect(m_reader, &FirmwareReader::finished, this, &FirmwareOperationManager::onReaderFinished);
    connect(m_reader, &FirmwareReader::error, this, &FirmwareOperationManager::onReaderError);
    connect(m_reader, &FirmwareReader::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, this, &FirmwareOperationManager::onThreadFinished);

    m_thread->start();
}

void FirmwareOperationManager::writeFirmware(const QByteArray& firmwareData, const QString& tempFirmwarePath)
{
    if (m_thread && m_thread->isRunning()) {
        if (!m_thread->wait(5000)) {
            qWarning() << "Existing firmware thread did not finish before starting write";
            emit writeFinished(false, tr("Firmware operation timeout"));
            return;
        }
    }
    cleanupWorker();

    if (!m_videoHid || firmwareData.isEmpty()) {
        emit writeFinished(false, tr("Invalid firmware write parameters"));
        return;
    }

    m_tempFirmwarePath = tempFirmwarePath;
    m_isWriteOperation = true;
    m_thread = new QThread(this);
    m_writer = new FirmwareWriter(m_videoHid, m_address, firmwareData);
    m_writer->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_writer, &FirmwareWriter::process);
    connect(m_writer, &FirmwareWriter::progress, this, &FirmwareOperationManager::onWriterProgress);
    connect(m_writer, &FirmwareWriter::finished, this, &FirmwareOperationManager::onWriterFinished);
    connect(m_writer, &FirmwareWriter::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, this, &FirmwareOperationManager::onThreadFinished);

    m_thread->start();
}

void FirmwareOperationManager::cancel()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait(2000);
    }
    cleanupWorker();
}

void FirmwareOperationManager::cleanupWorker()
{
    if (m_reader) {
        m_reader->deleteLater();
        m_reader = nullptr;
    }
    if (m_writer) {
        m_writer->deleteLater();
        m_writer = nullptr;
    }
    if (m_thread) {
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

void FirmwareOperationManager::onReaderProgress(int percent)
{
    emit progress(percent);
}

void FirmwareOperationManager::onReaderFinished(bool success, const QByteArray &firmwareData)
{
    m_readSuccess = success;
    m_readFirmwareData = firmwareData;
    m_readError = success ? QString() : tr("Failed to read firmware");
    emit readFinished(success, firmwareData, m_readError);
}

void FirmwareOperationManager::onReaderError(const QString& message)
{
    m_readSuccess = false;
    m_readFirmwareData.clear();
    m_readError = message;
    emit progress(0);
    emit readFinished(false, QByteArray(), message);
}

void FirmwareOperationManager::onWriterProgress(int percent)
{
    emit progress(40 + (percent * 60 / 100));
}

void FirmwareOperationManager::onWriterFinished(bool success)
{
    m_writeSuccess = success;
    emit writeFinished(success, success ? QString() : tr("Failed to write firmware"));
}

void FirmwareOperationManager::onThreadFinished()
{
    bool writeComplete = m_isWriteOperation;
    bool readComplete = !m_isWriteOperation;
    bool writeSuccess = m_writeSuccess;
    bool readSuccess = m_readSuccess;
    QString readError = m_readError;

    cleanupWorker();

    if (writeComplete) {
        emit writeCompleted(writeSuccess);
    }
    if (readComplete) {
        emit readCompleted(readSuccess, m_readFirmwareData, readError);
    }
}
