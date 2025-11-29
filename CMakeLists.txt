cmake_minimum_required(VERSION 3.15)
project(GameBoyEmulator VERSION 1.0.0 LANGUAGES CXX)

# 1. DEPENDENCIES
find_package(SDL2 CONFIG REQUIRED)

# 2. C++ STANDARD
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 3. SOURCE FILES
set(SOURCES
    src/main.cpp
    src/emulator.cpp
    src/display.cpp
    src/savestate.cpp
    src/cpu/cpu.cpp
    src/cpu/instructions.cpp
    src/memory/mmu.cpp
    src/memory/cartridge.cpp
    src/graphics/ppu.cpp
    src/input/joypad.cpp
    src/timer/timer.cpp
    src/audio/apu.cpp
)

# 4. EXECUTABLE TARGET
add_executable(gameboy ${SOURCES})

target_include_directories(gameboy PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
)

# 5. LINKING SDL2
target_link_libraries(gameboy PRIVATE 
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)

if(WIN32)
    add_custom_command(TARGET gameboy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:gameboy>
    )
endif()

# 6. COMPILER WARNINGS
if(MSVC)
    target_compile_options(gameboy PRIVATE /W4 /WX-)
else()
    target_compile_options(gameboy PRIVATE -Wall -Wextra)
endif()

# 7. STATUS
message(STATUS "GameBoy Emulator - Build Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  SDL2: ENABLED")
message(STATUS "  Joypad: ENABLED")
message(STATUS "  Timer: ENABLED")
message(STATUS "  Audio: ENABLED")
message(STATUS "  Save States: ENABLED")