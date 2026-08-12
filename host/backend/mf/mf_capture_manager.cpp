#include "mf_capture_manager.h"
#include "mf_frame_processor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "ole32.lib")
#endif

#include <QDebug>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_multimedia_backend)

// MfCaptureThread implementation

MfCaptureThread::MfCaptureThread(QObject* parent)
    : QThread(parent)
    , sourceReader_(nullptr)
    , frameProcessor_(nullptr)
    , running_(false)
{
}

void MfCaptureThread::setRunning(bool running)
{
    running_ = running;
}

void MfCaptureThread::setSourceReader(IMFSourceReader* reader)
{
    sourceReader_ = reader;
}

void MfCaptureThread::setFrameProcessor(MfFrameProcessor* processor)
{
    frameProcessor_ = processor;
}

void MfCaptureThread::run()
{
#ifndef Q_OS_WIN
    qCWarning(log_multimedia_backend) << "Media Foundation capture thread not available on non-Windows";
    return;
#else
    qCInfo(log_multimedia_backend) << "Media Foundation capture thread started";

    if (!sourceReader_ || !frameProcessor_) {
        qCCritical(log_multimedia_backend) << "Source reader or frame processor not set";
        emit captureError("Source reader or frame processor not set");
        return;
    }

    int consecutiveFailures = 0;
    const int maxConsecutiveFailures = 20;
    int framesProcessed = 0;

    while (running_.load()) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timeStamp = 0;
        IMFSample* sample = nullptr;

        HRESULT hr = sourceReader_->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timeStamp,
            &sample
        );

        if (FAILED(hr)) {
            qCWarning(log_multimedia_backend) << "ReadSample failed:" << Qt::hex << hr;
            consecutiveFailures++;
            if (consecutiveFailures >= maxConsecutiveFailures) {
                emit captureError("Too many consecutive read failures");
                emit deviceDisconnected();
                break;
            }
            msleep(10);
            continue;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            qCInfo(log_multimedia_backend) << "End of stream reached";
            break;
        }

        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            qCDebug(log_multimedia_backend) << "Stream tick received (gap in stream)";
            continue;
        }

        if (!sample) {
            consecutiveFailures++;
            continue;
        }

        consecutiveFailures = 0;

        // Get the first buffer from the sample
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->GetBufferByIndex(0, &buffer);
        if (SUCCEEDED(hr) && buffer) {
            BYTE* data = nullptr;
            DWORD maxLength = 0;
            DWORD currentLength = 0;

            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (SUCCEEDED(hr) && data && currentLength > 0) {
                QImage image = frameProcessor_->processFrame(data, currentLength);
                if (!image.isNull()) {
                    emit frameReady(image);
                    framesProcessed++;
                }

                buffer->Unlock();
            }

            buffer->Release();
        }

        sample->Release();

        if (framesProcessed % 60 == 0 && framesProcessed > 0) {
            qCInfo(log_multimedia_backend) << "Frames processed:" << framesProcessed;
        }
    }

    qCInfo(log_multimedia_backend) << "Media Foundation capture thread finished, processed"
                                   << framesProcessed << "frames";
#endif
}

// MfCaptureManager implementation

MfCaptureManager::MfCaptureManager(QObject* parent)
    : QObject(parent)
    , mediaSource_(nullptr)
    , sourceReader_(nullptr)
    , captureThread_(nullptr)
    , frameProcessor_(nullptr)
    , framerate_(30)
    , initialized_(false)
{
}

MfCaptureManager::~MfCaptureManager()
{
    stopCapture();
    cleanupMediaFoundation();
}

