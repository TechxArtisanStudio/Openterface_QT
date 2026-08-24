/*
 * Test stubs for symbols normally defined in main.cpp.
 * When tests link against openterface_lib (which excludes main.cpp),
 * these globals must be provided here.
 */
#include <QAtomicInteger>

QAtomicInteger<int> g_applicationShuttingDown(0);
