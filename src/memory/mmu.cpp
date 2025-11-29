#include "mmu.h"
#include <iostream>

/**
 * MMU Implementation
 * GameBoy Memory Map:
 * 0x0000-0x3FFF: ROM Bank 0
 * 0x4000-0x7FFF: ROM Bank 1-N (switchable)
 * 0x8000-0x9FFF: VRAM
 * 0xA000-0xBFFF: External RAM
 * 0xC000-0xDFFF: Work RAM
 * 0xE000-0xFDFF: Echo RAM (mirror of C000-DDFF)
 * 0xFE00-0xFE9F: OAM
 * 0xFEA0-0xFEFF: Unusable
 * 0xFF00-0xFF7F: I/O Registers
 * 0xFF80-0xFFFE: HRAM
 * 0xFFFF: IE Register
 */

MMU::MMU() : ie_register(0) {
    // Initialize all memory to 0
    wram.fill(0);
    vram.fill(0);
    hram.fill(0);
    oam.fill(0);
    io.fill(0);
    
    // Initialize I/O registers to power-on state
    // These values match the state after the GameBoy BIOS runs
    io[0x00] = 0xCF; // P1/JOYP - Joypad
    io[0x05] = 0x00; // TIMA - Timer counter
    io[0x06] = 0x00; // TMA - Timer modulo
    io[0x07] = 0x00; // TAC - Timer control
    io[0x0F] = 0xE0; // IF - Interrupt flag
    
    // Sound registers
    io[0x10] = 0x80; // NR10
    io[0x11] = 0xBF; // NR11
    io[0x12] = 0xF3; // NR12
    io[0x14] = 0xBF; // NR14
    io[0x16] = 0x3F; // NR21
    io[0x17] = 0x00; // NR22
    io[0x19] = 0xBF; // NR24
    io[0x1A] = 0x7F; // NR30
    io[0x1B] = 0xFF; // NR31
    io[0x1C] = 0x9F; // NR32
    io[0x1E] = 0xBF; // NR33
    io[0x20] = 0xFF; // NR41
    io[0x21] = 0x00; // NR42
    io[0x22] = 0x00; // NR43
    io[0x23] = 0xBF; // NR30
    io[0x24] = 0x77; // NR50
    io[0x25] = 0xF3; // NR51
    io[0x26] = 0xF1; // NR52
    
    // LCD registers
    io[0x40] = 0x91; // LCDC - LCD control
    io[0x41] = 0x00; // STAT - LCD status
    io[0x42] = 0x00; // SCY - Scroll Y
    io[0x43] = 0x00; // SCX - Scroll X
    io[0x44] = 0x00; // LY - LCD Y coordinate
    io[0x45] = 0x00; // LYC - LY compare
    io[0x47] = 0xFC; // BGP - BG palette
    io[0x48] = 0xFF; // OBP0 - Object palette 0
    io[0x49] = 0xFF; // OBP1 - Object palette 1
    io[0x4A] = 0x00; // WY - Window Y
    io[0x4B] = 0x00; // WX - Window X
}

void MMU::loadCartridge(const std::string& filepath) {
    cartridge = std::make_unique<Cartridge>(filepath);
    std::cout << "Loaded cartridge: " << cartridge->getTitle() << std::endl;
}

u8 MMU::read(u16 addr) const {
    // ROM Bank 0 (0x0000-0x3FFF)
    if (addr < 0x4000) {
        return cartridge ? cartridge->readROM(addr) : 0xFF;
    }
    // ROM Bank 1-N (0x4000-0x7FFF)
    else if (addr < 0x8000) {
        return cartridge ? cartridge->readROM(addr) : 0xFF;
    }
    // VRAM (0x8000-0x9FFF)
    else if (addr < 0xA000) {
        return vram[addr - 0x8000];
    }
    // External RAM (0xA000-0xBFFF)
    else if (addr < 0xC000) {
        return cartridge ? cartridge->readRAM(addr - 0xA000) : 0xFF;
    }
    // Work RAM (0xC000-0xDFFF)
    else if (addr < 0xE000) {
        return wram[addr - 0xC000];
    }
    // Echo RAM (0xE000-0xFDFF) - mirror of WRAM
    else if (addr < 0xFE00) {
        return wram[addr - 0xE000];
    }
    // OAM - Object Attribute Memory (0xFE00-0xFE9F)
    else if (addr < 0xFEA0) {
        return oam[addr - 0xFE00];
    }
    // Unusable memory (0xFEA0-0xFEFF)
    else if (addr < 0xFF00) {
        return 0xFF;
    }
    // I/O Registers (0xFF00-0xFF7F)
    else if (addr < 0xFF80) {
        // Special handling for joypad register
        if (addr == 0xFF00) {
            return joypad.read(io[0x00]);
        }
        return io[addr - 0xFF00];
    }
    // High RAM (0xFF80-0xFFFE)
    else if (addr < 0xFFFF) {
        return hram[addr - 0xFF80];
    }
    // Interrupt Enable Register (0xFFFF)
    else {
        return ie_register;
    }
}

