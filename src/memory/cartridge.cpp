#include "cartridge.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

/**
 * Cartridge Implementation
 * Loads .gb ROM files and implements MBC1
 */

Cartridge::Cartridge(const std::string& filepath) 
    : rom_bank(1), ram_bank(0), ram_enabled(false), rom_banking(true) {
    
    // Open ROM file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open ROM file: " + filepath);
    }
    
    // Get file size
    size_t size = file.tellg();
    rom.resize(size);
    
    // Read entire file into ROM vector
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(rom.data()), size);
    file.close();
    
    // Validate minimum size (cartridge header is at 0x0100-0x014F)
    if (size < 0x0150) {
        throw std::runtime_error("ROM file too small to be a valid GameBoy ROM");
    }
    
    // Parse MBC type and allocate RAM
    parseMBC();
    
    std::cout << "ROM loaded successfully:" << std::endl;
    std::cout << "  Size: " << size / 1024 << " KB" << std::endl;
    std::cout << "  Title: " << getTitle() << std::endl;
}

void Cartridge::parseMBC() {
    // MBC type is at address 0x0147
    mbc_type = rom[0x0147];
    
    // RAM size is at address 0x0149
    u8 ram_size_code = rom[0x0149];
    
    size_t ram_size = 0;
    switch (ram_size_code) {
        case 0x00: ram_size = 0; break;              // No RAM
        case 0x01: ram_size = 2 * 1024; break;       // 2 KB
        case 0x02: ram_size = 8 * 1024; break;       // 8 KB (1 bank)
        case 0x03: ram_size = 32 * 1024; break;      // 32 KB (4 banks of 8KB)
        case 0x04: ram_size = 128 * 1024; break;     // 128 KB (16 banks)
        case 0x05: ram_size = 64 * 1024; break;      // 64 KB (8 banks)
        default:
            std::cerr << "Unknown RAM size code: 0x" << std::hex << (int)ram_size_code << std::endl;
            ram_size = 32 * 1024;  // Default to 32KB
            break;
    }
    
    if (ram_size > 0) {
        ram.resize(ram_size, 0);
        std::cout << "  RAM: " << ram_size / 1024 << " KB" << std::endl;
    } else {
        std::cout << "  RAM: None" << std::endl;
    }
    
    // Log MBC type
    std::cout << "  MBC Type: 0x" << std::hex << (int)mbc_type << std::dec;
    switch (mbc_type) {
        case 0x00: std::cout << " (ROM ONLY)"; break;
        case 0x01: std::cout << " (MBC1)"; break;
        case 0x02: std::cout << " (MBC1+RAM)"; break;
        case 0x03: std::cout << " (MBC1+RAM+BATTERY)"; break;
        case 0x05: std::cout << " (MBC2)"; break;
        case 0x06: std::cout << " (MBC2+BATTERY)"; break;
        case 0x0F: std::cout << " (MBC3+TIMER+BATTERY)"; break;
        case 0x10: std::cout << " (MBC3+TIMER+RAM+BATTERY)"; break;
        case 0x11: std::cout << " (MBC3)"; break;
        case 0x12: std::cout << " (MBC3+RAM)"; break;
        case 0x13: std::cout << " (MBC3+RAM+BATTERY)"; break;
        case 0x19: std::cout << " (MBC5)"; break;
        case 0x1A: std::cout << " (MBC5+RAM)"; break;
        case 0x1B: std::cout << " (MBC5+RAM+BATTERY)"; break;
        default: std::cout << " (UNKNOWN)"; break;
    }
    std::cout << std::endl;
}

std::string Cartridge::getTitle() const {
    // Title is stored at 0x0134-0x0143 (16 bytes)
    std::string title;
    for (int i = 0x0134; i <= 0x0143; i++) {
        if (rom[i] == 0) break;  // Null terminator
        title += static_cast<char>(rom[i]);
    }
    return title;
}

u8 Cartridge::readROM(u16 addr) const {
    if (addr < 0x4000) {
        // Bank 0 - always accessible
        if (addr < rom.size()) {
            return rom[addr];
        }
        return 0xFF;
    } else {
        // Banked ROM (0x4000-0x7FFF)
        // Calculate physical address: bank * 0x4000 + offset
        u32 physical_addr = (rom_bank * 0x4000) + (addr - 0x4000);
        
        if (physical_addr < rom.size()) {
            return rom[physical_addr];
        }
        return 0xFF;
    }
}

