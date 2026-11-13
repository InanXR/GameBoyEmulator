#pragma once
#include "cpu/cpu.h"
#include "memory/mmu.h"
#include "graphics/ppu.h"
#include "common/types.h"
#include <array>
#include <string>

/**
 * Emulation session.
 *
 * Owns the machine (CPU, MMU incl. APU/timer/cartridge, PPU) and drives
 * it one frame at a time through runFrame(). The core is platform-
 * free: the SDL frontend (src/platform/main.cpp) owns the windowed
 * loop, display input, audio device, hotkeys and frame pacing, and the
 * automated test suite drives it headless.
 */
class Emulator {
public:
    Emulator();
    ~Emulator();

    // Non-copyable: owns the whole machine.
    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;

    // ---- session control -------------------------------------------
    bool loadROM(const std::string& romPath);
    void reset();          // power-cycle, keeping the battery save
    void runFrame();       // exactly 70224 T-cycles of emulation
    void runHeadless(int frames);  // run N frames, no window required

    // ---- save states ------------------------------------------------
    // Uses "<rom>.state" (see siblingPath).
    bool saveState();
    bool loadState();

    // ---- frontend accessors -----------------------------------------
    bool isRunning() const { return running; }
    const std::string& getSerialOutput() const { return mmu.getSerialOutput(); }
    const std::array<u8, 160 * 144>& framebuffer() const { return ppu.framebuffer; }

    Joypad& joypad() { return mmu.getJoypad(); }
    APU& audio() { return mmu.getAPU(); }

    // File path with the ROM's extension replaced (e.g. ".state",
    // ".sav", ".ppm"). Used by the frontend for screenshots and by
    // the session for save states.
    std::string siblingPath(const char* extension) const;

private:
    CPU cpu;
    MMU mmu;
    PPU ppu;

    std::string rom_path;
    std::string state_path;

    bool running;
    u32 frameCount;
};
