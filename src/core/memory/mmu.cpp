#include "mmu.h"
#include <iostream>

MMU::MMU() : ie_register(0x00), joyp_select(0x00) {
    powerOn();
}

void MMU::powerOn() {
    wram.fill(0);
    vram.fill(0);
    hram.fill(0);
    oam.fill(0);
    io.fill(0);

    // Power-on state after the DMG boot ROM (Pan Docs: Power-Up Sequence)
    io[0x00] = 0xCF; // P1/JOYP
    io[0x05] = 0x00; // TIMA
    io[0x06] = 0x00; // TMA
    io[0x07] = 0xF8; // TAC (unused bits read 1)
    io[0x0F] = 0xE0 | 0x01; // IF (VBlank pending from boot, upper bits read 1)

    io[0x10] = 0x80; // NR10
    io[0x11] = 0xBF; // NR11
    io[0x12] = 0xF3; // NR12
    io[0x13] = 0xFF; // NR13 (write-only, reads 0xFF)
    io[0x14] = 0xBF; // NR14
    io[0x16] = 0x3F; // NR21
    io[0x17] = 0x00; // NR22
    io[0x18] = 0xFF; // NR23
    io[0x19] = 0xBF; // NR24
    io[0x1A] = 0x7F; // NR30
    io[0x1B] = 0xFF; // NR31
    io[0x1C] = 0x9F; // NR32
    io[0x1D] = 0xFF; // NR33
    io[0x1E] = 0xBF; // NR34
    io[0x20] = 0xFF; // NR41
    io[0x21] = 0x00; // NR42
    io[0x22] = 0x00; // NR43
    io[0x23] = 0xBF; // NR44
    io[0x24] = 0x77; // NR50
    io[0x25] = 0xF3; // NR51
    io[0x26] = 0xF1; // NR52

    io[0x40] = 0x91; // LCDC
    io[0x41] = 0x85; // STAT (bit 7 always 1)
    io[0x42] = 0x00; // SCY
    io[0x43] = 0x00; // SCX
    io[0x44] = 0x00; // LY
    io[0x45] = 0x00; // LYC
    io[0x47] = 0xFC; // BGP
    io[0x48] = 0x00; // OBP0
    io[0x49] = 0x00; // OBP1
    io[0x4A] = 0x00; // WY
    io[0x4B] = 0x00; // WX
}

void MMU::reset() {
    powerOn();
    timer.powerOn();
    apu.reset();
    joypad.reset();
}

void MMU::reloadCartridge() {
    if (cartridge) {
        cartridge = std::make_unique<Cartridge>(cartridge->getFilePath());
    }
}

void MMU::loadCartridge(const std::string& filepath) {
    cartridge = std::make_unique<Cartridge>(filepath);
    std::cout << "Loaded cartridge: " << cartridge->getTitle() << std::endl;
}

u8 MMU::read(u16 addr) const {
    if (addr < 0x8000) {
        // ROM (banks handled by the cartridge)
        return cartridge ? cartridge->readROM(addr) : 0xFF;
    }
    if (addr < 0xA000) {
        return vram[addr - 0x8000];
    }
    if (addr < 0xC000) {
        return cartridge ? cartridge->readRAM(addr & 0x1FFF) : 0xFF;
    }
    if (addr < 0xE000) {
        return wram[addr - 0xC000];
    }
    if (addr < 0xFE00) {
        return wram[addr - 0xE000];  // Echo RAM
    }
    if (addr < 0xFEA0) {
        return oam[addr - 0xFE00];
    }
    if (addr < 0xFF00) {
        return 0xFF;  // unusable region
    }
    if (addr < 0xFF80) {
        u8 off = static_cast<u8>(addr - 0xFF00);
        switch (off) {
            case 0x00: return joypad.read();
            case 0x01: return io[0x01];               // SB
            case 0x02: return (io[0x02] & 0x83) | 0x7C; // SC (unused bits read 1)
            case 0x04: return timer.readDIV();
            case 0x05: return timer.readTIMA();
            case 0x06: return timer.readTMA();
            case 0x07: return timer.readTAC();
            case 0x0F: return static_cast<u8>(io[0x0F] | 0xE0); // IF upper bits read 1
            default:
                if (off >= 0x10 && off <= 0x3F) {
                    return apu.read(off);
                }
                return io[off];
        }
    }
    if (addr < 0xFFFF) {
        return hram[addr - 0xFF80];
    }
    return ie_register;
}