u8 Cartridge::readRAM(u16 addr) const {
    if (!ram_enabled || ram.empty()) {
        // Special case: MBC2 has built-in RAM even if external RAM vector is empty
        if (mbc_type == 0x05 || mbc_type == 0x06) {
            // MBC2 has 512 x 4-bit RAM built-in
            if (ram.size() >= 512) {
                return ram[addr & 0x01FF] & 0x0F;  // Only lower 4 bits, 512 bytes
            }
        }
        return 0xFF;  // RAM disabled or not present
    }
    
    // MBC3 RTC register access
    if ((mbc_type >= 0x0F && mbc_type <= 0x13) && ram_bank >= 0x08 && ram_bank <= 0x0C) {
        // Reading RTC registers
        switch (ram_bank) {
            case 0x08: return rtc_seconds;
            case 0x09: return rtc_minutes;
            case 0x0A: return rtc_hours;
            case 0x0B: return rtc_days_low;
            case 0x0C: return rtc_days_high;
            default: return 0xFF;
        }
    }
    
    // MBC2: Only 512 x 4-bit RAM
    if (mbc_type == 0x05 || mbc_type == 0x06) {
        return ram[addr & 0x01FF] & 0x0F;  // Mask to 4 bits
    }
    
    // Normal RAM access for other MBCs
    u32 physical_addr = (ram_bank * 0x2000) + addr;
    
    if (physical_addr < ram.size()) {
        return ram[physical_addr];
    }
    return 0xFF;
}

void Cartridge::writeROM(u16 addr, u8 value) {
    // Writes to ROM area control the MBC
    
    switch (mbc_type) {
        case 0x00:  // ROM ONLY - no banking
            break;
            
        case 0x01:  // MBC1
        case 0x02:  // MBC1+RAM
        case 0x03:  // MBC1+RAM+BATTERY
            if (addr < 0x2000) {
                // RAM Enable (0x0000-0x1FFF)
                ram_enabled = (value & 0x0F) == 0x0A;
            }
            else if (addr < 0x4000) {
                // ROM Bank Number (0x2000-0x3FFF)
                int bank = value & 0x1F;
                if (bank == 0) bank = 1;
                rom_bank = (rom_bank & 0x60) | bank;
            }
            else if (addr < 0x6000) {
                // RAM Bank Number or Upper ROM Bank (0x4000-0x5FFF)
                if (rom_banking) {
                    rom_bank = (rom_bank & 0x1F) | ((value & 0x03) << 5);
                } else {
                    ram_bank = value & 0x03;
                }
            }
            else if (addr < 0x8000) {
                // ROM/RAM Mode Select (0x6000-0x7FFF)
                rom_banking = (value & 0x01) == 0;
                if (rom_banking) {
                    ram_bank = 0;
                }
            }
            break;
            
        case 0x05:  // MBC2
        case 0x06:  // MBC2+BATTERY
            if (addr < 0x4000) {
                // Check bit 8 of address to determine RAM enable vs ROM bank
                if ((addr & 0x0100) == 0) {
                    // RAM Enable (bit 8 = 0)
                    ram_enabled = (value & 0x0F) == 0x0A;
                } else {
                    // ROM Bank Select (bit 8 = 1)
                    // Only lower 4 bits used
                    int bank = value & 0x0F;
                    if (bank == 0) bank = 1;
                    rom_bank = bank;
                }
            }
            break;
            
        case 0x0F:  // MBC3+TIMER+BATTERY
        case 0x10:  // MBC3+TIMER+RAM+BATTERY
        case 0x11:  // MBC3
        case 0x12:  // MBC3+RAM
        case 0x13:  // MBC3+RAM+BATTERY
            if (addr < 0x2000) {
                // RAM/RTC Enable (0x0000-0x1FFF)
                ram_enabled = (value & 0x0F) == 0x0A;
            }
            else if (addr < 0x4000) {
                // ROM Bank Number (0x2000-0x3FFF)
                // 7 bits, bank 0 = bank 1
                int bank = value & 0x7F;
                if (bank == 0) bank = 1;
                rom_bank = bank;
            }
            else if (addr < 0x6000) {
                // RAM Bank (0x00-0x03) or RTC Register (0x08-0x0C)
                ram_bank = value;
            }
            else if (addr < 0x8000) {
                // Latch Clock Data (0x6000-0x7FFF)
                // Latches RTC when 0x00 is written, then 0x01
                if (rtc_latch_state == 0x00 && value == 0x01) {
                    rtc_latched = true;
                }
                rtc_latch_state = value;
            }
            break;
            
        case 0x19:  // MBC5
        case 0x1A:  // MBC5+RAM
        case 0x1B:  // MBC5+RAM+BATTERY
        case 0x1C:  // MBC5+RUMBLE
        case 0x1D:  // MBC5+RUMBLE+RAM
        case 0x1E:  // MBC5+RUMBLE+RAM+BATTERY
            if (addr < 0x2000) {
                // RAM Enable (0x0000-0x1FFF)
                ram_enabled = (value & 0x0F) == 0x0A;
            }
            else if (addr < 0x3000) {
                // ROM Bank Number - lower 8 bits (0x2000-0x2FFF)
                rom_bank = (rom_bank & 0x100) | value;
            }
            else if (addr < 0x4000) {
                // ROM Bank Number - 9th bit (0x3000-0x3FFF)
                rom_bank = (rom_bank & 0xFF) | ((value & 0x01) << 8);
            }
            else if (addr < 0x6000) {
                // RAM Bank Number (0x4000-0x5FFF)
                // 4 bits for RAM, bit 3 for rumble (ignored)
                ram_bank = value & 0x0F;
            }
            break;
            
        default:
            // Unknown MBC type - treat as ROM only
            break;
    }
}

