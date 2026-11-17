/*
 * Compatibility shim for FFmpeg static libs built with newer MinGW-w64.
 * Bridges 64-bit time functions to older MinGW 11.2.0 (Qt 6.5/6.6 default).
 *
 * The shim functions are declared .weak so that if the linked libwinpthread
 * already provides them (e.g. MSYS2's MinGW-w64 >= 12 libwinpthread on a
 * system where the compiler headers are still < 12), the library symbols win
 * and no "multiple definition" linker error occurs. When the library does
 * NOT provide them, the shim definitions are used as fallback.
 *
 * NOTE: do NOT replace .weak with __attribute__((weak)) — on MinGW-w64 < 12
 * the attribute produces malformed symbol names like `.weak.clock_gettime64.`
 * which the linker cannot match, leaving FFmpeg's undefined refs unresolved.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

#if !defined(__MINGW64_VERSION_MAJOR) || (__MINGW64_VERSION_MAJOR < 12)

__asm__(".weak clock_gettime64");
int clock_gettime64(int clock_id, struct timespec *ts) {
    return clock_gettime(clock_id, ts);
}

__asm__(".weak nanosleep64");
int nanosleep64(const struct timespec *req, struct timespec *rem) {
    return nanosleep(req, rem);
}

__asm__(".weak pthread_cond_timedwait64");
extern int pthread_cond_timedwait(void *cond, void *mutex, const struct timespec *abstime);
int pthread_cond_timedwait64(void *cond, void *mutex, const struct timespec *abstime) {
    return pthread_cond_timedwait(cond, mutex, abstime);
}

#endif /* __MINGW64_VERSION_MAJOR < 12 */

#ifdef __cplusplus
}
#endif
