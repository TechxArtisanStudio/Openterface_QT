// SPDX-License-Identifier: GPL-3.0-or-later
#include "gstreamerhelpers.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_gstreamer_gstreamerhelpers, "opf.backend.gstreamerhelpers")

using namespace Openterface::GStreamer::GstHelpers;

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

bool Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(void* elementPtr, int targetState, int timeoutMs, QString* outError)
{
    if (!elementPtr) {
        if (outError) *outError = QStringLiteral("Element pointer is null");
        qCWarning(log_gstreamer_gstreamerhelpers) << "setPipelineStateWithTimeout: element pointer is null";
        return false;
    }

    GstElement *element = static_cast<GstElement*>(elementPtr);

    GstStateChangeReturn ret = gst_element_set_state(element, static_cast<GstState>(targetState));
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCCritical(log_gstreamer_gstreamerhelpers) << "Failed to set element state to" << targetState;
        // Try to pull any error from the bus for diagnostics
        // Caller may pass a bus to parseAndLogGstErrorMessage, but we don't have it here.
        if (outError) *outError = QStringLiteral("Failed to set state (GST_STATE_CHANGE_FAILURE)");
        return false;
    }

    GstState state, pending;
    ret = gst_element_get_state(element, &state, &pending, static_cast<GstClockTime>(timeoutMs) * GST_MSECOND);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        if (outError) *outError = QStringLiteral("State change failure");
        qCCritical(log_gstreamer_gstreamerhelpers) << "State change failure waiting for target state";
        return false;
    }

    if (state != static_cast<GstState>(targetState)) {
        if (outError) *outError = QStringLiteral("Element did not reach target state in timeout");
        qCCritical(log_gstreamer_gstreamerhelpers) << "Element failed to reach state" << targetState << "(current:" << state << ", pending:" << pending << ")";
        return false;
    }

    return true;
}

void Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(void* busPtr, const char* context)
{
    if (!busPtr) {
        qCWarning(log_gstreamer_gstreamerhelpers) << "Bus not available for error details" << (context ? context : "");
        return;
    }

    GstBus* bus = static_cast<GstBus*>(busPtr);
    GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (!msg) {
        qCDebug(log_gstreamer_gstreamerhelpers) << "No error message available on bus" << (context ? context : "");
        return;
    }

    GError* error = nullptr;
    gchar* debug_info = nullptr;
    gst_message_parse_error(msg, &error, &debug_info);

    qCCritical(log_gstreamer_gstreamerhelpers) << "GStreamer Error:" << (error ? error->message : "Unknown") << (context ? context : "");
    qCCritical(log_gstreamer_gstreamerhelpers) << "Debug info:" << (debug_info ? debug_info : "None");

    if (error) g_error_free(error);
    if (debug_info) g_free(debug_info);
    gst_message_unref(msg);
}

#else // HAVE_GSTREAMER

bool Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(void* /*elementPtr*/, int /*targetState*/, int /*timeoutMs*/, QString* outError)
{
    if (outError) *outError = QStringLiteral("GStreamer not available in this build");
    qCWarning(log_gstreamer_gstreamerhelpers) << "setPipelineStateWithTimeout called but GStreamer is not compiled in";
    return false;
}

void Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(void* /*busPtr*/, const char* context)
{
    qCDebug(log_gstreamer_gstreamerhelpers) << "GStreamer not compiled in - no bus to parse" << (context ? context : "");
}

#endif // HAVE_GSTREAMER
