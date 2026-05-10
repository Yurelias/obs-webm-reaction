cmake_minimum_required(VERSION 3.16)
project(webm-reaction VERSION 1.0.0)

set(CMAKE_C_STANDARD 11)

# ── Find OBS ────────────────────────────────────────────────────────────────
# When building inside the OBS source tree this is already set.
# For out-of-tree builds, point LIBOBS_INCLUDE_DIR and LIBOBS_LIB manually
# (see README), or use the CMake package:
find_package(libobs QUIET)

if(NOT libobs_FOUND)
    # Fallback: manual paths (adjust as needed)
    if(NOT DEFINED LIBOBS_INCLUDE_DIR)
        set(LIBOBS_INCLUDE_DIR "/usr/include/obs" CACHE PATH "Path to libobs headers")
    endif()
    if(NOT DEFINED LIBOBS_LIB)
        find_library(LIBOBS_LIB obs REQUIRED)
    endif()
    add_library(OBS::libobs UNKNOWN IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}"
        IMPORTED_LOCATION "${LIBOBS_LIB}")
else()
    add_library(OBS::libobs ALIAS libobs)
endif()

# ── Plugin target ────────────────────────────────────────────────────────────
add_library(webm-reaction MODULE webm-reaction.c)

target_link_libraries(webm-reaction PRIVATE OBS::libobs m)

target_compile_options(webm-reaction PRIVATE
    -Wall -Wextra -Wno-unused-parameter)

set_target_properties(webm-reaction PROPERTIES PREFIX "")  # no "lib" prefix on Linux if desired

# ── Install / copy ────────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    install(TARGETS webm-reaction
        LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/64bit")
    install(DIRECTORY data/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/data")

    # Convenience target: install into user plugin directory
    add_custom_target(install-user
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$ENV{HOME}/.config/obs-studio/plugins/webm-reaction/bin/64bit"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:webm-reaction>"
            "$ENV{HOME}/.config/obs-studio/plugins/webm-reaction/bin/64bit/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/data"
            "$ENV{HOME}/.config/obs-studio/plugins/webm-reaction/data"
        COMMENT "Installing webm-reaction plugin to user OBS plugin directory"
        DEPENDS webm-reaction)
endif()

if(WIN32)
    install(TARGETS webm-reaction
        RUNTIME DESTINATION "obs-plugins/64bit")
    install(DIRECTORY data/
        DESTINATION "data/obs-plugins/webm-reaction")
endif()

if(APPLE)
    install(TARGETS webm-reaction
        LIBRARY DESTINATION "webm-reaction/bin")
    install(DIRECTORY data/
        DESTINATION "webm-reaction/data")
endif()
