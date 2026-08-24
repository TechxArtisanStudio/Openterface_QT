#ifndef OPFLOGGING_H
#define OPFLOGGING_H

#include <QLoggingCategory>
#include "logcategoryregistry.h"

// OPF_LOGGING_CATEGORY wraps Q_LOGGING_CATEGORY and additionally registers
// the category name in LogCategoryRegistry at static initialization time.
//
// Usage (drop-in replacement for Q_LOGGING_CATEGORY):
//   OPF_LOGGING_CATEGORY(log_serial_tx, "opf.core.serial.tx")
//
// Then use normally:
//   qCDebug(log_serial_tx) << "TX data:" << data.toHex();

#define OPF_LOGGING_CATEGORY(varname, name) \
    Q_LOGGING_CATEGORY(varname, name) \
    static struct OpfCatReg_##varname { \
        OpfCatReg_##varname() { \
            LogCategoryRegistry::instance().registerCategory(QStringLiteral(name)); \
        } \
    } s_opfCatReg_##varname;

#endif // OPFLOGGING_H
