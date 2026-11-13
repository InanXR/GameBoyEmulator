#include "cartridge.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

/**
 * Cartridge Implementation
 *
 * MBC handling follows Pan Docs:
 *  https://gbdev.io/pandocs/MBC1.html
 *  https://gbdev.io/pandocs/MBC2.html
 *  https://gbdev.io/pandocs/MBC3.html
 *  https://gbdev.io/pandocs/MBC5.html
 */

Cartridge::Cartridge(const std::string& path)
    : filepath(path)
    , cart_type(0)
    , mbc(MBC::NONE)
    , battery(false)
    , rtc_present(false)
    , rom_bank(1)
    , bank2(0)
    , ram_bank(0)
    , ram_enabled(false)
    , mode(false)
    , rtc_latch_state(0)
    , rtc_cycles(0)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open ROM file: " + path);
    }

    size_t size = static_cast<size_t>(file.tellg());
    if (size < 0x0150) {
        throw std::runtime_error("ROM file too small to be a valid GameBoy ROM: " + path);
    }

    rom.resize(size);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(rom.data()), size);

    parseHeader();

    std::cout << "ROM loaded successfully:" << std::endl;
    std::cout << "  Size: " << (size / 1024) << " KB" << std::endl;
    std::cout << "  Title: " << getTitle() << std::endl;

    if (battery) {
        loadBattery();
    }
}

void Cartridge::parseHeader() {
    cart_type = rom[0x0147];

    switch (cart_type) {
        case 0x00: mbc = MBC::NONE; break;
        case 0x01: case 0x02: case 0x03: mbc = MBC::MBC1; break;
        case 0x05: case 0x06:           mbc = MBC::MBC2; break;
        case 0x0F: case 0x10: case 0x11:
        case 0x12: case 0x13:           mbc = MBC::MBC3; break;
        case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: mbc = MBC::MBC5; break;
        default:
            std::cerr << "Warning: unknown cartridge type 0x" << std::hex
                      << static_cast<int>(cart_type) << std::dec
                      << ", treating as ROM only" << std::endl;
            mbc = MBC::NONE;
            break;
    }

    switch (cart_type) {
        case 0x03: case 0x06: case 0x09: case 0x0F: case 0x10:
        case 0x13: case 0x1B: case 0x1E:
            battery = true;
            break;
        default:
            battery = false;
            break;
    }

    rtc_present = (cart_type == 0x0F || cart_type == 0x10);

    // External RAM size from header 0x0149.
    size_t ram_size = 0;
    switch (rom[0x0149]) {
        case 0x00: ram_size = 0; break;
        case 0x01: ram_size = 2 * 1024; break;
        case 0x02: ram_size = 8 * 1024; break;
        case 0x03: ram_size = 32 * 1024; break;
        case 0x04: ram_size = 128 * 1024; break;
        case 0x05: ram_size = 64 * 1024; break;
        default:   ram_size = 32 * 1024; break;
    }

    // MBC2 always has 512 x 4-bit built-in RAM regardless of the header.
    if (mbc == MBC::MBC2) {
        ram_size = 512;
    }

    if (ram_size > 0) {
        ram.assign(ram_size, 0);
        std::cout << "  RAM: " << (ram_size / 1024) << " KB" << std::endl;
    } else {
        std::cout << "  RAM: None" << std::endl;
    }

    std::cout << "  MBC Type: 0x" << std::hex << static_cast<int>(cart_type) << std::dec << " (";
    switch (mbc) {
        case MBC::NONE: std::cout << "ROM ONLY"; break;
        case MBC::MBC1: std::cout << "MBC1"; break;
        case MBC::MBC2: std::cout << "MBC2"; break;
        case MBC::MBC3: std::cout << "MBC3" << (rtc_present ? " + RTC" : ""); break;
        case MBC::MBC5: std::cout << "MBC5"; break;
    }
    std::cout << ")" << (battery ? " + BATTERY" : "") << std::endl;

    for (int i = 0; i < 5; i++) {
        rtc_regs[i] = 0;
        rtc_latched[i] = 0;
    }
}