void MMU::write(u16 addr, u8 value) {
    if (addr < 0x8000) {
        if (cartridge) cartridge->writeROM(addr, value);
        return;
    }
    if (addr < 0xA000) {
        vram[addr - 0x8000] = value;
        return;
    }
    if (addr < 0xC000) {
        if (cartridge) cartridge->writeRAM(addr & 0x1FFF, value);
        return;
    }
    if (addr < 0xE000) {
        wram[addr - 0xC000] = value;
        return;
    }
    if (addr < 0xFE00) {
        wram[addr - 0xE000] = value;  // Echo RAM
        return;
    }
    if (addr < 0xFEA0) {
        oam[addr - 0xFE00] = value;
        return;
    }
    if (addr < 0xFF00) {
        return;  // unusable region: ignore writes
    }
    if (addr < 0xFF80) {
        u8 off = static_cast<u8>(addr - 0xFF00);
        switch (off) {
            case 0x00:
                joypad.write(value);
                joyp_select = value & 0x30;
                io[0x00] = value;
                return;

            case 0x01: // SB
                io[0x01] = value;
                return;

            case 0x02: // SC
                io[0x02] = value;
                if (value & 0x80) {
                    // Transfer requested: emulate an instant transfer with
                    // no peer (shifts in 0xFFs). Deliver the byte via the
                    // serial callback; blargg test ROMs print this way.
                    char c = static_cast<char>(io[0x01]);
                    serial_output += c;
                    if (serial_callback) serial_callback(c);
                    io[0x02] = static_cast<u8>(value & 0x7F); // transfer done
                    io[0x01] = 0xFF;                           // shifted in bits
                    requestInterrupt(0x08);                     // serial IRQ
                }
                return;

            case 0x04: // DIV - write resets the whole system counter
                timer.resetDIV(*this);
                return;

            case 0x05: timer.writeTIMA(value); return;
            case 0x06: timer.writeTMA(value); return;
            case 0x07: timer.writeTAC(value, *this); return;

            case 0x0F: // IF - only the low 5 bits are writable
                io[0x0F] = value & 0x1F;
                return;

            case 0x41: // STAT - bits 3-6 writable, rest read-only
                io[0x41] = static_cast<u8>((io[0x41] & 0x87) | (value & 0x78));
                return;

            case 0x44: // LY - read-only
                return;

            case 0x46: // OAM DMA
                doDMATransfer(value);
                io[0x46] = value;
                return;

            default:
                if (off >= 0x10 && off <= 0x3F) {
                    apu.write(off, value);
                    return;
                }
                io[off] = value;
                return;
        }
    }
    if (addr < 0xFFFF) {
        hram[addr - 0xFF80] = value;
        return;
    }
    ie_register = value;
}

void MMU::doDMATransfer(u8 value) {
    // Copies 160 bytes from value*0x100 into OAM. Real hardware takes
    // 160 M-cycles and locks CPU access to OAM during it; the immediate
    // copy is a well-established simplification.
    u16 source = static_cast<u16>(value) << 8;
    for (int i = 0; i < 0xA0; i++) {
        oam[i] = read(static_cast<u16>(source + i));
    }
}

// ============================================
// SAVE STATE
// ============================================

void MMU::saveState(std::ofstream& file) const {
    file.write(reinterpret_cast<const char*>(wram.data()), wram.size());
    file.write(reinterpret_cast<const char*>(vram.data()), vram.size());
    file.write(reinterpret_cast<const char*>(hram.data()), hram.size());
    file.write(reinterpret_cast<const char*>(oam.data()), oam.size());
    file.write(reinterpret_cast<const char*>(io.data()), io.size());
    file.write(reinterpret_cast<const char*>(&ie_register), sizeof(ie_register));
    file.write(reinterpret_cast<const char*>(&joyp_select), sizeof(joyp_select));
    timer.saveState(file);
    if (cartridge) cartridge->saveState(file);
}

void MMU::loadState(std::ifstream& file) {
    file.read(reinterpret_cast<char*>(wram.data()), wram.size());
    file.read(reinterpret_cast<char*>(vram.data()), vram.size());
    file.read(reinterpret_cast<char*>(hram.data()), hram.size());
    file.read(reinterpret_cast<char*>(oam.data()), oam.size());
    file.read(reinterpret_cast<char*>(io.data()), io.size());
    file.read(reinterpret_cast<char*>(&ie_register), sizeof(ie_register));
    file.read(reinterpret_cast<char*>(&joyp_select), sizeof(joyp_select));
    timer.loadState(file);
    if (cartridge) cartridge->loadState(file);
}
