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
 *
 * Hardware model: a cartridge whose header CGB flag ($0143) has bit 7
 * set boots as a Game Boy Color (CGB registers, banked VRAM/WRAM,
 * palette RAM, double-speed CPU); everything else boots as a DMG. The
 * frontend can force DMG with the --dmg flag (for CGB palettes-in-
 * monochrome comparisons or debugging).
 */
class Emulator {
public:
    Emulator();
    ~Emulator();

    // Non-copyable: owns the whole machine.
    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;

    // ---- session control -------------------------------------------
    // force_dmg: ignore the cartridge's CGB flag and boot as a DMG.
    bool loadROM(const std::string& romPath, bool force_dmg = false);
    void reset();          // power-cycle, keeping the battery save
    void runFrame();       // one PPU frame (70224 dots) of emulation
    void runHeadless(int frames);  // run N frames, no window required

    // ---- save states ------------------------------------------------
    // Uses "<rom>.state" (see siblingPath).
    bool saveState();
    bool loadState();

    // ---- frontend accessors -----------------------------------------
    bool isRunning() const { return running; }
    bool isCGB() const { return mmu.isCGB(); }
    bool isDoubleSpeed() const { return mmu.isDoubleSpeed(); }
    const std::string& getSerialOutput() const { return mmu.getSerialOutput(); }
    const std::array<u16, 160 * 144>& framebuffer() const { return ppu.framebuffer; }

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
