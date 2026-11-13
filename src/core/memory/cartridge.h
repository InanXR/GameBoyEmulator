#pragma once
#include "common/types.h"
#include <vector>
#include <string>
#include <fstream>

/**
 * Cartridge / ROM Handler
 *
 * Loads GameBoy ROM files and implements the memory bank controllers:
 *
 *  - No MBC (ROM only)
 *  - MBC1  : 5-bit ROM bank + 2-bit secondary register, two banking modes
 *            (mode 1 remaps the 0000-3FFF region and RAM banks)
 *  - MBC2  : built-in 512 x 4-bit RAM
 *  - MBC3  : up to 128 ROM banks + real-time clock (latched registers,
 *            32768 Hz quartz, halt flag, 512-day carry)
 *  - MBC5  : 9-bit ROM banks + 4-bit RAM banks (rumble carts mask bit 3)
 *
 * Battery-backed cartridges persist their RAM (and RTC state) to a .sav
 * file next to the ROM.
 */
class Cartridge {
public:
    explicit Cartridge(const std::string& filepath);

    // ROM access (handles banking)
    u8 readROM(u16 addr) const;
    void writeROM(u16 addr, u8 value);  // MBC control

    // RAM access (handles banking; also RTC registers on MBC3)
    u8 readRAM(u16 addr) const;
    void writeRAM(u16 addr, u8 value);

    // Header info
    std::string getTitle() const;
    bool hasBattery() const { return battery; }
    bool hasRTC() const { return rtc_present; }
    const std::string& getFilePath() const { return filepath; }

    // Battery RAM persistence (.sav file next to the ROM)
    void loadBattery();
    void saveBattery() const;

    // Advance the RTC by m_cycles M-cycles (MBC3 cartridges only).
    void tick(int m_cycles);

    // Save/Load emulator state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);

    // For savestates: fresh copy of volatile MBC state.
    struct MBCState {
        int rom_bank;
        int ram_bank;
        bool ram_enabled;
        bool mode;
        u8 rtc_latch_state;
    };

private:
    std::vector<u8> rom;
    std::vector<u8> ram;
    std::string filepath;

    // Cartridge type from header 0x0147
    u8 cart_type;

    // MBC variant selector
    enum class MBC { NONE, MBC1, MBC2, MBC3, MBC5 } mbc;

    bool battery;       // battery-backed RAM
    bool rtc_present;   // MBC3 with timer (types 0x0F / 0x10)

    // ---- MBC state ----
    int  rom_bank;      // 5-bit (MBC1) / 7-bit (MBC3) / 9-bit (MBC5) register
    int  bank2;         // MBC1 secondary 2-bit register
    int  ram_bank;      // RAM bank register
    bool ram_enabled;
    bool mode;          // MBC1 banking mode select (1 = advanced)

    // ---- MBC1 addressing helpers ----
    u32 romOffsetLow() const;   // effective ROM offset for 0x0000-0x3FFF
    u32 romOffsetHigh() const;  // effective ROM offset for 0x4000-0x7FFF
    int effectiveRAMBank() const;

    // ---- MBC3 RTC ----
    u8  rtc_latch_state;        // detects 0x00 -> 0x01 latch pulse
    u32 rtc_cycles;             // 32768 Hz accumulator (T-cycles)
    u8  rtc_regs[5];            // live: sec, min, hr, days_lo, days_hi
    u8  rtc_latched[5];         // latched copies visible to the game
    void tickRTC(int t_cycles);

    void parseHeader();
    size_t romBanks() const { return (rom.size() + 0x3FFF) / 0x4000; }
    size_t ramBanks() const { return ram.empty() ? 0 : (ram.size() + 0x1FFF) / 0x2000; }
};
