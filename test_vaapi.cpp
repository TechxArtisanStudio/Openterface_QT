#include <iostream>
#include <dlfcn.h>

// Forward declarations for our wrapper functions
extern "C" void* __wrap_dlopen(const char* filename, int flag);

int main() {
    std::cout << "Testing VAAPI dlopen wrapper..." << std::endl;
    
    // Test VAAPI library loading (should be allowed)
    void* va_handle = __wrap_dlopen("libva.so.2", RTLD_LAZY);
    if (va_handle) {
        std::cout << "✓ VAAPI library (libva.so.2) successfully loaded" << std::endl;
        dlclose(va_handle);
    } else {
        std::cout << "✗ VAAPI library (libva.so.2) failed to load" << std::endl;
    }
    
    // Test VA-DRM library loading (should be allowed)
    void* va_drm_handle = __wrap_dlopen("libva-drm.so.2", RTLD_LAZY);
    if (va_drm_handle) {
        std::cout << "✓ VA-DRM library (libva-drm.so.2) successfully loaded" << std::endl;
        dlclose(va_drm_handle);
    } else {
        std::cout << "✗ VA-DRM library (libva-drm.so.2) failed to load" << std::endl;
    }
    
    // Test a random library that should be blocked
    void* random_handle = __wrap_dlopen("librandom.so", RTLD_LAZY);
    if (random_handle) {
        std::cout << "✗ Random library (librandom.so) was incorrectly allowed" << std::endl;
        dlclose(random_handle);
    } else {
        std::cout << "✓ Random library (librandom.so) correctly blocked" << std::endl;
    }
    
    return 0;
}
