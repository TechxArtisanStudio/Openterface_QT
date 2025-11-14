/*
 * Qt Version Wrapper - Intercepts dlopen to prevent loading incompatible Qt6 versions
 * 
 * This preload library prevents Fedora's system Qt6.9 from loading when Qt6.6.3 is bundled.
 * It works by intercepting dlopen() calls and redirecting Qt6 library loads to our bundled versions.
 * 
 * Compile:
 *   gcc -shared -fPIC -o qt_version_wrapper.so qt_version_wrapper.c -ldl
 * 
 * Use with:
 *   LD_PRELOAD=/path/to/qt_version_wrapper.so:/usr/lib/openterfaceqt/qt6/libQt6Core.so.6.6.3:... ./app
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

/* Track recursion to prevent infinite loops */
static __thread int in_wrapper = 0;

/* Configuration: Where our bundled Qt libraries are installed */
#define BUNDLED_QT_PATH "/usr/lib/openterfaceqt/qt6"

/* System Qt library paths we want to block */
static const char *system_qt_paths[] = {
    "/lib64/libQt6",
    "/lib/libQt6",
    "/usr/lib/libQt6",
    "/usr/lib64/libQt6",
    "/lib/x86_64-linux-gnu/libQt6",
    "/usr/lib/x86_64-linux-gnu/libQt6",
    NULL
};

/* List of Qt6 library names we need to guard */
static const char *qt6_libraries[] = {
    "libQt6Core",
    "libQt6Gui", 
    "libQt6Widgets",
    "libQt6Qml",
    "libQt6Quick",
    "libQt6Multimedia",
    "libQt6MultimediaWidgets",
    "libQt6SerialPort",
    "libQt6Network",
    "libQt6OpenGL",
    "libQt6Xml",
    "libQt6Concurrent",
    "libQt6DBus",
    "libQt6Svg",
    "libQt6QuickWidgets",
    "libQt6PrintSupport",
    NULL
};

typedef void* (*dlopen_t)(const char*, int);
static dlopen_t real_dlopen = NULL;

static void* get_real_dlopen(void) {
    if (!real_dlopen) {
        real_dlopen = (dlopen_t) dlsym(RTLD_NEXT, "dlopen");
        if (!real_dlopen) {
            fprintf(stderr, "qt_version_wrapper: Failed to get real dlopen\n");
            return NULL;
        }
    }
    return real_dlopen;
}

static int is_qt6_library(const char *filename) {
    if (!filename) return 0;
    
    for (int i = 0; qt6_libraries[i]; i++) {
        if (strstr(filename, qt6_libraries[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_system_qt_path(const char *filename) {
    if (!filename) return 0;
    
    for (int i = 0; system_qt_paths[i]; i++) {
        if (strstr(filename, system_qt_paths[i])) {
            return 1;
        }
    }
    return 0;
}

static char* resolve_bundled_path(const char *filename) {
    /* Extract library name from path */
    const char *libname = strrchr(filename, '/');
    if (!libname) libname = filename;
    else libname++;
    
    /* Build path to bundled library */
    static char bundled_path[PATH_MAX];
    snprintf(bundled_path, sizeof(bundled_path), "%s/%s", BUNDLED_QT_PATH, libname);
    
    /* Check if bundled version exists */
    if (access(bundled_path, F_OK) == 0) {
        fprintf(stderr, 
                "qt_version_wrapper: Redirected %s -> %s\n",
                filename, bundled_path);
        return bundled_path;
    }
    
    return NULL;
}

/*
 * Main dlopen wrapper - intercepts all dlopen calls
 */
void* dlopen(const char *filename, int flags) {
    void *result;
    dlopen_t real_dlopen_fn = (dlopen_t) get_real_dlopen();
    
    if (!real_dlopen_fn) {
        return NULL;
    }
    
    /* Prevent infinite recursion */
    if (in_wrapper) {
        return (*real_dlopen_fn)(filename, flags);
    }
    
    in_wrapper = 1;
    
    /* Check if this is a system Qt6 library that we should block */
    if (filename && is_qt6_library(filename) && is_system_qt_path(filename)) {
        char *bundled = resolve_bundled_path(filename);
        if (bundled) {
            result = (*real_dlopen_fn)(bundled, flags);
        } else {
            /* Library not found in bundled path - allow system to handle */
            fprintf(stderr, 
                    "qt_version_wrapper: WARNING - System Qt6 path detected but bundled version not found: %s\n",
                    filename);
            result = (*real_dlopen_fn)(filename, flags);
        }
    } else {
        /* Not a Qt6 library or not from system path - allow normally */
        result = (*real_dlopen_fn)(filename, flags);
    }
    
    in_wrapper = 0;
    return result;
}

/*
 * Constructor: Called when library is loaded
 */
__attribute__((constructor))
static void qt_version_wrapper_init(void) {
    const char *bundled_path = BUNDLED_QT_PATH;
    
    /* Verify bundled Qt path exists */
    if (access(bundled_path, F_OK) != 0) {
        fprintf(stderr, "qt_version_wrapper: WARNING - Bundled Qt path not found: %s\n", 
                bundled_path);
        return;
    }
    
    fprintf(stderr, "qt_version_wrapper: Initialized for %s\n", bundled_path);
}