bool MfCaptureManager::initialize(const QString& deviceSymbolicLink, const QSize& resolution, int framerate)
{
#ifndef Q_OS_WIN
    Q_UNUSED(deviceSymbolicLink);
    Q_UNUSED(resolution);
    Q_UNUSED(framerate);
    qCWarning(log_multimedia_backend) << "Media Foundation is only available on Windows";
    return false;
#else
    qCInfo(log_multimedia_backend) << "Initializing Media Foundation capture:"
                                   << deviceSymbolicLink
                                   << resolution << "@" << framerate;

    resolution_ = resolution;
    framerate_ = framerate;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        qCCritical(log_multimedia_backend) << "Failed to initialize COM:" << Qt::hex << hr;
        return false;
    }

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to start Media Foundation:" << Qt::hex << hr;
        CoUninitialize();
        return false;
    }

    // Create attributes for device activation
    IMFAttributes* attributes = nullptr;
    hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to create attributes:" << Qt::hex << hr;
        MFShutdown();
        CoUninitialize();
        return false;
    }

    // Activate the media source from the symbolic link
    IMFActivate* activate = nullptr;
    hr = MFCreateAttributes(&attributes, 2);
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to create activation attributes:" << Qt::hex << hr;
        MFShutdown();
        CoUninitialize();
        return false;
    }

    hr = attributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to set source type:" << Qt::hex << hr;
        attributes->Release();
        MFShutdown();
        CoUninitialize();
        return false;
    }

    hr = attributes->SetString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
        reinterpret_cast<const WCHAR*>(deviceSymbolicLink.utf16())
    );
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to set symbolic link:" << Qt::hex << hr;
        attributes->Release();
        MFShutdown();
        CoUninitialize();
        return false;
    }

    UINT32 count = 0;
    IMFActivate** activates = nullptr;
    hr = MFEnumDeviceSources(attributes, &activates, &count);
    if (FAILED(hr) || count == 0) {
        qCCritical(log_multimedia_backend) << "Failed to find device or no devices:" << Qt::hex << hr;
        attributes->Release();
        MFShutdown();
        CoUninitialize();
        return false;
    }

    // Use the first matching device
    hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mediaSource_));
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to activate media source:" << Qt::hex << hr;
        for (UINT32 i = 0; i < count; i++) activates[i]->Release();
        CoTaskMemFree(activates);
        attributes->Release();
        MFShutdown();
        CoUninitialize();
        return false;
    }

    for (UINT32 i = 0; i < count; i++) activates[i]->Release();
    CoTaskMemFree(activates);
    attributes->Release();

    // Create source reader
    hr = MFCreateSourceReaderFromMediaSource(mediaSource_, nullptr, &sourceReader_);
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to create source reader:" << Qt::hex << hr;
        cleanupMediaFoundation();
        return false;
    }

    // Set the native media type (resolution and format)
    IMFMediaType* mediaType = nullptr;
    hr = MFCreateMediaType(&mediaType);
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to create media type:" << Qt::hex << hr;
        cleanupMediaFoundation();
        return false;
    }

    hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) {
        qCCritical(log_multimedia_backend) << "Failed to set major type:" << Qt::hex << hr;
        mediaType->Release();
        cleanupMediaFoundation();
        return false;
    }

    // Try NV12 first (most widely supported), then RGB24
    GUID subtypes[] = { MFVideoFormat_NV12, MFVideoFormat_RGB24, MFVideoFormat_YUY2 };
    bool typeSet = false;

    for (const auto& subtype : subtypes) {
        hr = mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
        if (FAILED(hr)) continue;

        hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, resolution.width(), resolution.height());
        if (FAILED(hr)) continue;

        hr = MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, framerate, 1);
        if (FAILED(hr)) continue;

        hr = MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(hr)) continue;

        hr = sourceReader_->SetCurrentMediaType(0, nullptr, mediaType);
        if (SUCCEEDED(hr)) {
            typeSet = true;
            qCInfo(log_multimedia_backend) << "Media type set successfully";
            break;
        }
    }

    mediaType->Release();

    if (!typeSet) {
        qCCritical(log_multimedia_backend) << "Failed to set any media type";
        cleanupMediaFoundation();
        return false;
    }

    // Initialize frame processor with NV12 (our preferred format)
    frameProcessor_ = new MfFrameProcessor();
    if (!frameProcessor_->initialize(resolution.width(), resolution.height(), "NV12")) {
        qCCritical(log_multimedia_backend) << "Failed to initialize frame processor";
        cleanupMediaFoundation();
        return false;
    }

    // Create capture thread
    captureThread_ = new MfCaptureThread(this);
    captureThread_->setSourceReader(sourceReader_);
    captureThread_->setFrameProcessor(frameProcessor_);

    initialized_ = true;
    qCInfo(log_multimedia_backend) << "Media Foundation capture initialized successfully";
    return true;
#endif
}

bool MfCaptureManager::startCapture()
{
    if (!initialized_ || !captureThread_) {
        qCCritical(log_multimedia_backend) << "Capture not initialized";
        return false;
    }

    connect(captureThread_, &MfCaptureThread::frameReady,
            this, &MfCaptureManager::frameReady);
    connect(captureThread_, &MfCaptureThread::captureError,
            this, &MfCaptureManager::captureError);
    connect(captureThread_, &MfCaptureThread::deviceDisconnected,
            this, &MfCaptureManager::deviceDisconnected);

    captureThread_->setRunning(true);
    captureThread_->start();

    qCInfo(log_multimedia_backend) << "Media Foundation capture started";
    return true;
}

void MfCaptureManager::stopCapture()
{
    if (captureThread_ && captureThread_->isRunning()) {
        captureThread_->setRunning(false);
        captureThread_->wait(3000);
        if (captureThread_->isRunning()) {
            captureThread_->requestInterruption();
            captureThread_->wait(1000);
        }

        disconnect(captureThread_, nullptr, this, nullptr);
    }

    qCInfo(log_multimedia_backend) << "Media Foundation capture stopped";
}

bool MfCaptureManager::isCapturing() const
{
    return captureThread_ && captureThread_->isRunning();
}

void MfCaptureManager::cleanupMediaFoundation()
{
#ifndef Q_OS_WIN
    return;
#else
    if (sourceReader_) {
        sourceReader_->Release();
        sourceReader_ = nullptr;
    }

    if (mediaSource_) {
        mediaSource_->Shutdown();
        mediaSource_->Release();
        mediaSource_ = nullptr;
    }

    delete frameProcessor_;
    frameProcessor_ = nullptr;

    MFShutdown();
    CoUninitialize();

    initialized_ = false;
#endif
}
