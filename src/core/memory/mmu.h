#pragma once
#include "common/types.h"
#include "cartridge.h"
#include "input/joypad.h"
#include "timer/timer.h"
#include "audio/apu.h"
#include <memory>
#include <array>
#include <fstream>
#include <string>
#include <functional>

/**
 * Memory Management Unit
 *
 * Full GameBoy memory map:
 *  0x0000-0x3FFF  ROM bank 0
 *  0x4000-0x7FFF  ROM bank N (switchable)
 *  0x8000-0x9FFF  VRAM
 *  0xA000-0xBFFF  cartridge RAM / RTC
 *  0xC000-0xDFFF  WRAM
 *  0xE000-0xFDFF  Echo RAM
 *  0xFE00-0xFE9F  OAM
 *  0xFEA0-0xFEFF  unusable
 *  0xFF00-0xFF7F  I/O registers
 *  0xFF80-0xFFFE  HRAM
 *  0xFFFF         Interrupt Enable
 */
class MMU {
public:
    MMU();

    void loadCartridge(const std::string& filepath);
    bool hasCartridge() const { return cartridge != nullptr; }

    // Power-cycle: restore power-on memory/IO state and reset the
    // timer, APU and joypad. The cartridge (and its battery RAM) is
    // preserved; use reloadCartridge() to also reset MBC state.
    void reset();

    // Recreate the cartridge from its file: fresh MBC state with the
    // battery save reloaded from disk.
    void reloadCartridge();

    u8 read(u16 addr) const;
    void write(u16 addr, u8 value);

    // Interrupt helpers (bit 0=VBlank 1=LCD 2=Timer 3=Serial 4=Joypad)
    void requestInterrupt(u8 bit) {
        io[0x0F] = static_cast<u8>(io[0x0F] | (bit & 0x1F));
    }

    // Direct PPU access to LY without the write side effects
    void setLY(u8 value) { io[0x44] = value; }
    u8 getLY() const { return io[0x44]; }

    // Internal PPU register update. STAT bits 0-2 (mode + coincidence)
    // are read-only for the CPU, but the PPU itself owns them; the
    // normal write path masks them out, so the PPU must use this.
    void setSTAT(u8 value) { io[0x41] = value; }

    // OAM DMA transfer
    void doDMATransfer(u8 value);

    const std::array<u8, 0x2000>& getVRAM() const { return vram; }
    const std::array<u8, 0xA0>& getOAM() const { return oam; }

    Joypad& getJoypad() { return joypad; }
    Timer& getTimer() { return timer; }
    APU& getAPU() { return apu; }

    // Advance the cartridge RTC by m_cycles M-cycles (MBC3 cartridges).
    void tickCartridge(int m_cycles) { if (cartridge) cartridge->tick(m_cycles); }

    // Serial output callback (blargg test ROMs print results here)
    void setSerialCallback(std::function<void(char)> cb) { serial_callback = std::move(cb); }
    const std::string& getSerialOutput() const { return serial_output; }

    // Battery RAM persistence passthrough
    void saveBattery() const { if (cartridge) cartridge->saveBattery(); }

    // Save/Load state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);

private:
    // Power-on memory/IO state, shared by the constructor and reset().
    void powerOn();

    std::unique_ptr<Cartridge> cartridge;

    std::array<u8, 0x2000> wram;
    std::array<u8, 0x2000> vram;
    std::array<u8, 0x80>   hram;
    std::array<u8, 0xA0>   oam;
    std::array<u8, 0x80>   io;

    mutable Joypad joypad;
    Timer timer;
    APU apu;

    u8 ie_register;

    // Serial port state
    std::function<void(char)> serial_callback;
    std::string serial_output;

    // Joypad register shadow kept in io[0x00] (select bits only)
    u8 joyp_select;
};
