#pragma once
#include "utils/types.h"
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
 * Manages saving and loading complete emulator state to/from files.
 * Format: Binary .state files containing all component states
 */
class SaveState {
public:
    // Save complete state to file
    static bool save(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer);
    
    // Load complete state from file
    static bool load(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer);
    
private:
    static constexpr const char* MAGIC = "GBSTATE";
    static constexpr u8 VERSION = 1;
};