std::string Cartridge::getTitle() const {
    // Title occupies 0x0134-0x0143 (0x0134-0x0143 on DMG, CGB flag may
    // overlap at 0x143; keep it simple and stop at NUL or non-printable).
    std::string title;
    for (int i = 0x0134; i <= 0x0143; i++) {
        if (rom[i] == 0) break;
        if (rom[i] < 0x20 || rom[i] > 0x7E) break;
        title += static_cast<char>(rom[i]);
    }
    return title;
}

// ============================================
// MBC1 ADDRESSING
// ============================================

u32 Cartridge::romOffsetLow() const {
    // 0x0000-0x3FFF: bank 0 in mode 0; bank (bank2 << 5) in mode 1.
    u32 bank = mode ? static_cast<u32>(bank2) << 5 : 0;
    return bank * 0x4000;
}

u32 Cartridge::romOffsetHigh() const {
    // 0x4000-0x7FFF: (bank2 << 5) | lo5 in both modes; the 5-bit
    // register value 0 is translated to 1 (the 00->01 translation only
    // looks at the low 5-bit register, per Pan Docs).
    u32 lo = static_cast<u32>(rom_bank) & 0x1F;
    if (lo == 0) lo = 1;
    u32 bank = (static_cast<u32>(bank2) << 5) | lo;
    if (romBanks() > 0) bank %= romBanks();
    return bank * 0x4000;
}

int Cartridge::effectiveRAMBank() const {
    // MBC1: the 2-bit secondary register banks RAM, but only in mode 1;
    // mode 0 locks RAM to bank 0 (and the register instead supplies ROM
    // bank bits 5-6).
    if (mbc == MBC::MBC1) {
        if (!mode) return 0;
        int bank = bank2;
        if (ramBanks() > 0) bank %= static_cast<int>(ramBanks());
        return bank;
    }
    int bank = ram_bank;
    if (ramBanks() > 0) bank %= static_cast<int>(ramBanks());
    return bank;
}

// ============================================
// ROM / RAM ACCESS
// ============================================

u8 Cartridge::readROM(u16 addr) const {
    u32 offset;
    if (addr < 0x4000) {
        offset = romOffsetLow() + addr;
    } else {
        offset = romOffsetHigh() + (addr - 0x4000);
    }
    if (offset < rom.size()) return rom[offset];
    return 0xFF;
}

u8 Cartridge::readRAM(u16 addr) const {
    if (!ram_enabled) return 0xFF;

    // MBC3 RTC registers (bank select 0x08-0x0C) read the LATCHED copies.
    if (rtc_present && ram_bank >= 0x08 && ram_bank <= 0x0C) {
        return rtc_latched[ram_bank - 0x08];
    }

    if (ram.empty()) return 0xFF;

    if (mbc == MBC::MBC2) {
        return ram[addr & 0x01FF] & 0x0F;  // 512 x 4-bit
    }

    u32 offset = static_cast<u32>(effectiveRAMBank()) * 0x2000 + (addr & 0x1FFF);
    if (offset < ram.size()) return ram[offset];
    return 0xFF;
}

void Cartridge::writeRAM(u16 addr, u8 value) {
    if (!ram_enabled) return;

    // MBC3 RTC register writes (only meaningful while the clock is
    // halted on hardware, but accepted regardless).
    if (rtc_present && ram_bank >= 0x08 && ram_bank <= 0x0C) {
        // Writes go to the live registers; a subsequent latch copies
        // them out to the latched set.
        rtc_regs[ram_bank - 0x08] = value;
        return;
    }

    if (ram.empty()) return;

    if (mbc == MBC::MBC2) {
        ram[addr & 0x01FF] = value & 0x0F;
        return;
    }

    u32 offset = static_cast<u32>(effectiveRAMBank()) * 0x2000 + (addr & 0x1FFF);
    if (offset < ram.size()) ram[offset] = value;
}

// ============================================
// MBC CONTROL REGISTER WRITES
// ============================================

