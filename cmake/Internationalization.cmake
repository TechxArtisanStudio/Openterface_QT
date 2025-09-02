# Internationalization.cmake - Qt6 Translation Management

# Option to enable/disable automatic translation processing
option(OPENTERFACE_AUTO_TRANSLATIONS "Automatically update translations before build" ON)

message(STATUS "=== Qt Translation Configuration ===")

if(OPENTERFACE_AUTO_TRANSLATIONS)
    message(STATUS "Automatic translation processing enabled")
    
    # Always create a target to run the translation update script
    # This is the most reliable method across different environments
    add_custom_target(update_translations
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/update_translations.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating and compiling translations using script"
        VERBATIM
    )
    
    # Check if translation script exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/update_translations.sh")
        message(STATUS "✓ Translation script found: scripts/update_translations.sh")
        message(STATUS "✓ Run 'make update_translations' to update translations")
        message(STATUS "✓ Or run directly: ./scripts/update_translations.sh")
    else()
        message(WARNING "⚠ Translation script not found at scripts/update_translations.sh")
    endif()
    
    # Check if .ts files exist
    set(TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_da.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_de.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_en.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_fr.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_ja.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_se.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_zh.ts
    )
    
    set(ts_count 0)
    foreach(ts_file ${TS_FILES})
        if(EXISTS ${ts_file})
            math(EXPR ts_count "${ts_count} + 1")
        endif()
    endforeach()
    
    message(STATUS "✓ Found ${ts_count} translation source files (.ts)")
    
    # Check if .qm files exist
    set(QM_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_da.qm
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_de.qm
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_en.qm
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_fr.qm
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_ja.qm
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_se.qm
        ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_zh.qm
    )
    
    set(qm_count 0)
    foreach(qm_file ${QM_FILES})
        if(EXISTS ${qm_file})
            math(EXPR qm_count "${qm_count} + 1")
        endif()
    endforeach()
    
    message(STATUS "✓ Found ${qm_count} compiled translation files (.qm)")
    
    if(${qm_count} LESS ${ts_count})
        message(STATUS "ℹ Some .qm files missing - run translation update to generate them")
    endif()
    
else()
    message(STATUS "Automatic translation processing disabled")
    message(STATUS "Set -DOPENTERFACE_AUTO_TRANSLATIONS=ON to enable")
    message(STATUS "Or run manually: ./scripts/update_translations.sh")
endif()

message(STATUS "=== End Translation Configuration ===")

# Additional helper target for testing translation setup
add_custom_target(test_translations
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_translations.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Testing translation setup"
    VERBATIM
)
