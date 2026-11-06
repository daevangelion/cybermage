cmake_minimum_required(VERSION 3.22.1)
project(cybermage C)

if(DEFINED PORT_SOURCE_DIR)
    get_filename_component(PORT_DIR "${PORT_SOURCE_DIR}" ABSOLUTE)
else()
    get_filename_component(PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
endif()
if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2_SOURCE_DIR must name the existing SDL release-2.30.10 checkout")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL")

# Match the desktop Makefile, including subsystem files one directory deep.
file(GLOB PORT_SOURCES CONFIGURE_DEPENDS "${PORT_DIR}/src/*.c" "${PORT_DIR}/src/*/*.c")
add_library(main SHARED ${PORT_SOURCES})
target_include_directories(main PRIVATE "${PORT_DIR}/include")
target_compile_options(main PRIVATE -std=c99 -pedantic -Wall -Wextra -Werror)
# Same isolated third-party exception as Hexx; keep Nuked-OPL3 unmodified.
set_source_files_properties("${PORT_DIR}/src/audio/opl3.c"
    PROPERTIES COMPILE_OPTIONS -Wno-static-in-inline)
set_source_files_properties("${PORT_DIR}/src/main.c" PROPERTIES COMPILE_DEFINITIONS main=SDL_main)
target_link_libraries(main PRIVATE SDL2 m)