void Cartridge::writeRAM(u16 addr, u8 value) {
    if (!ram_enabled) {
        return;  // RAM disabled
    }
    
    // MBC3 RTC register write
    if ((mbc_type >= 0x0F && mbc_type <= 0x13) && ram_bank >= 0x08 && ram_bank <= 0x0C) {
        // Writing to RTC registers
        switch (ram_bank) {
            case 0x08: rtc_seconds = value; break;
            case 0x09: rtc_minutes = value; break;
            case 0x0A: rtc_hours = value; break;
            case 0x0B: rtc_days_low = value; break;
            case 0x0C: rtc_days_high = value; break;
        }
        return;
    }
    
    if (ram.empty()) {
        return;  // No RAM present
    }
    
    // MBC2: Only 512 x 4-bit RAM
    if (mbc_type == 0x05 || mbc_type == 0x06) {
        ram[addr & 0x01FF] = value & 0x0F;  // Mask to 4 bits, 512 bytes
        return;
    }
    
    // Normal RAM write for other MBCs
    u32 physical_addr = (ram_bank * 0x2000) + addr;
    
    if (physical_addr < ram.size()) {
        ram[physical_addr] = value;
    }
}

// ============================================
// SAVE STATE
// ============================================

void Cartridge::saveState(std::ofstream& file) const {
    // Save cartridge RAM
    u32 ram_size = static_cast<u32>(ram.size());
    file.write(reinterpret_cast<const char*>(&ram_size), sizeof(ram_size));
    if (ram_size > 0) {
        file.write(reinterpret_cast<const char*>(ram.data()), ram_size);
    }
    
    // Save MBC state
    file.write(reinterpret_cast<const char*>(&rom_bank), sizeof(rom_bank));
    file.write(reinterpret_cast<const char*>(&ram_bank), sizeof(ram_bank));
    file.write(reinterpret_cast<const char*>(&ram_enabled), sizeof(ram_enabled));
    file.write(reinterpret_cast<const char*>(&rom_banking), sizeof(rom_banking));
    
    // Save MBC3 RTC state
    file.write(reinterpret_cast<const char*>(&rtc_seconds), sizeof(rtc_seconds));
    file.write(reinterpret_cast<const char*>(&rtc_minutes), sizeof(rtc_minutes));
    file.write(reinterpret_cast<const char*>(&rtc_hours), sizeof(rtc_hours));
    file.write(reinterpret_cast<const char*>(&rtc_days_low), sizeof(rtc_days_low));
    file.write(reinterpret_cast<const char*>(&rtc_days_high), sizeof(rtc_days_high));
    file.write(reinterpret_cast<const char*>(&rtc_latched), sizeof(rtc_latched));
    file.write(reinterpret_cast<const char*>(&rtc_latch_state), sizeof(rtc_latch_state));
}

void Cartridge::loadState(std::ifstream& file) {
    // Load cartridge RAM
    u32 ram_size = 0;
    file.read(reinterpret_cast<char*>(&ram_size), sizeof(ram_size));
    if (ram_size > 0 && ram_size <= ram.size()) {
        file.read(reinterpret_cast<char*>(ram.data()), ram_size);
    }
    
    // Load MBC state
    file.read(reinterpret_cast<char*>(&rom_bank), sizeof(rom_bank));
    file.read(reinterpret_cast<char*>(&ram_bank), sizeof(ram_bank));
    file.read(reinterpret_cast<char*>(&ram_enabled), sizeof(ram_enabled));
    file.read(reinterpret_cast<char*>(&rom_banking), sizeof(rom_banking));
    
    // Load MBC3 RTC state
    file.read(reinterpret_cast<char*>(&rtc_seconds), sizeof(rtc_seconds));
    file.read(reinterpret_cast<char*>(&rtc_minutes), sizeof(rtc_minutes));
    file.read(reinterpret_cast<char*>(&rtc_hours), sizeof(rtc_hours));
    file.read(reinterpret_cast<char*>(&rtc_days_low), sizeof(rtc_days_low));
    file.read(reinterpret_cast<char*>(&rtc_days_high), sizeof(rtc_days_high));
    file.read(reinterpret_cast<char*>(&rtc_latched), sizeof(rtc_latched));
    file.read(reinterpret_cast<char*>(&rtc_latch_state), sizeof(rtc_latch_state));
}
