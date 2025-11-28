// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENTERFACE_GSTREAMER_SINKSELECTOR_H
#define OPENTERFACE_GSTREAMER_SINKSELECTOR_H

#include <QString>
#include <QStringList>

namespace Openterface {
namespace GStreamer {

class SinkSelector
{
public:
    // Return a validated video sink name for the current environment.
    // - platform: QGuiApplication::platformName() value (can be empty)
    // The implementation will consult OPENTERFACE_GST_SINK if set, and fall back
    // to probing available GStreamer elements when possible.
    static QString selectSink(const QString &platform = QString());
    // Return an ordered list of candidate sinks to try (respecting the OPENTERFACE_GST_SINK override first)
    static QStringList candidateSinks(const QString &platform = QString());
};

} // namespace GStreamer
} // namespace Openterface

#endif // OPENTERFACE_GSTREAMER_SINKSELECTOR_H
