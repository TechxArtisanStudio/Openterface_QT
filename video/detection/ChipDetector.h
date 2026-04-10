#ifndef CHIPDETECTOR_H
#define CHIPDETECTOR_H

#include <memory>
#include <QString>

// VideoChipType enum and VideoChip base class are defined here
#include "../videohidchip.h"

class IHIDTransport;
class VideoHid;

/**
 * @brief Stateless utility for HID device chip identification and instantiation.
 *
 * Encapsulates all platform-specific (Windows / Linux) VID/PID parsing so that
 * VideoHid::detectChipType() is a simple 3-line call.
 *
 * Phase 2: Ms2130sChip no longer needs a VideoHid* owner; all I/O goes through IHIDTransport.
 */
class ChipDetector {
public:
    /**
     * Parse a HID device path to determine the connected chip type.
     *
     * Windows: looks for VID/PID tokens in the path string.
     * Linux:   walks the sysfs tree from the hidraw device node, falls back to
     *          path string matching when sysfs is unavailable.
     *
     * @param devicePath  Platform device path (Windows GUID path or Linux /dev/hidrawX)
     * @param portChain   Optional USB port chain string (reserved for future use)
     * @return Detected VideoChipType, or VideoChipType::UNKNOWN if unrecognised
     */
    static VideoChipType detect(const QString& devicePath, const QString& portChain = {});

    /**
     * Instantiate the correct VideoChip subclass for the given type.
     *
     * @param type       Chip type returned by detect()
     * @param transport  HID transport implementation (VideoHid implements IHIDTransport)
     * @return Heap-allocated chip instance, or nullptr for VideoChipType::UNKNOWN
     */
    static std::unique_ptr<VideoChip> createChip(VideoChipType type,
                                                  IHIDTransport* transport);
};

#endif // CHIPDETECTOR_H