void Cartridge::writeROM(u16 addr, u8 value) {
    switch (mbc) {
        case MBC::NONE:
            break;

        case MBC::MBC1:
            if (addr < 0x2000) {
                // 0000-1FFF: RAM enable (0x0A in the low nibble enables)
                ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                // 2000-3FFF: 5-bit ROM bank number
                rom_bank = value & 0x1F;
            } else if (addr < 0x6000) {
                // 4000-5FFF: 2-bit secondary register. In mode 0 it
                // supplies ROM bank bits 5-6 (1 MiB+ carts); in mode 1
                // it banks RAM (and the 0000-3FFF ROM region).
                bank2 = value & 0x03;
            } else {
                // 6000-7FFF: mode select
                mode = (value & 0x01) != 0;
                if (!mode) ram_bank = 0;  // mode 0 locks RAM to bank 0
            }
            break;

        case MBC::MBC2:
            if (addr < 0x4000) {
                if (addr & 0x0100) {
                    // bit 8 set: ROM bank select (4 bits)
                    rom_bank = (value & 0x0F) == 0 ? 1 : (value & 0x0F);
                } else {
                    // bit 8 clear: RAM enable
                    ram_enabled = (value & 0x0F) == 0x0A;
                }
            }
            break;

        case MBC::MBC3:
            if (addr < 0x2000) {
                ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                // 7-bit ROM bank; 0 translated to 1. Banks 0x20/0x40/0x60
                // are directly reachable on MBC3 (unlike MBC1).
                int bank = value & 0x7F;
                if (bank == 0) bank = 1;
                rom_bank = bank;
            } else if (addr < 0x6000) {
                // RAM bank 0-3, or RTC register select 0x08-0x0C
                ram_bank = value & 0x0F;
            } else {
                // 6000-7FFF: latch clock data on 0x00 -> 0x01 transition
                if (rtc_latch_state == 0x00 && (value & 0x01) == 0x01) {
                    for (int i = 0; i < 5; i++) rtc_latched[i] = rtc_regs[i];
                }
                rtc_latch_state = value;
            }
            break;

        case MBC::MBC5:
            if (addr < 0x2000) {
                ram_enabled = (value & 0x0F) == 0x0A;
            } else if (addr < 0x3000) {
                // 2000-2FFF: low 8 bits of ROM bank (no 0->1 translation!)
                rom_bank = (rom_bank & 0x100) | value;
            } else if (addr < 0x4000) {
                // 3000-3FFF: 9th ROM bank bit
                rom_bank = (rom_bank & 0xFF) | ((value & 0x01) << 8);
            } else {
                // 4000-5FFF: RAM bank (bit 3 is the rumble motor on
                // rumble carts and must not participate in addressing)
                bool rumble = (cart_type >= 0x1C && cart_type <= 0x1E);
                ram_bank = rumble ? (value & 0x07) : (value & 0x0F);
            }
            break;
    }
}

// ============================================
// RTC (MBC3)
// ============================================

void Cartridge::tick(int m_cycles) {
    if (!rtc_present) return;
    tickRTC(m_cycles * 4);  // T-cycles
}

void Cartridge::tickRTC(int t_cycles) {
    // Bit 6 of the days-high register halts the clock when set.
    if (rtc_regs[4] & 0x40) return;

    rtc_cycles += static_cast<u32>(t_cycles);
    while (rtc_cycles >= 32768) {  // one RTC second
        rtc_cycles -= 32768;

        int sec = rtc_regs[0] + 1;
        if (sec >= 60) {
            rtc_regs[0] = 0;
            int min = rtc_regs[1] + 1;
            if (min >= 60) {
                rtc_regs[1] = 0;
                int hr = rtc_regs[2] + 1;
                if (hr >= 24) {
                    rtc_regs[2] = 0;
                    int days = (rtc_regs[4] & 0x01) * 256 + rtc_regs[3] + 1;
                    if (days >= 512) {
                        // 512-day rollover: set the carry flag
                        rtc_regs[4] |= 0x80;
                        days = 0;
                    }
                    rtc_regs[3] = days & 0xFF;
                    rtc_regs[4] = (rtc_regs[4] & 0xFE) | ((days >> 8) & 0x01);
                } else {
                    rtc_regs[2] = static_cast<u8>(hr);
                }
            } else {
                rtc_regs[1] = static_cast<u8>(min);
            }
        } else {
            rtc_regs[0] = static_cast<u8>(sec);
        }
    }
}

// ============================================
// BATTERY PERSISTENCE
// ============================================

