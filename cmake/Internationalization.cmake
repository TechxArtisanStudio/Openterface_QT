# Internationalization.cmake - Qt6 Translation Management

# Option to enable/disable automatic translation processing
option(OPENTERFACE_AUTO_TRANSLATIONS "Automatically update translations before build" ON)

# Define translation source files (.ts files) globally
set(TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_da.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_de.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_fr.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_ja.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_se.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/config/languages/openterface_zh.ts
)

# Function to setup translations for a target (to be called after target creation)
function(setup_translations TARGET_NAME)
    if(OPENTERFACE_AUTO_TRANSLATIONS)
        # Find Qt6 LinguistTools (optional to avoid breaking builds)
        find_package(Qt6 COMPONENTS LinguistTools QUIET)
        
        if(Qt6_FOUND AND Qt6LinguistTools_FOUND)
            message(STATUS "Qt6 LinguistTools found - enabling automatic translation processing for ${TARGET_NAME}")
            
            # Define all source files that contain translatable strings
            file(GLOB_RECURSE TRANSLATABLE_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/*.h
                ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
            )
            
            # Create custom target for updating translations (lupdate equivalent)
            # This will extract translatable strings from source files and update .ts files
            qt6_add_lupdate(${TARGET_NAME} 
                TS_FILES ${TS_FILES}
                SOURCES ${TRANSLATABLE_SOURCES}
                OPTIONS -no-obsolete
            )
            
            # Create custom target for releasing translations (lrelease equivalent)
            # This will compile .ts files into .qm files
            qt6_add_lrelease(${TARGET_NAME}
                TS_FILES ${TS_FILES}
                QM_FILES_OUTPUT_VARIABLE qm_files
            )
            
            message(STATUS "Qt6 Internationalization configured for ${TARGET_NAME}:")
            message(STATUS "  Translation files: ${TS_FILES}")
            message(STATUS "  Compiled .qm files will be generated automatically")
            
        else()
            # Fallback to script-based approach
            message(STATUS "Qt6 LinguistTools not found - using script-based translation processing")
            
            # Create custom target to run the translation update script
            add_custom_target(update_translations
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/update_translations.sh
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Updating and compiling translations using script"
                VERBATIM
            )
            
            message(STATUS "Translation script target created: update_translations")
            message(STATUS "Run 'make update_translations' to update translations manually")
            message(STATUS "Or run: ./scripts/update_translations.sh")
        endif()
        
    else()
        message(STATUS "Automatic translation processing disabled for ${TARGET_NAME}")
        message(STATUS "Set -DOPENTERFACE_AUTO_TRANSLATIONS=ON to enable")
        message(STATUS "Or run manually: ./scripts/update_translations.sh")
    endif()
endfunction()
