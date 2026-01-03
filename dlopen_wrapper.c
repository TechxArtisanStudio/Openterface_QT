#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// Declare the real dlopen function that the linker will provide
extern void* __real_dlopen(const char* filename, int flag);

// Keep track of recursive calls to prevent infinite loops
static int in_dlopen_wrapper = 0;

// Wrapper function for dlopen to satisfy Qt6's symbol requirements
void* __wrap_dlopen(const char* filename, int flag) {
    // Prevent infinite recursion
    if (in_dlopen_wrapper) {
        return NULL;
    }
    
    in_dlopen_wrapper = 1;
    void* result = NULL;
    
    // For a static build, we generally don't want to load dynamic libraries
    // However, we allow VAAPI libraries for hardware acceleration
    
    if (filename) {
        // Allow VAAPI libraries for hardware acceleration
        if (strstr(filename, "libva") || 
            strstr(filename, "va.so") || 
            strstr(filename, "va-drm") || 
            strstr(filename, "va-x11") ||
            strstr(filename, "vaapi")) {
            printf("Static build: Allowing VAAPI library: dlopen(\"%s\")\n", filename);
            result = __real_dlopen(filename, flag);
        }
        // Allow other essential graphics/video libraries
        else if (strstr(filename, "libdrm") ||
                 strstr(filename, "libEGL") ||
                 strstr(filename, "libGL")) {
            printf("Static build: Allowing graphics library: dlopen(\"%s\")\n", filename);
            result = __real_dlopen(filename, flag);
        }
        else {
            // Block all other dynamic libraries
            printf("Static build: dlopen(\"%s\") disabled\n", filename);
            result = NULL;
        }
    }
    
    in_dlopen_wrapper = 0;
    return result;
}
