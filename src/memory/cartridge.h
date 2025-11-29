#pragma once
#include "../utils/types.h"
#include <vector>
#include <string>
#include <fstream>

/**
 * Cartridge / ROM Handler
 * 
 * Handles loading GameBoy ROM files and implementing
 * Memory Bank Controllers (MBC) for bank switching.
 * 
 * Implemented: MBC1, MBC2, MBC3, MBC5
 */
class Cartridge {
public:
    // Load cartridge from file
    explicit Cartridge(const std::string& filepath);
    
    // ROM access (handles banking)
    u8 readROM(u16 addr) const;
    void writeROM(u16 addr, u8 value);  // For MBC control
    
    // RAM access (handles banking)
    u8 readRAM(u16 addr) const;
    void writeRAM(u16 addr, u8 value);
    
    // Get cartridge title from header
    std::string getTitle() const;
    
    // Save/Load state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);
    
private:
    // ROM and RAM data
    std::vector<u8> rom;
    std::vector<u8> ram;
    
    // MBC state
    u8 mbc_type;
    int rom_bank;      // Current ROM bank (1-511 for MBC5)
    int ram_bank;      // Current RAM bank (0-15 for MBC5)
    bool ram_enabled;  // RAM enable flag
    bool rom_banking;  // ROM banking mode (vs RAM banking mode)
    
    // MBC3 RTC (Real-Time Clock) state
    u8 rtc_seconds = 0;
    u8 rtc_minutes = 0;
    u8 rtc_hours = 0;
    u8 rtc_days_low = 0;
    u8 rtc_days_high = 0;  // bit 0: day MSB, bit 6: halt, bit 7: carry
    bool rtc_latched = false;
    u8 rtc_latch_state = 0xFF;  // For detecting 0x00->0x01 transition
    
    // Parse MBC type and RAM size from ROM header
    void parseMBC();
};
