// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENTERFACE_GSTREAMER_QUEUECONFIGURATOR_H
#define OPENTERFACE_GSTREAMER_QUEUECONFIGURATOR_H

#include <QString>

namespace Openterface {
namespace GStreamer {

class QueueConfigurator
{
public:
    // Configure display queue (aggressive buffering, low latency)
    static void configureDisplayQueue(void* pipeline);

    // Configure recording queue (larger buffers, less strict latency)
    static void configureRecordingQueue(void* pipeline);

    // Convenience to configure both queues when present
    static void configureQueues(void* pipeline);
};

} // namespace GStreamer
} // namespace Openterface

#endif // OPENTERFACE_GSTREAMER_QUEUECONFIGURATOR_H
