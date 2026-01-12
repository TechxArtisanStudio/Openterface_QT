#ifndef DIAGNOSTICSTYPES_H
#define DIAGNOSTICSTYPES_H

#include <QLoggingCategory>

// Common types used by diagnostics UI and manager
enum class TestStatus {
    NotStarted,
    InProgress,
    Completed,
    Failed
};

// Declare logging category once for use across translation units
Q_DECLARE_LOGGING_CATEGORY(log_device_diagnostics)

#endif // DIAGNOSTICSTYPES_H