void MMU::write(u16 addr, u8 value) {
    // ROM (0x0000-0x7FFF) - writes go to MBC controller
    if (addr < 0x8000) {
        if (cartridge) {
            cartridge->writeROM(addr, value);
        }
    }
    // VRAM (0x8000-0x9FFF)
    else if (addr < 0xA000) {
        vram[addr - 0x8000] = value;
    }
    // External RAM (0xA000-0xBFFF)
    else if (addr < 0xC000) {
        if (cartridge) {
            cartridge->writeRAM(addr - 0xA000, value);
        }
    }
    // Work RAM (0xC000-0xDFFF)
    else if (addr < 0xE000) {
        wram[addr - 0xC000] = value;
    }
    // Echo RAM (0xE000-0xFDFF)
    else if (addr < 0xFE00) {
        wram[addr - 0xE000] = value;
    }
    // OAM (0xFE00-0xFE9F)
    else if (addr < 0xFEA0) {
        oam[addr - 0xFE00] = value;
    }
    // Unusable (0xFEA0-0xFEFF) - ignore writes
    else if (addr < 0xFF00) {
        // Do nothing
    }
    // I/O Registers (0xFF00-0xFF7F)
    else if (addr < 0xFF80) {
        // Special handling for some registers
        if (addr == 0xFF00) {
            // Joypad register - store select bits
            joypad.write(value);
            io[0x00] = value;
        }
        else if (addr == 0xFF04) {
            // DIV register - writing any value resets it to 0
            timer.resetDIV();
            io[0x04] = 0;
        }
        else if (addr >= 0xFF10 && addr <= 0xFF3F) {
            apu.write(addr, value);
        }
        else if (addr == 0xFF44) {
            // LY register - read-only, writing resets to 0
            io[0x44] = 0;
        }
        else if (addr == 0xFF46) {
            // DMA transfer
            doDMATransfer(value);
            io[0x46] = value;
        }
        else {
            io[addr - 0xFF00] = value;
        }
    }
    // High RAM (0xFF80-0xFFFE)
    else if (addr < 0xFFFF) {
        hram[addr - 0xFF80] = value;
    }
    // Interrupt Enable Register (0xFFFF)
    else {
        ie_register = value;
    }
}

void MMU::doDMATransfer(u8 value) {
    // DMA transfers 160 bytes (0xA0) from source to OAM
    // Source address is value * 0x100 (so value of 0xC1 = 0xC100)
    u16 source = static_cast<u16>(value) << 8;
    
    // Copy 160 bytes to OAM (0xFE00-0xFE9F)
    for (int i = 0; i < 0xA0; i++) {
        oam[i] = read(source + i);
    }
    
    // DMA transfer takes 160 M-cycles (640 T-cycles)
    // This will be handled by the CPU cycle counter
}

// ============================================
// SAVE STATE
// ============================================

void MMU::saveState(std::ofstream& file) const {
    // Save all memory arrays
    file.write(reinterpret_cast<const char*>(wram.data()), wram.size());
    file.write(reinterpret_cast<const char*>(vram.data()), vram.size());
    file.write(reinterpret_cast<const char*>(hram.data()), hram.size());
    file.write(reinterpret_cast<const char*>(oam.data()), oam.size());
    file.write(reinterpret_cast<const char*>(io.data()), io.size());
    file.write(reinterpret_cast<const char*>(&ie_register), sizeof(ie_register));
    
    // Cartridge saves its own RAM and MBC state
    if (cartridge) {
        cartridge->saveState(file);
    }
}

void MMU::loadState(std::ifstream& file) {
    // Load all memory arrays
    file.read(reinterpret_cast<char*>(wram.data()), wram.size());
    file.read(reinterpret_cast<char*>(vram.data()), vram.size());
    file.read(reinterpret_cast<char*>(hram.data()), hram.size());
    file.read(reinterpret_cast<char*>(oam.data()), oam.size());
    file.read(reinterpret_cast<char*>(io.data()), io.size());
    file.read(reinterpret_cast<char*>(&ie_register), sizeof(ie_register));
    
    // Cartridge loads its own RAM and MBC state
    if (cartridge) {
        cartridge->loadState(file);
    }
}
