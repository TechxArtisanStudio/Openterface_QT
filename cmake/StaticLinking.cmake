# StaticLinking.cmake - Force static linking of compression libraries

# Function to force static linking of compression libraries on Windows
function(force_static_compression_libraries target)
    if(WIN32)
        message(STATUS "Forcing static linking of compression libraries for ${target}")
        
        # Define MINGW_ROOT if not set
        if(NOT DEFINED MINGW_ROOT)
            set(MINGW_ROOT "C:/msys64/mingw64")
        endif()
        
        # List of compression libraries to force static linking
        set(COMPRESSION_LIBRARIES
            zstd
            brotlidec
            brotlienc
            brotlicommon
            z
            bz2
            lzma
        )
        
        # Build list of static library paths
        set(STATIC_LIB_PATHS "")
        set(MISSING_LIBS "")
        
        foreach(lib ${COMPRESSION_LIBRARIES})
            set(lib_path "${MINGW_ROOT}/lib/lib${lib}.a")
            if(EXISTS "${lib_path}")
                list(APPEND STATIC_LIB_PATHS "${lib_path}")
                message(STATUS "  Found static library: ${lib_path}")
            else()
                list(APPEND MISSING_LIBS "${lib}")
                message(WARNING "  Missing static library: ${lib_path}")
            endif()
        endforeach()
        
        # If any libraries are missing, warn the user
        if(MISSING_LIBS)
            message(WARNING "Missing static compression libraries: ${MISSING_LIBS}")
            message(WARNING "Install them with: pacman -S ${MISSING_LIBS}")
        endif()
        
        # Link found static libraries
        if(STATIC_LIB_PATHS)
            target_link_libraries(${target} PRIVATE ${STATIC_LIB_PATHS})
            message(STATUS "Linked static compression libraries: ${STATIC_LIB_PATHS}")
        endif()
        
        # Set library suffixes to prefer .a files for this target
        set_property(GLOBAL PROPERTY CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        
        message(STATUS "Static compression library linking configured for ${target}")
    endif()
endfunction()

# Function to verify no dynamic compression libraries are linked
function(verify_no_dynamic_compression_libs target)
    if(WIN32)
        # This will be checked at build time via the objdump verification
        # in the GitHub Actions workflow
        message(STATUS "Dynamic compression library verification will be performed after build")
    endif()
endfunction()