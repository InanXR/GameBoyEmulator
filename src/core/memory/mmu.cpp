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

    // Power-on state after the boot ROM (Pan Docs: Power-Up Sequence).
    io[0x00] = 0xCF; // P1/JOYP
    io[0x01] = 0x00; // SB
    io[0x02] = 0x7E; // SC (bit 1 = internal clock selected)
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
    io[0x46] = 0xFF; // DMA (CGB boots to 0x00, set below)
    io[0x47] = 0xFC; // BGP
    io[0x48] = 0x00; // OBP0 (uninitialized on hardware)
    io[0x49] = 0x00; // OBP1
    io[0x4A] = 0x00; // WY
    io[0x4B] = 0x00; // WX

    // CGB register power-on state. The CGB boot ROM initializes all
    // background palette colors to white; object palette RAM is
    // uninitialized on hardware (deterministic 0xFF here). Palette
    // RAM color 0xFFFF reads as white because bit 15 is ignored.
    vram_bank = 0;
    svbk_raw = 0;
    double_speed = false;
    speed_armed = false;
    opri = 0;
    rp = 0x00;
    bgp_index = 0;
    obp_index = 0;
    bg_palette.fill(0xFF);
    obj_palette.fill(0xFF);
    cram_locked = false;
    hdma1 = hdma2 = hdma3 = hdma4 = 0xFF;
    hdma_blocks = 0;
    hdma_active = false;
    hdma_dst_bank = 0;

    if (cgb) {
        io[0x02] = 0x7F; // SC (CGB boots with the fast-cable bit set)
        io[0x46] = 0x00; // DMA (CGB boot value)
        io[0x4C] = 0x00; // KEY0
        io[0x72] = 0x00; // undocumented FF72-FF75: 0 at power-on
        io[0x73] = 0x00;
        io[0x74] = 0x00;
        io[0x75] = 0x00;
    }
}

void MMU::reset() {
    powerOn();
    timer.powerOn();
    apu.reset();
    joypad.reset();
}

