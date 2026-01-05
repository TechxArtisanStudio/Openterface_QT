// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Openterface {
namespace GStreamer {
namespace GstHelpers {

/**
 * Helper to set an element state and wait until it reaches the target state (or fails).
 * Returns true on success. On failure, returns false and may fill outError with a short description.
 * The call is a thin wrapper around gst_element_set_state + gst_element_get_state.
 */
bool setPipelineStateWithTimeout(void* element, int targetState, int timeoutMs, QString* outError = nullptr);

/**
 * Helper to pop an error message from the bus and log its details in a consistent way.
 * If there is no bus or no error message it will log a helpful message and return.
 */
void parseAndLogGstErrorMessage(void* bus, const char* context = nullptr);

} // namespace GstHelpers
} // namespace GStreamer
} // namespace Openterface
