# ============================================================================
# Targets
#
#   gameboy_core - static library: the complete headless emulation core.
#                  No platform dependencies (pure C++17 + std::thread).
#   gameboy     - SDL2 frontend executable linking the core.
#
# Include roots (PUBLIC on the core, so consumers inherit them):
#   <src>       - platform and tests reference the core as "core/..."
#   <src>/core  - core-internal includes are root-relative ("memory/mmu.h")
# ============================================================================

# ---- Core library -----------------------------------------------------------

add_library(gameboy_core STATIC
    core/cpu/cpu.cpp
    core/cpu/instructions.cpp
    core/memory/mmu.cpp
    core/memory/cartridge.cpp
    core/graphics/ppu.cpp
    core/audio/apu.cpp
    core/timer/timer.cpp
    core/input/joypad.cpp
    core/emulator.cpp
    core/savestate.cpp
)

target_include_directories(gameboy_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
)

target_compile_features(gameboy_core PUBLIC cxx_std_17)

target_link_libraries(gameboy_core
    PUBLIC
        Threads::Threads
    PRIVATE
        gb_warnings
)

# ---- SDL2 frontend ----------------------------------------------------------

add_executable(gameboy
    platform/main.cpp
    platform/display.cpp
    platform/audio_backend.cpp
)

target_link_libraries(gameboy PRIVATE
    gameboy_core
    gb_warnings
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    ${SDL2_LINK_LIBS}
)

# Module discovery provides include dirs on some distros.
if(DEFINED SDL2_INCLUDE_DIRS AND NOT TARGET SDL2::SDL2)
    target_include_directories(gameboy PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

# Windows: copy the SDL2 DLL next to the executable after building.
if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET gameboy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:gameboy>
    )
endif()