void MMU::setCGB(bool enable) {
    cgb = enable;
    powerOn();
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

// ============================================
// WRAM (CGB banked; echo region mirrors it)
// ============================================

u8 MMU::readWRAM(u16 addr) const {
    if (addr < 0xD000) {
        return wram[addr - 0xC000];  // bank 0 is always at C000-CFFF
    }
    u8 bank = cgb ? (svbk_raw ? svbk_raw : 1) : 1;
    return wram[static_cast<size_t>(bank) * 0x1000 + (addr - 0xD000)];
}

void MMU::writeWRAM(u16 addr, u8 value) {
    if (addr < 0xD000) {
        wram[addr - 0xC000] = value;
        return;
    }
    u8 bank = cgb ? (svbk_raw ? svbk_raw : 1) : 1;
    wram[static_cast<size_t>(bank) * 0x1000 + (addr - 0xD000)] = value;
}

// ============================================
// CGB PALETTE RAM (CRAM)
// ============================================

u16 MMU::bgPaletteColor(int palette, int color) const {
    size_t o = static_cast<size_t>(palette) * 8 + static_cast<size_t>(color) * 2;
    return static_cast<u16>(bg_palette[o] | (bg_palette[o + 1] << 8));
}

u16 MMU::objPaletteColor(int palette, int color) const {
    size_t o = static_cast<size_t>(palette) * 8 + static_cast<size_t>(color) * 2;
    return static_cast<u16>(obj_palette[o] | (obj_palette[o + 1] << 8));
}

u8 MMU::readPalette(bool bg) const {
    // CRAM is inaccessible while the PPU reads it (mode 3): reads
    // return $FF.
    if (cram_locked) return 0xFF;
    u8 index = (bg ? bgp_index : obp_index) & 0x3F;
    return (bg ? bg_palette : obj_palette)[index];
}

void MMU::writePalette(bool bg, u8 value) {
    u8& index_reg = bg ? bgp_index : obp_index;
    if (!cram_locked) {
        u8 index = index_reg & 0x3F;
        (bg ? bg_palette : obj_palette)[index] = value;
    }
    // Auto-increment runs after every write, even one blocked by the
    // CRAM lock; reads never auto-increment. Wraps 63 -> 0.
    if (index_reg & 0x80) {
        index_reg = static_cast<u8>(((index_reg + 1) & 0x3F) | 0x80);
    }
}

// ============================================
// CGB HDMA / GDMA (FF51-FF55)
// ============================================

void MMU::startHDMA(u8 value) {
    if (hdma_active && !(value & 0x80)) {
        // Writing 0 to bit 7 of FF55 cancels a running HBlank DMA.
        // The remaining length stays readable (bit 7 then reads 1).
        hdma_active = false;
        return;
    }

    hdma_blocks = static_cast<u16>((value & 0x7F) + 1);
    hdma_dst_bank = vram_bank;

    if (value & 0x80) {
        // HBlank DMA: 16 bytes move during each HBlank of lines 0-143;
        // nothing moves during VBlank. The CPU is only stalled during
        // the block moves (approximated here by instantaneous copies;
        // see docs/03-HARDWARE-NOTES.md).
        hdma_active = true;
    } else {
        // General-purpose DMA: the whole transfer happens at once and
        // the CPU is stalled until it completes, which an atomic copy
        // models from the program's point of view.
        hdma_active = false;
        while (hdma_blocks > 0) {
            if (!hdmaCopyBlock()) break;
        }
    }
}

bool MMU::hdmaCopyBlock() {
    u16 src = static_cast<u16>((hdma1 << 8) | hdma2);          // 0000-DFF0, low nibble 0
    u16 dst = static_cast<u16>(0x8000 | (hdma3 << 8) | hdma4); // 8000-9FF0, low nibble 0

    for (int i = 0; i < 16; i++) {
        vram[static_cast<size_t>(hdma_dst_bank) * 0x2000 + (dst - 0x8000) + i] = read(src + i);
    }

    // The registers track the transfer as it advances.
    src = static_cast<u16>(src + 16);
    dst = static_cast<u16>(dst + 16);
    hdma1 = static_cast<u8>(src >> 8);
    hdma2 = static_cast<u8>(src & 0xF0);
    hdma3 = static_cast<u8>((dst >> 8) & 0x1F);
    hdma4 = static_cast<u8>(dst & 0xF0);
    hdma_blocks--;

    // A destination overflow stops the transfer prematurely.
    return dst <= 0x9FF0;
}

void MMU::hblankHDMA() {
    if (!hdma_active) return;
    if (!hdmaCopyBlock()) {
        hdma_active = false;
        return;
    }
    if (hdma_blocks == 0) hdma_active = false;
}

// ============================================
// MEMORY READ
// ============================================

u8 MMU::read(u16 addr) const {
    if (addr < 0x8000) {
        // ROM (banks handled by the cartridge)
        return cartridge ? cartridge->readROM(addr) : 0xFF;
    }
    if (addr < 0xA000) {
        return vram[static_cast<size_t>(vram_bank) * 0x2000 + (addr - 0x8000)];
    }
    if (addr < 0xC000) {
        return cartridge ? cartridge->readRAM(addr & 0x1FFF) : 0xFF;
    }
    if (addr < 0xE000) {
        return readWRAM(addr);
    }
    if (addr < 0xFE00) {
        return readWRAM(static_cast<u16>(addr - 0x2000));  // Echo RAM
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
            case 0x01: return io[0x01];                 // SB
            case 0x02: return static_cast<u8>((io[0x02] & 0x83) | 0x7C); // SC
            case 0x04: return timer.readDIV();
            case 0x05: return timer.readTIMA();
            case 0x06: return timer.readTMA();
            case 0x07: return timer.readTAC();
            case 0x0F: return static_cast<u8>(io[0x0F] | 0xE0); // IF upper bits read 1

            // CGB registers. In DMG mode these addresses are unused
            // I/O space and read $FF.
            case 0x4C: return cgb ? io[0x4C] : 0xFF;    // KEY0
            case 0x4D:  // KEY1: current speed | armed | unused bits 1
                if (!cgb) return 0xFF;
                return static_cast<u8>(((double_speed ? 0x80 : 0x00) |
                                         (speed_armed ? 0x01 : 0x00)) | 0x7E);
            case 0x4F: return cgb ? static_cast<u8>(0xFE | vram_bank) : 0xFF; // VBK
            case 0x51: case 0x52: case 0x53: case 0x54:
                return 0xFF;                            // HDMA1-4 (write-only)
            case 0x55: return hdma5Read();              // HDMA5
            case 0x56:  // RP: no IR hardware, so "receiving" reads 1
                return cgb ? static_cast<u8>((rp & 0xC0) | 0x3E | (rp & 0x01)) : 0xFF;
            case 0x68: return cgb ? bgp_index : 0xFF;   // BGPI
            case 0x69: return cgb ? readPalette(true) : 0xFF;   // BGPD
            case 0x6A: return cgb ? obp_index : 0xFF;   // OBPI
            case 0x6B: return cgb ? readPalette(false) : 0xFF;   // OBPD
            case 0x6C: return cgb ? opri : 0xFF;        // OPRI
            case 0x70: return cgb ? static_cast<u8>(0xF8 | svbk_raw) : 0xFF; // SVBK
            case 0x72: case 0x73: case 0x74: case 0x75:
                return cgb ? io[off] : 0xFF;           // undocumented
            case 0x76: return cgb ? apu.pcm12() : 0xFF; // PCM12
            case 0x77: return cgb ? apu.pcm34() : 0xFF; // PCM34

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

// ============================================
// MEMORY WRITE
// ============================================

void MMU::write(u16 addr, u8 value) {
    if (addr < 0x8000) {
        if (cartridge) cartridge->writeROM(addr, value);
        return;
    }
    if (addr < 0xA000) {
        vram[static_cast<size_t>(vram_bank) * 0x2000 + (addr - 0x8000)] = value;
        return;
    }
    if (addr < 0xC000) {
        if (cartridge) cartridge->writeRAM(addr & 0x1FFF, value);
        return;
    }
    if (addr < 0xE000) {
        writeWRAM(addr, value);
        return;
    }
    if (addr < 0xFE00) {
        writeWRAM(static_cast<u16>(addr - 0x2000), value);  // Echo RAM
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
                joyp_select = static_cast<u8>(value & 0x30);
                io[0x00] = value;
                return;

            case 0x01: // SB
                io[0x01] = value;
                return;

            case 0x02: { // SC
                // SC bit 1 (CGB-only clock speed) and bits 7/0.
                // Bit 0 is the clock select on both models: 1 = the
                // internal ("master") clock, 0 = external, which never
                // completes without a peer attached.
                io[0x02] = cgb ? static_cast<u8>(value & 0x83)
                               : static_cast<u8>(value & 0x81);
                if ((value & 0x80) && (value & 0x01)) {
                    char c = static_cast<char>(io[0x01]);
                    serial_output += c;
                    if (serial_callback) serial_callback(c);
                    io[0x02] = static_cast<u8>(value & 0x7F); // transfer done
                    io[0x01] = 0xFF;                          // shifted in bits
                    requestInterrupt(0x08);                   // serial IRQ
                }
                return;
            }

            case 0x04: // DIV - write resets the whole system counter
                timer.resetDIV(*this);
                return;

            case 0x05: timer.writeTIMA(value); return;
            case 0x06: timer.writeTMA(value); return;
            case 0x07: timer.writeTAC(value, *this, !cgb); return;

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

            // CGB registers. In DMG mode the writes are ignored.
            case 0x4C: if (cgb) io[0x4C] = value; return;      // KEY0
            case 0x4D: if (cgb) speed_armed = (value & 0x01) != 0; return; // KEY1
            case 0x4F: if (cgb) vram_bank = value & 0x01; return; // VBK
            case 0x51: if (cgb) hdma1 = value; return;         // HDMA1
            case 0x52: if (cgb) hdma2 = static_cast<u8>(value & 0xF0); return; // HDMA2
            case 0x53: if (cgb) hdma3 = static_cast<u8>(value & 0x1F); return; // HDMA3
            case 0x54: if (cgb) hdma4 = static_cast<u8>(value & 0xF0); return; // HDMA4
            case 0x55: if (cgb) startHDMA(value); return;      // HDMA5
            case 0x56: if (cgb) rp = static_cast<u8>(value & 0xC1); return; // RP
            case 0x68: if (cgb) bgp_index = value; return;      // BGPI
            case 0x69: if (cgb) writePalette(true, value); return;  // BGPD
            case 0x6A: if (cgb) obp_index = value; return;      // OBPI
            case 0x6B: if (cgb) writePalette(false, value); return; // OBPD
            case 0x6C: if (cgb) opri = static_cast<u8>(value & 0x01); return; // OPRI
            case 0x70: if (cgb) svbk_raw = static_cast<u8>(value & 0x07); return; // SVBK
            case 0x76: case 0x77: return; // PCM12/PCM34: read-only

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

    // CGB state (hardware mode, banks, palette RAM, HDMA engine)
    u8 mode = static_cast<u8>((cgb ? 0x01 : 0) | (double_speed ? 0x02 : 0) |
                              (speed_armed ? 0x04 : 0) | (hdma_active ? 0x08 : 0) |
                              (cram_locked ? 0x10 : 0));
    file.write(reinterpret_cast<const char*>(&mode), sizeof(mode));
    file.write(reinterpret_cast<const char*>(&vram_bank), sizeof(vram_bank));
    file.write(reinterpret_cast<const char*>(&svbk_raw), sizeof(svbk_raw));
    file.write(reinterpret_cast<const char*>(&opri), sizeof(opri));
    file.write(reinterpret_cast<const char*>(&rp), sizeof(rp));
    file.write(reinterpret_cast<const char*>(&bgp_index), sizeof(bgp_index));
    file.write(reinterpret_cast<const char*>(&obp_index), sizeof(obp_index));
    file.write(reinterpret_cast<const char*>(bg_palette.data()), bg_palette.size());
    file.write(reinterpret_cast<const char*>(obj_palette.data()), obj_palette.size());
    file.write(reinterpret_cast<const char*>(&hdma1), sizeof(hdma1));
    file.write(reinterpret_cast<const char*>(&hdma2), sizeof(hdma2));
    file.write(reinterpret_cast<const char*>(&hdma3), sizeof(hdma3));
    file.write(reinterpret_cast<const char*>(&hdma4), sizeof(hdma4));
    file.write(reinterpret_cast<const char*>(&hdma_blocks), sizeof(hdma_blocks));
    file.write(reinterpret_cast<const char*>(&hdma_dst_bank), sizeof(hdma_dst_bank));

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

    u8 mode = 0;
    file.read(reinterpret_cast<char*>(&mode), sizeof(mode));
    cgb = (mode & 0x01) != 0;
    double_speed = (mode & 0x02) != 0;
    speed_armed = (mode & 0x04) != 0;
    hdma_active = (mode & 0x08) != 0;
    cram_locked = (mode & 0x10) != 0;
    file.read(reinterpret_cast<char*>(&vram_bank), sizeof(vram_bank));
    file.read(reinterpret_cast<char*>(&svbk_raw), sizeof(svbk_raw));
    file.read(reinterpret_cast<char*>(&opri), sizeof(opri));
    file.read(reinterpret_cast<char*>(&rp), sizeof(rp));
    file.read(reinterpret_cast<char*>(&bgp_index), sizeof(bgp_index));
    file.read(reinterpret_cast<char*>(&obp_index), sizeof(obp_index));
    file.read(reinterpret_cast<char*>(bg_palette.data()), bg_palette.size());
    file.read(reinterpret_cast<char*>(obj_palette.data()), obj_palette.size());
    file.read(reinterpret_cast<char*>(&hdma1), sizeof(hdma1));
    file.read(reinterpret_cast<char*>(&hdma2), sizeof(hdma2));
    file.read(reinterpret_cast<char*>(&hdma3), sizeof(hdma3));
    file.read(reinterpret_cast<char*>(&hdma4), sizeof(hdma4));
    file.read(reinterpret_cast<char*>(&hdma_blocks), sizeof(hdma_blocks));
    file.read(reinterpret_cast<char*>(&hdma_dst_bank), sizeof(hdma_dst_bank));

    if (cartridge) cartridge->loadState(file);
}
