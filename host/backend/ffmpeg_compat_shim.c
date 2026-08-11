/*
 * Compatibility shim for FFmpeg static libs built with newer MinGW-w64.
 * Bridges 64-bit time functions and stack protector symbols to older MinGW 11.2.0.
 *
 * All symbols are declared __attribute__((weak)) so that if the runtime
 * already provides them (e.g. MSYS2 mingw64 libwinpthread), the linker
 * will prefer the strong definitions from the system library.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 64-bit time function wrappers - map to 32-bit versions (fine for Windows) */
#include <time.h>

#ifdef __MINGW32__
#define SHIM_ATTR __attribute__((weak))
#else
#define SHIM_ATTR
#endif

/*
 * Newer MinGW-w64 (>= 12.x) already declares clock_gettime64 / nanosleep64 /
 * pthread_cond_timedwait64 in <pthread_time.h> and <pthread.h> with
 * 'struct _timespec64*' parameters. Defining them again with 'struct timespec*'
 * causes a conflicting-types error.
 * Skip our shim when the system provides these functions.
 */
#if !defined(__MINGW64_VERSION_MAJOR) || (__MINGW64_VERSION_MAJOR < 12)

SHIM_ATTR int clock_gettime64(int clock_id, struct timespec *ts) {
    return clock_gettime(clock_id, ts);
}

SHIM_ATTR int nanosleep64(const struct timespec *req, struct timespec *rem) {
    return nanosleep(req, rem);
}

/* pthread cond timedwait - delegate to regular version */
SHIM_ATTR int pthread_cond_timedwait64(void *cond, void *mutex, const struct timespec *abstime) {
    /* Forward to the regular pthread_cond_timedwait from libwinpthread */
    typedef int (*pthread_cond_timedwait_fn)(void*, void*, const struct timespec*);
    /* libwinpthread exports pthread_cond_timedwait - just call it */
    extern int pthread_cond_timedwait(void *cond, void *mutex, const struct timespec *abstime);
    return pthread_cond_timedwait(cond, mutex, abstime);
}

#endif /* __MINGW64_VERSION_MAJOR < 12 */

#ifdef __cplusplus
}
#endif
