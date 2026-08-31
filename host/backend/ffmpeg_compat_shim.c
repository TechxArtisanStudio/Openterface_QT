/*
 * Compatibility shim for FFmpeg static libs built with newer MinGW-w64.
 * Bridges 64-bit time functions to older MinGW 11.2.0 (Qt 6.5/6.6 default).
 *
 * NOTE: do NOT use __attribute__((weak)) here — on MinGW-w64 < 12 the
 * attribute produces malformed symbol names like `.weak.clock_gettime64.`
 * which the linker cannot match, leaving FFmpeg's undefined refs unresolved.
 * The version guard below is the correct safety net: on MinGW-w64 >= 12 the
 * system headers already provide these symbols, so the shim is skipped.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

#if !defined(__MINGW64_VERSION_MAJOR) || (__MINGW64_VERSION_MAJOR < 12)

int clock_gettime64(int clock_id, struct timespec *ts) {
    return clock_gettime(clock_id, ts);
}

int nanosleep64(const struct timespec *req, struct timespec *rem) {
    return nanosleep(req, rem);
}

extern int pthread_cond_timedwait(void *cond, void *mutex, const struct timespec *abstime);
int pthread_cond_timedwait64(void *cond, void *mutex, const struct timespec *abstime) {
    return pthread_cond_timedwait(cond, mutex, abstime);
}

#endif /* __MINGW64_VERSION_MAJOR < 12 */

#ifdef __cplusplus
}
#endif
