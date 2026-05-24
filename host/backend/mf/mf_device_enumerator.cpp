#include "mf_device_enumerator.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif

#include <QDebug>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_multimedia_backend)

MfDeviceEnumerator::MfDeviceEnumerator()
{
}

MfDeviceEnumerator::~MfDeviceEnumerator()
{
}

QList<MfDeviceInfo> MfDeviceEnumerator::enumerateVideoDevices()
{
    deviceList_.clear();

#ifndef Q_OS_WIN
    qCWarning(log_multimedia_backend) << "Media Foundation is only available on Windows";
    return deviceList_;
#else
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        qCWarning(log_multimedia_backend) << "Failed to initialize COM for Media Foundation enumeration:" << Qt::hex << hr;
        return deviceList_;
    }

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        qCWarning(log_multimedia_backend) << "Failed to start Media Foundation:" << Qt::hex << hr;
        CoUninitialize();
        return deviceList_;
    }

    IMFAttributes* attributes = nullptr;
    hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        qCWarning(log_multimedia_backend) << "Failed to create MF attributes:" << Qt::hex << hr;
        MFShutdown();
        CoUninitialize();
        return deviceList_;
    }

    hr = attributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr)) {
        qCWarning(log_multimedia_backend) << "Failed to set video capture source type:" << Qt::hex << hr;
        attributes->Release();
        MFShutdown();
        CoUninitialize();
        return deviceList_;
    }

    IMFActivate** activateObjects = nullptr;
    UINT32 deviceCount = 0;

    hr = MFEnumDeviceSources(attributes, &activateObjects, &deviceCount);
    if (FAILED(hr)) {
        qCWarning(log_multimedia_backend) << "Failed to enumerate Media Foundation devices:" << Qt::hex << hr;
        attributes->Release();
        MFShutdown();
        CoUninitialize();
        return deviceList_;
    }

    qCInfo(log_multimedia_backend) << "Found" << deviceCount << "Media Foundation video capture devices";

    for (UINT32 i = 0; i < deviceCount; i++) {
        if (!activateObjects[i]) continue;

        MfDeviceInfo info;
        info.index = i;

        // Get friendly name
        wchar_t* namePtr = nullptr;
        UINT32 nameLength = 0;
        hr = activateObjects[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &namePtr,
            &nameLength
        );
        if (SUCCEEDED(hr) && namePtr) {
            info.friendlyName = QString::fromWCharArray(namePtr);
            CoTaskMemFree(namePtr);
        } else {
            info.friendlyName = QString("Device %1").arg(i);
        }

        // Get symbolic link (used to open the device)
        wchar_t* linkPtr = nullptr;
        UINT32 linkLength = 0;
        hr = activateObjects[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &linkPtr,
            &linkLength
        );
        if (SUCCEEDED(hr) && linkPtr) {
            info.symbolicLink = QString::fromWCharArray(linkPtr);
            CoTaskMemFree(linkPtr);
        }

        qCInfo(log_multimedia_backend) << "  [" << i << "]" << info.friendlyName
                                       << "->" << info.symbolicLink;

        deviceList_.append(info);
    }

    // Free activate objects
    for (UINT32 i = 0; i < deviceCount; i++) {
        if (activateObjects[i]) {
            activateObjects[i]->Release();
        }
    }
    CoTaskMemFree(activateObjects);

    attributes->Release();
    MFShutdown();
    CoUninitialize();

    return deviceList_;
#endif
}

QString MfDeviceEnumerator::getDeviceSymbolicLink(int index) const
{
    if (index >= 0 && index < deviceList_.size()) {
        return deviceList_[index].symbolicLink;
    }
    return QString();
}

int MfDeviceEnumerator::getDeviceCount() const
{
    return deviceList_.size();
}
