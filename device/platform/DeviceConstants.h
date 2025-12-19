#ifndef DEVICECONSTANTS_H
#define DEVICECONSTANTS_H

#include <QString>

/**
 * @brief Device VID/PID constants for Openterface devices
 * 
 * This header contains all the vendor and product ID constants used
 * for device discovery across different generations of Openterface devices.
 */

// Generation 1 - Original devices
static const QString OPENTERFACE_VID = "534D";        // MS2109 integrated device
static const QString OPENTERFACE_PID = "2109";
static const QString SERIAL_VID = "1A86";             // Serial port device
static const QString SERIAL_PID = "7523";

// Generation 2 - USB 2.0 compatibility  
static const QString SERIAL_VID_V2 = "1A86";          // New generation serial
static const QString SERIAL_PID_V2 = "FE0C";

// Generation 3 - USB 3.0 integrated devices
static const QString OPENTERFACE_VID_V2 = "345F";     // USB 3.0 integrated
static const QString OPENTERFACE_PID_V2 = "2132";

static const QString OPENTERFACE_VID_V3 = "345F";     // V3 USB 3.0 integrated
static const QString OPENTERFACE_PID_V3 = "2109";

#endif // DEVICECONSTANTS_H