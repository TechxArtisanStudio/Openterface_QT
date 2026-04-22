#ifndef FIRMWARENETWORKCLIENT_H
#define FIRMWARENETWORKCLIENT_H

#include <QString>
#include <string>
#include <vector>

#include "../videohidchip.h" // VideoChipType

// FirmwareResult is defined in videohid.h; forward-declare here via inclusion.
// The enum is needed by the public API of this class.
enum class FirmwareResult;

/**
 * @brief Handles all network I/O for firmware version checking and binary download.
 *
 * Extracted from VideoHid as part of Phase 4 refactoring. This is a plain (non-QObject)
 * synchronous utility class; it uses QEventLoop internally for network waits, matching
 * the original VideoHid behaviour.
 *
 * State held:
 *   - networkFirmware  — raw binary downloaded by fetchBinFile()
 *   - latestVersion    — version string parsed from the downloaded binary header
 *   - lastResult       — FirmwareResult from the most recent check() call
 *
 * VideoHid::isLatestFirmware() delegates to check(); VideoHid::loadFirmwareToEeprom()
 * reads firmware data via getNetworkFirmware().
 */
class FirmwareNetworkClient {
public:
    static constexpr const char* DEFAULT_INDEX_URL =
        "https://assets.openterface.com/openterface/firmware/minikvm_latest_firmware2.txt";

    // ─────────────────────────────────────────────────────────────────────
    //  Main API
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Fetch the firmware index, download the matching binary, compare versions.
     *
     * @param currentVersion  Version string reported by the connected device.
     * @param chip            Chip type for selecting the correct firmware file.
     * @param indexUrl        URL of the firmware index text file.
     * @param timeoutMs       Per-request timeout in milliseconds.
     * @return FirmwareResult indicating Latest / Upgradable / Timeout / CheckFailed.
     */
    FirmwareResult check(const std::string& currentVersion,
                         VideoChipType chip,
                         const QString& indexUrl = QString::fromLatin1(DEFAULT_INDEX_URL),
                         int timeoutMs = 5000);

    // ─────────────────────────────────────────────────────────────────────
    //  Pure utility — no state required
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Parse a firmware index file and return the best filename for the given chip.
     *
     * Supports:
     *  - Legacy single-line: "filename.bin"
     *  - CSV multi-line: "<version>,<filename>,<chipToken>"
     *
     * @param indexContent  Full text content of the index file.
     * @param chip          Target chip type.
     * @return Filename string, or empty if no suitable entry found.
     */
    static QString pickFirmwareFileNameFromIndex(const QString& indexContent,
                                                  VideoChipType chip = VideoChipType::UNKNOWN);

    // ─────────────────────────────────────────────────────────────────────
    //  State accessors
    // ─────────────────────────────────────────────────────────────────────

    /// Version string parsed from the last downloaded firmware binary (bytes 12-15 of header).
    std::string getLatestVersion() const { return m_latestVersion; }

    /// Raw binary downloaded by the last successful call to check().
    const std::vector<unsigned char>& getNetworkFirmware() const { return m_networkFirmware; }

    /// FirmwareResult from the most recent check() call.
    FirmwareResult lastResult() const { return m_lastResult; }

private:
    // ─────────────────────────────────────────────────────────────────────
    //  Internal helpers
    // ─────────────────────────────────────────────────────────────────────

    /// Fetch the firmware index and return the selected filename for @p chip.
    /// Stores FirmwareResult::Timeout / CheckSuccess / CheckFailed in m_lastResult.
    QString fetchFilename(VideoChipType chip, const QString& indexUrl, int timeoutMs);

    /// Download the binary at @p url, populate m_networkFirmware and m_latestVersion.
    /// Stores FirmwareResult::Timeout / CheckFailed in m_lastResult on error.
    void fetchBinFile(const QString& url, int timeoutMs);

    // ─────────────────────────────────────────────────────────────────────
    //  State
    // ─────────────────────────────────────────────────────────────────────
    std::string               m_latestVersion;
    std::vector<unsigned char> m_networkFirmware;
    FirmwareResult            m_lastResult{};
};

#endif // FIRMWARENETWORKCLIENT_H
