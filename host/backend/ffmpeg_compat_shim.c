/*
 * Compatibility shim for FFmpeg static libs built with newer MinGW-w64.
 * Bridges 64-bit time functions and stack protector symbols to older MinGW 11.2.0.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 64-bit time function wrappers - map to 32-bit versions (fine for Windows) */
#include <time.h>

int clock_gettime64(int clock_id, struct timespec *ts) {
    return clock_gettime(clock_id, ts);
}

int nanosleep64(const struct timespec *req, struct timespec *rem) {
    return nanosleep(req, rem);
}

/* pthread cond timedwait - delegate to regular version */
int pthread_cond_timedwait64(void *cond, void *mutex, const struct timespec *abstime) {
    /* Forward to the regular pthread_cond_timedwait from libwinpthread */
    typedef int (*pthread_cond_timedwait_fn)(void*, void*, const struct timespec*);
    /* libwinpthread exports pthread_cond_timedwait - just call it */
    extern int pthread_cond_timedwait(void *cond, void *mutex, const struct timespec *abstime);
    return pthread_cond_timedwait(cond, mutex, abstime);
}

#ifdef __cplusplus
}
#endif
