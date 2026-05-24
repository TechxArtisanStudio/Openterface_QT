#include "ffmpeg_amd_detector.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <devguid.h>

// MinGW may not define SPDRP_DEVICE_DESCRIPTION in all SDK versions
#ifndef SPDRP_DEVICE_DESCRIPTION
#define SPDRP_DEVICE_DESCRIPTION 0x0000000CUL
#endif

#pragma comment(lib, "setupapi.lib")
#endif

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

bool FFmpegAmdDetector::isAmdIntegratedGpuDetected()
{
#ifndef Q_OS_WIN
    return false;
#else
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_ffmpeg_backend) << "Failed to enumerate display adapters";
        return false;
    }

    bool foundAmdIgpu = false;
    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfo, i, &deviceInfoData); i++) {
        wchar_t descriptionBuffer[256] = {0};
        DWORD requiredSize = 0;

        BOOL result = SetupDiGetDeviceRegistryPropertyW(
            deviceInfo, &deviceInfoData,
            SPDRP_DEVICE_DESCRIPTION, nullptr,
            reinterpret_cast<PBYTE>(descriptionBuffer),
            sizeof(descriptionBuffer), &requiredSize
        );

        if (!result) {
            continue;
        }

        QString description = QString::fromWCharArray(descriptionBuffer);
        QString lowerDesc = description.toLower();

        bool isAmd = lowerDesc.contains("amd") || lowerDesc.contains("radeon");
        bool isNvidia = lowerDesc.contains("nvidia") || lowerDesc.contains("geforce");
        bool isIntel = lowerDesc.contains("intel") || lowerDesc.contains("uhd") || lowerDesc.contains("iris");

        if (isAmd) {
            // Check if it's an integrated GPU by looking for iGPU keywords
            bool isIntegrated = lowerDesc.contains("integrated") ||
                               lowerDesc.contains("apu") ||
                               lowerDesc.contains("vega") ||
                               lowerDesc.contains("gfx") ||
                               lowerDesc.contains("renoir") ||
                               lowerDesc.contains("cezanne") ||
                               lowerDesc.contains("rembrandt") ||
                               lowerDesc.contains("mendocino") ||
                               lowerDesc.contains("phoenix") ||
                               lowerDesc.contains("strix");

            // Also check adapter RAM — iGPUs typically have < 1GB dedicated VRAM
            bool isLikelyIgpu = false;
            wchar_t ramBuffer[256] = {0};
            if (SetupDiGetDeviceRegistryPropertyW(
                    deviceInfo, &deviceInfoData,
                    SPDRP_HARDWAREID, nullptr,
                    reinterpret_cast<PBYTE>(ramBuffer),
                    sizeof(ramBuffer), &requiredSize
                )) {
                // Hardware ID check: PCI\VEN_1002 = AMD
                QString hwId = QString::fromWCharArray(ramBuffer);
                if (hwId.contains("VEN_1002", Qt::CaseInsensitive) && isIntegrated) {
                    isLikelyIgpu = true;
                }
            }

            if (isIntegrated || isLikelyIgpu) {
                qCInfo(log_ffmpeg_backend) << "AMD iGPU detected:" << description;
                foundAmdIgpu = true;
                break;
            }
        }

        Q_UNUSED(isNvidia);
        Q_UNUSED(isIntel);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);

    if (!foundAmdIgpu) {
        qCDebug(log_ffmpeg_backend) << "No AMD integrated GPU detected";
    }

    return foundAmdIgpu;
#endif
}

QString FFmpegAmdDetector::getAmdGpuInfo()
{
#ifndef Q_OS_WIN
    return QString();
#else
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }

    QStringList amdGpus;
    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfo, i, &deviceInfoData); i++) {
        wchar_t descriptionBuffer[256] = {0};
        DWORD requiredSize = 0;

        if (SetupDiGetDeviceRegistryPropertyW(
                deviceInfo, &deviceInfoData,
                SPDRP_DEVICE_DESCRIPTION, nullptr,
                reinterpret_cast<PBYTE>(descriptionBuffer),
                sizeof(descriptionBuffer), &requiredSize
            )) {
            QString description = QString::fromWCharArray(descriptionBuffer);
            if (description.toLower().contains("amd") || description.toLower().contains("radeon")) {
                amdGpus.append(description);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return amdGpus.join(", ");
#endif
}