void Cartridge::loadBattery() {
    if (!battery || ram.empty()) return;

    std::string sav = filepath;
    auto dot = sav.find_last_of('.');
    auto slash = sav.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || slash < dot)) {
        sav = sav.substr(0, dot);
    }
    sav += ".sav";

    std::ifstream file(sav, std::ios::binary);
    if (!file) return;  // no save yet; start clean

    const char magic[8] = "GBSAV01";
    char header[8] = {0};
    file.read(header, 7);
    if (std::char_traits<char>::compare(header, magic, 7) != 0) {
        std::cerr << "Warning: unrecognized .sav file, ignoring" << std::endl;
        return;
    }

    u32 size = 0;
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    size = std::min<u32>(size, static_cast<u32>(ram.size()));
    if (size > 0) file.read(reinterpret_cast<char*>(ram.data()), size);

    if (rtc_present && !file.eof()) {
        file.read(reinterpret_cast<char*>(rtc_regs), 5);
        file.read(reinterpret_cast<char*>(&rtc_cycles), sizeof(rtc_cycles));
        for (int i = 0; i < 5; i++) rtc_latched[i] = rtc_regs[i];
    }

    std::cout << "  Battery save loaded: " << sav << std::endl;
}

void Cartridge::saveBattery() const {
    if (!battery || ram.empty()) return;

    std::string sav = filepath;
    auto dot = sav.find_last_of('.');
    auto slash = sav.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || slash < dot)) {
        sav = sav.substr(0, dot);
    }
    sav += ".sav";

    std::ofstream file(sav, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "Warning: could not open battery save file " << sav << std::endl;
        return;
    }

    file.write("GBSAV01", 7);
    u32 size = static_cast<u32>(ram.size());
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    file.write(reinterpret_cast<const char*>(ram.data()), ram.size());

    if (rtc_present) {
        file.write(reinterpret_cast<const char*>(rtc_regs), 5);
        file.write(reinterpret_cast<const char*>(&rtc_cycles), sizeof(rtc_cycles));
    }
}

// ============================================
// SAVE STATE
// ============================================

void Cartridge::saveState(std::ofstream& file) const {
    u32 ram_size = static_cast<u32>(ram.size());
    file.write(reinterpret_cast<const char*>(&ram_size), sizeof(ram_size));
    if (ram_size > 0) {
        file.write(reinterpret_cast<const char*>(ram.data()), ram_size);
    }

    file.write(reinterpret_cast<const char*>(&rom_bank), sizeof(rom_bank));
    file.write(reinterpret_cast<const char*>(&bank2), sizeof(bank2));
    file.write(reinterpret_cast<const char*>(&ram_bank), sizeof(ram_bank));
    u8 en = ram_enabled ? 1 : 0;
    u8 m = mode ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&en), sizeof(en));
    file.write(reinterpret_cast<const char*>(&m), sizeof(m));
    file.write(reinterpret_cast<const char*>(&rtc_latch_state), sizeof(rtc_latch_state));
    file.write(reinterpret_cast<const char*>(rtc_regs), sizeof(rtc_regs));
    file.write(reinterpret_cast<const char*>(rtc_latched), sizeof(rtc_latched));
    file.write(reinterpret_cast<const char*>(&rtc_cycles), sizeof(rtc_cycles));
}

void Cartridge::loadState(std::ifstream& file) {
    u32 ram_size = 0;
    file.read(reinterpret_cast<char*>(&ram_size), sizeof(ram_size));
    if (ram_size > 0 && ram_size <= ram.size()) {
        file.read(reinterpret_cast<char*>(ram.data()), ram_size);
    } else if (ram_size > ram.size()) {
        // Size mismatch: skip the stored RAM to stay aligned.
        file.seekg(ram_size, std::ios::cur);
    }

    file.read(reinterpret_cast<char*>(&rom_bank), sizeof(rom_bank));
    file.read(reinterpret_cast<char*>(&bank2), sizeof(bank2));
    file.read(reinterpret_cast<char*>(&ram_bank), sizeof(ram_bank));
    u8 en = 0, m = 0;
    file.read(reinterpret_cast<char*>(&en), sizeof(en));
    file.read(reinterpret_cast<char*>(&m), sizeof(m));
    ram_enabled = (en != 0);
    mode = (m != 0);
    file.read(reinterpret_cast<char*>(&rtc_latch_state), sizeof(rtc_latch_state));
    file.read(reinterpret_cast<char*>(rtc_regs), sizeof(rtc_regs));
    file.read(reinterpret_cast<char*>(rtc_latched), sizeof(rtc_latched));
    file.read(reinterpret_cast<char*>(&rtc_cycles), sizeof(rtc_cycles));
}
