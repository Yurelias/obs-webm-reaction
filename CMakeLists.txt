cmake_minimum_required(VERSION 3.16)
project(webm-reaction VERSION 1.0.0)

set(CMAKE_C_STANDARD 11)

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

add_library(webm-reaction MODULE webm-reaction.c)
target_link_libraries(webm-reaction PRIVATE OBS::libobs)
set_target_properties(webm-reaction PROPERTIES PREFIX "")
