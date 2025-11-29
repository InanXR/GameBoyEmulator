#pragma once
#include "cpu/cpu.h"
#include "memory/mmu.h"
#include "graphics/ppu.h"
#include "display.h"
#include "utils/types.h"
#include <string>

/**
 * Main GameBoy Emulator Class
 * Orchestrates all components (CPU, MMU, PPU, Display)
 */
class Emulator {
public:
    Emulator();
    ~Emulator();
    
    // Load a ROM file
    bool loadROM(const std::string& romPath);
    
    // Main emulation loop (for terminal mode)
    void run();
    
    // Run with SDL2 display (Phase 6!)
    void runWithDisplay();
    
private:
    CPU cpu;
    MMU mmu;
    PPU ppu;
    Display display;
    
    bool running;
    u32 frameCount;
    
    // Timing control
    static constexpr int TARGET_FPS = 60;
    static constexpr int CYCLES_PER_FRAME = 70224;  // ~4.194 MHz / 60 Hz
};
