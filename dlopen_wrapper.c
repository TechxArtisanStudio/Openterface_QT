#include <dlfcn.h>
#include <stdio.h>

// Wrapper function for dlopen to satisfy Qt6's symbol requirements
void* __wrap_dlopen(const char* filename, int flag) {
    // For a static build, we don't want to load dynamic libraries
    // Return NULL to indicate failure, which Qt6 should handle gracefully
    if (filename) {
        printf("Static build: dlopen(\"%s\") disabled\n", filename);
    }
    return NULL;
}

// Also provide __real_dlopen if needed
void* __real_dlopen(const char* filename, int flag) {
    return dlopen(filename, flag);
}
