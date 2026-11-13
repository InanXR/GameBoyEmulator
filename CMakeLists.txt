cmake_minimum_required(VERSION 3.16)

project(GameBoyEmulator
    VERSION 2.0.0
    DESCRIPTION "Game Boy (DMG) emulator: C++17 core + SDL2 frontend"
    LANGUAGES CXX
)

# ============================================================================
# Options
# ============================================================================

option(BUILD_TESTS
       "Build the unit test target and register it with CTest" ON)
option(GB_DOWNLOAD_TESTS
       "Register the ROM-downloading accuracy suite (tests/run_tests.sh) \
as a CTest test. Off by default so plain 'ctest' stays offline." OFF)

# ============================================================================
# Default build type
# ============================================================================

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# ============================================================================
# Dependencies
# ============================================================================

# SDL2: prefer the CMake config package, fall back to the classic
# module / sdl2-config discovery so the build works on every distro.
find_package(SDL2 QUIET CONFIG)
if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED MODULE)
endif()

find_package(Threads REQUIRED)

# Resolve the SDL2 target name once; consumers just use ${SDL2_LINK_LIBS}.
if(TARGET SDL2::SDL2)
    set(SDL2_LINK_LIBS SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    set(SDL2_LINK_LIBS SDL2::SDL2-static)
else()
    set(SDL2_LINK_LIBS ${SDL2_LIBRARIES})
endif()

# ============================================================================
# Shared helper modules
# ============================================================================

include(cmake/CompilerWarnings.cmake)

# ============================================================================
# Layout
# ============================================================================

# Keep executables at the build root (build/gameboy) regardless of the
# source subdirectory that defines them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ============================================================================
# Targets
# ============================================================================

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Status
# ============================================================================

message(STATUS "GameBoy Emulator ${PROJECT_VERSION}")
message(STATUS "  C++ standard: 17")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  SDL2:         ${SDL2_VERSION} (${SDL2_LINK_LIBS})")
message(STATUS "  Tests:        ${BUILD_TESTS} (download suite: ${GB_DOWNLOAD_TESTS})")
