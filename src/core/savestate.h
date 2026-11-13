#pragma once
#include "common/types.h"
#include <string>
#include <fstream>

// Forward declarations
class CPU;
class MMU;
class PPU;
class APU;
class Timer;

/**
 * Save State Manager
 *
 * Saves and loads the complete emulator state to/from binary files.
 * Format: "GBSTATE" magic + version byte + per-component state.
 *
 * v1 (2025): CPU, MMU, cartridge only (PPU/APU/timer were stubs)
 * v2 (2026): complete state - CPU, MMU (incl. timer + APU registers),
 *            cartridge (MBC + RTC), PPU (scanline/mode/window), APU
 *            (all channels), timer (DIV phase).
 */
class SaveState {
public:
    static bool save(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer);

    static bool load(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer);

private:
    static constexpr const char* MAGIC = "GBSTATE";
    static constexpr u8 VERSION = 2;
};
