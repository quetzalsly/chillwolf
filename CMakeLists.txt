######################################################################

cmake_minimum_required(VERSION 4.3.2...5.3.3 FATAL_ERROR)

######################################################################

project(ChillWolf3D
    VERSION 1.0.0
    DESCRIPTION "ChillWolf3D"
    HOMEPAGE_URL "https://github.com/quetzalsly/chillwolf"
    LANGUAGES C CXX
)

######################################################################

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set_property(GLOBAL PROPERTY USE_FOLDERS ON FORCE)
set(CMAKE_SUPPRESS_REGENERATION ON FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>" CACHE PATH "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>" CACHE PATH "" FORCE)

######################################################################

if(NOT WIN32)
    message(FATAL_ERROR "This project is Windows-only.")
endif()

if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
    message(FATAL_ERROR "This project is x86-only. Regenerate with: cmake -B build -A Win32")
endif()

if(CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "This project is x86-only. Regenerate with: cmake -B build -A Win32")
endif()

######################################################################

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    CACHE STRING "" FORCE
)

add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)
add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

######################################################################

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(optimization_full)

######################################################################

enable_language(RC)

######################################################################

add_compile_definitions(
    "WIN32_LEAN_AND_MEAN"
    "NOMINMAX"
)

######################################################################

set(NATIVE_APP_NAME "${PROJECT_NAME}" CACHE STRING "Native app target name")
set(NATIVE_APP_DISPLAY_NAME "${PROJECT_DESCRIPTION}" CACHE STRING "Native app display name")
set(NATIVE_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Native app version")
set(NATIVE_APP_DEVELOPER "quetzalsly" CACHE STRING "Native app developer")
set(NATIVE_APP_PUBLISHER "${NATIVE_APP_DEVELOPER}" CACHE STRING "Native app publisher")
set(NATIVE_APP_URL "${PROJECT_HOMEPAGE_URL}" CACHE STRING "Native app URL")

######################################################################

set(_DEFAULT_SDL_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL-1.2.15/lib/x86")
set(_DEFAULT_SDL_MIXER_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL_mixer-1.2.12/lib/x86")
set(_DEFAULT_GAME_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gamedata")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/win32/vs2008/choco_wold3s/SDL-1.2.15/lib/x86")
    set(_DEFAULT_SDL_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/win32/vs2008/choco_wold3s/SDL-1.2.15/lib/x86")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/win32/vs2008/choco_wold3s/SDL_mixer-1.2.12/lib/x86")
    set(_DEFAULT_SDL_MIXER_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/win32/vs2008/choco_wold3s/SDL_mixer-1.2.12/lib/x86")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    set(_DEFAULT_GAME_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

set(CHILLWOLF_SDL_LIBRARY_DIR "${_DEFAULT_SDL_LIBRARY_DIR}" CACHE PATH "SDL 1.2 x86 library directory")
set(CHILLWOLF_SDL_MIXER_LIBRARY_DIR "${_DEFAULT_SDL_MIXER_LIBRARY_DIR}" CACHE PATH "SDL_mixer 1.2 x86 library directory")
set(CHILLWOLF_GAME_DATA_DIR "${_DEFAULT_GAME_DATA_DIR}" CACHE PATH "Game data directory to copy beside the executable")

######################################################################

function(require_existing_directory VARIABLE_NAME)
    if("${${VARIABLE_NAME}}" STREQUAL "")
        message(FATAL_ERROR "${VARIABLE_NAME} is empty.")
    endif()

    if(NOT IS_DIRECTORY "${${VARIABLE_NAME}}")
        message(FATAL_ERROR "${VARIABLE_NAME} does not point to an existing directory: ${${VARIABLE_NAME}}")
    endif()
endfunction()

######################################################################

function(setup_win32_target_data TARGET ICON_PATH)
    set(APP_NAME "${NATIVE_APP_NAME}")
    set(APP_DISPLAY_NAME "${NATIVE_APP_DISPLAY_NAME}")
    set(APP_VERSION "${NATIVE_APP_VERSION}")
    set(APP_DEVELOPER "${NATIVE_APP_DEVELOPER}")
    set(APP_PUBLISHER "${NATIVE_APP_PUBLISHER}")
    set(APP_URL "${NATIVE_APP_URL}")

    if(APP_NAME STREQUAL "")
        set(APP_NAME "ChillWolf3D")
    endif()

    if(APP_DISPLAY_NAME STREQUAL "")
        set(APP_DISPLAY_NAME "ChillWolf3D")
    endif()

    if(APP_VERSION STREQUAL "")
        set(APP_VERSION "1.0.0")
    endif()

    if(APP_DEVELOPER STREQUAL "")
        set(APP_DEVELOPER "quetzalsly")
    endif()

    if(APP_PUBLISHER STREQUAL "")
        set(APP_PUBLISHER "${APP_DEVELOPER}")
    endif()

    if(APP_URL STREQUAL "")
        set(APP_URL "https://github.com/quetzalsly/chillwolf")
    endif()

    get_filename_component(APP_ICON "${ICON_PATH}" ABSOLUTE)

    if(NOT EXISTS "${APP_ICON}")
        message(FATAL_ERROR "[setup_win32_target_data] Icon not found: ${APP_ICON}")
    endif()

    string(TIMESTAMP CURRENT_YEAR "%Y")
    set(APP_COPYRIGHT "© ${CURRENT_YEAR} ${APP_DEVELOPER}. All Rights Reserved.")

    string(REPLACE "." ";" VERSION_LIST "${APP_VERSION}")
    list(LENGTH VERSION_LIST VERSION_LIST_LENGTH)

    if(VERSION_LIST_LENGTH LESS 3)
        message(FATAL_ERROR "[setup_win32_target_data] APP_VERSION must be major.minor.patch, got: ${APP_VERSION}")
    endif()

    list(GET VERSION_LIST 0 APP_VERSION_MAJOR)
    list(GET VERSION_LIST 1 APP_VERSION_MINOR)
    list(GET VERSION_LIST 2 APP_VERSION_PATCH)

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_version.rc"
        @ONLY
    )

    target_sources(${TARGET} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_version.rc"
        "${APP_ICON}"
    )
endfunction()

######################################################################

function(add_manual_files_internal TARGET ROOT_DIR)
    set(options ONLY_ROOT)
    cmake_parse_arguments(AMFI "${options}" "" "" ${ARGN})

    get_filename_component(ROOT_DIR "${ROOT_DIR}" ABSOLUTE)

    set(rel_files ${AMFI_UNPARSED_ARGUMENTS})
    set(full_files "")
    set(root_dirs "")

    foreach(f IN LISTS rel_files)
        set(full_path "${ROOT_DIR}/${f}")

        if(NOT EXISTS "${full_path}")
            message(FATAL_ERROR "[add_manual_files_internal] File not found: ${full_path}")
        endif()

        list(APPEND full_files "${full_path}")

        if(NOT AMFI_ONLY_ROOT)
            get_filename_component(dir "${f}" DIRECTORY)
            string(FIND "${dir}" "/" slash_pos)

            if(slash_pos EQUAL -1)
                set(root "${dir}")
            else()
                string(SUBSTRING "${dir}" 0 ${slash_pos} root)
            endif()

            if(NOT root STREQUAL "")
                list(APPEND root_dirs "${ROOT_DIR}/${root}")
            endif()
        endif()
    endforeach()

    if(AMFI_ONLY_ROOT)
        set(root_dirs "${ROOT_DIR}")
    else()
        list(REMOVE_DUPLICATES root_dirs)
    endif()

    if(root_dirs)
        target_include_directories(${TARGET} PRIVATE ${root_dirs})
    endif()

    if(full_files)
        list(REMOVE_DUPLICATES full_files)
        list(SORT full_files)

        target_sources(${TARGET} PRIVATE ${full_files})

        get_filename_component(_sg_name "${ROOT_DIR}" NAME)
        source_group(TREE "${ROOT_DIR}" PREFIX "${_sg_name}" FILES ${full_files})
    endif()
endfunction()

######################################################################

function(add_manual_files TARGET ROOT_DIR)
    add_manual_files_internal(${TARGET} "${ROOT_DIR}" ${ARGN})
endfunction()

######################################################################

function(add_manual_files_glob TARGET ROOT_DIR)
    get_filename_component(ROOT_DIR "${ROOT_DIR}" ABSOLUTE)

    file(GLOB_RECURSE rel_files
        LIST_DIRECTORIES FALSE
        CONFIGURE_DEPENDS
        RELATIVE "${ROOT_DIR}"
        "${ROOT_DIR}/*.h"
        "${ROOT_DIR}/*.hpp"
        "${ROOT_DIR}/*.ipp"
        "${ROOT_DIR}/*.inl"
        "${ROOT_DIR}/*.c"
        "${ROOT_DIR}/*.cc"
        "${ROOT_DIR}/*.cpp"
        "${ROOT_DIR}/*.natvis"
        "${ROOT_DIR}/*.natstepfilter"
    )

    add_manual_files_internal(${TARGET} "${ROOT_DIR}" ONLY_ROOT ${rel_files})
endfunction()

######################################################################

function(copy_directory_to_output_configs TARGET SOURCE_DIR)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[copy_directory_to_output_configs] Unknown target: ${TARGET}")
    endif()

    get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

    if(NOT IS_DIRECTORY "${SOURCE_DIR}")
        message(FATAL_ERROR "[copy_directory_to_output_configs] Directory not found: ${SOURCE_DIR}")
    endif()

    set(commands "")

    foreach(config_name Debug Release)
        list(APPEND commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/${config_name}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SOURCE_DIR}" "${CMAKE_BINARY_DIR}/${config_name}"
        )
    endforeach()

    add_custom_command(
        TARGET ${TARGET}
        POST_BUILD
        ${commands}
        COMMENT "Copying '${SOURCE_DIR}' to Debug and Release output folders"
        VERBATIM
    )
endfunction()

######################################################################

function(copy_dlls_to_output_configs TARGET SOURCE_DIR)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[copy_dlls_to_output_configs] Unknown target: ${TARGET}")
    endif()

    get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

    if(NOT IS_DIRECTORY "${SOURCE_DIR}")
        message(FATAL_ERROR "[copy_dlls_to_output_configs] Directory not found: ${SOURCE_DIR}")
    endif()

    file(GLOB dll_files
        LIST_DIRECTORIES FALSE
        CONFIGURE_DEPENDS
        "${SOURCE_DIR}/*.dll"
    )

    if(NOT dll_files)
        message(WARNING "[copy_dlls_to_output_configs] No DLL files found in: ${SOURCE_DIR}")
        return()
    endif()

    set(commands "")

    foreach(config_name Debug Release)
        list(APPEND commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/${config_name}"
        )

        foreach(dll_file IN LISTS dll_files)
            list(APPEND commands
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dll_file}" "${CMAKE_BINARY_DIR}/${config_name}/"
            )
        endforeach()
    endforeach()

    add_custom_command(
        TARGET ${TARGET}
        POST_BUILD
        ${commands}
        COMMENT "Copying DLLs from '${SOURCE_DIR}' to Debug and Release output folders"
        VERBATIM
    )
endfunction()

######################################################################

function(add_native_app NATIVE_APP_DIR)
    get_filename_component(NATIVE_APP_DIR "${NATIVE_APP_DIR}" ABSOLUTE)

    require_existing_directory(CHILLWOLF_SDL_LIBRARY_DIR)
    require_existing_directory(CHILLWOLF_SDL_MIXER_LIBRARY_DIR)
    require_existing_directory(CHILLWOLF_GAME_DATA_DIR)

    get_filename_component(CHILLWOLF_SDL_ROOT_DIR "${CHILLWOLF_SDL_LIBRARY_DIR}/../.." ABSOLUTE)
    get_filename_component(CHILLWOLF_SDL_MIXER_ROOT_DIR "${CHILLWOLF_SDL_MIXER_LIBRARY_DIR}/../.." ABSOLUTE)

    set(CHILLWOLF_SDL_INCLUDE_DIR "${CHILLWOLF_SDL_ROOT_DIR}/include")
    set(CHILLWOLF_SDL_MIXER_INCLUDE_DIR "${CHILLWOLF_SDL_MIXER_ROOT_DIR}/include")

    require_existing_directory(CHILLWOLF_SDL_INCLUDE_DIR)
    require_existing_directory(CHILLWOLF_SDL_MIXER_INCLUDE_DIR)

    add_executable(${NATIVE_APP_NAME})

    add_manual_files_glob(${NATIVE_APP_NAME} "${NATIVE_APP_DIR}")

    target_include_directories(${NATIVE_APP_NAME} PRIVATE
        "${NATIVE_APP_DIR}"
        "${CHILLWOLF_SDL_INCLUDE_DIR}"
        "${CHILLWOLF_SDL_MIXER_INCLUDE_DIR}"
    )

    target_link_directories(${NATIVE_APP_NAME} PRIVATE
        "${CHILLWOLF_SDL_LIBRARY_DIR}"
        "${CHILLWOLF_SDL_MIXER_LIBRARY_DIR}"
    )

    target_compile_definitions(${NATIVE_APP_NAME} PRIVATE
        WIN32
        _CONSOLE
        UNICODE
        _UNICODE
        NATIVE_APP_NAME="${NATIVE_APP_NAME}"
        NATIVE_APP_DISPLAY_NAME="${NATIVE_APP_DISPLAY_NAME}"
        NATIVE_APP_VERSION="${NATIVE_APP_VERSION}"
        NATIVE_APP_DEVELOPER="${NATIVE_APP_DEVELOPER}"
        NATIVE_APP_PUBLISHER="${NATIVE_APP_PUBLISHER}"
        NATIVE_APP_URL="${NATIVE_APP_URL}"
    )

    setup_win32_target_data(
        ${NATIVE_APP_NAME}
        "${NATIVE_APP_DIR}/app.ico"
    )

    target_link_libraries(${NATIVE_APP_NAME} PRIVATE
        SDL
        SDLmain
        SDL_mixer
        opengl32
        user32
        gdi32
    )

    if(MSVC)
        target_link_libraries(${NATIVE_APP_NAME} PRIVATE
            legacy_stdio_definitions
        )
    endif()

    copy_dlls_to_output_configs(${NATIVE_APP_NAME} "${CHILLWOLF_SDL_LIBRARY_DIR}")
    copy_dlls_to_output_configs(${NATIVE_APP_NAME} "${CHILLWOLF_SDL_MIXER_LIBRARY_DIR}")
    copy_directory_to_output_configs(${NATIVE_APP_NAME} "${CHILLWOLF_GAME_DATA_DIR}")

    set_target_properties(${NATIVE_APP_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${NATIVE_APP_NAME}>"
    )
endfunction()

######################################################################

add_native_app("${CMAKE_CURRENT_SOURCE_DIR}")

######################################################################
