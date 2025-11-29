#pragma once
#include "../utils/types.h"
#include "cartridge.h"
#include "../input/joypad.h"
#include "../timer/timer.h"
#include "../audio/apu.h"
#include <memory>
#include <array>
#include <fstream>

/**
 * Memory Management Unit
 * 
 * Handles all GameBoy memory accesses and mapping:
 * - ROM banks (cartridge)
 * - RAM banks (cartridge)
 * - Video RAM (VRAM)
 * - Work RAM (WRAM)
 * - OAM (sprite data)
 * - I/O registers
 * - High RAM (HRAM)
 */
class MMU {
public:
    MMU();
    
    // Load a cartridge ROM file
    void loadCartridge(const std::string& filepath);
    
    // Memory access
    u8 read(u16 addr) const;
    void write(u16 addr, u8 value);
    
    // Special access for PPU to update LY (0xFF44) without resetting it
    void setLY(u8 value) { io[0x44] = value; }
    
    // DMA transfer (used by PPU for sprite data)
    void doDMATransfer(u8 value);
    
    // Direct VRAM access (for PPU)
    const std::array<u8, 0x2000>& getVRAM() const { return vram; }
    
    // Direct OAM access (for PPU)
    const std::array<u8, 0xA0>& getOAM() const { return oam; }
    
    // Joypad access (public for direct button updates)
    Joypad& getJoypad() { return joypad; }
    
    // Timer access
    Timer& getTimer() { return timer; }
    
    // APU access
    APU& getAPU() { return apu; }
    
    // Save/Load state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);
    
private:
    // Cartridge (ROM and external RAM)
    std::unique_ptr<Cartridge> cartridge;
    
    // Internal memory arrays
    std::array<u8, 0x2000> wram;   // Work RAM (8KB) 0xC000-0xDFFF
    std::array<u8, 0x2000> vram;   // Video RAM (8KB) 0x8000-0x9FFF
    std::array<u8, 0x80>   hram;   // High RAM (128 bytes) 0xFF80-0xFFFE
    std::array<u8, 0xA0>   oam;    // Object Attribute Memory (160 bytes) 0xFE00-0xFE9F
    
    // I/O Registers (128 bytes) 0xFF00-0xFF7F
    std::array<u8, 0x80> io;
    
    // Joypad controller (mutable so it can be read in const read())
    mutable Joypad joypad;
    
    // Timer
    Timer timer;
    
    // APU (Audio)
    APU apu;
    
    // Interrupt Enable Register (0xFFFF)
    u8 ie_register;
};
