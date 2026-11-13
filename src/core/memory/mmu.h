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
 * Full GameBoy memory map (DMG and CGB models):
 *  0x0000-0x3FFF  ROM bank 0
 *  0x4000-0x7FFF  ROM bank N (switchable)
 *  0x8000-0x9FFF  VRAM (CGB: 2 banks of 8 KiB, selected by VBK $FF4F)
 *  0xA000-0xBFFF  cartridge RAM / RTC
 *  0xC000-0xCFFF  WRAM bank 0 (always)
 *  0xD000-0xDFFF  WRAM bank 1-7 (CGB: selected by SVBK $FF70)
 *  0xE000-0xFDFF  Echo RAM (mirrors C000-DDFF, banking included)
 *  0xFE00-0xFE9F  OAM
 *  0xFEA0-0xFEFF  unusable
 *  0xFF00-0xFF7F  I/O registers
 *  0xFF80-0xFFFE  HRAM
 *  0xFFFF         Interrupt Enable
 *
 * CGB additions implemented here:
 *  - KEY1 $FF4D   double-speed preparation (toggled by STOP)
 *  - VBK  $FF4F   VRAM bank select
 *  - SVBK $FF70   WRAM bank select
 *  - HDMA1-5 $FF51-55  VRAM DMA (general-purpose and HBlank)
 *  - RP   $FF56   infrared port (no IR hardware attached)
 *  - OPRI $FF6C   object priority mode
 *  - BGPI/BGPD $FF68-69, OBPI/OBPD $FF6A-6B  palette RAM access
 *  - PCM12/PCM34 $FF76-77  live APU channel outputs
 *  - KEY0 $FF4C and undocumented $FF72-75
 * In DMG mode the CGB registers read $FF and ignore writes.
 */
class MMU {
public:
    MMU();

    void loadCartridge(const std::string& filepath);
    bool hasCartridge() const { return cartridge != nullptr; }
    bool cartridgeSupportsCGB() const {
        return cartridge && cartridge->supportsCGB();
    }

    // Hardware model selection. The emulator picks CGB when the
    // cartridge header says the game supports it ($0143 & $80). In CGB
    // mode the boot register values, banked WRAM/VRAM, palette RAM,
    // HDMA and the speed switch are enabled.
    void setCGB(bool enable);
    bool isCGB() const { return cgb; }

    // ---- CGB speed switch (KEY1 $FF4D) --------------------------------
    bool isDoubleSpeed() const { return double_speed; }
    // Consumed by CPU::STOP: toggles the CPU clock speed and clears
    // the armed bit. After the switch the CPU, timer, serial port and
    // OAM DMA run at double rate; the PPU and APU are unaffected.
    void applySpeedSwitch() {
        double_speed = !double_speed;
        speed_armed = false;
    }
    bool speedSwitchArmed() const { return speed_armed; }

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

    // Direct PPU register update. STAT bits 0-2 (mode + coincidence)
    // are read-only for the CPU, but the PPU itself owns them; the
    // normal write path masks them out, so the PPU must use this.
    void setLY(u8 value) { io[0x44] = value; }
    u8 getLY() const { return io[0x44]; }
    void setSTAT(u8 value) { io[0x41] = value; }

    // OAM DMA transfer
    void doDMATransfer(u8 value);

    // ---- CGB helpers used by the PPU ----------------------------------
    // VRAM byte from an explicit bank. Rendering fetches tile data and
    // BG-map attributes from both banks regardless of VBK, and DMG
    // only has bank 0.
    u8 vramAt(u8 bank, u16 addr) const {
        return vram[static_cast<size_t>(bank) * 0x2000 + (addr - 0x8000)];
    }

    // Palette RAM lookups (RGB555; bit 15 is ignored by hardware).
    u16 bgPaletteColor(int palette, int color) const;
    u16 objPaletteColor(int palette, int color) const;

    // PPU state hooks: palette RAM (CRAM) is inaccessible while the
    // PPU reads it during mode 3; one HDMA block moves per HBlank.
    void setCramLocked(bool locked) { cram_locked = locked; }
    void hblankHDMA();

    // OPRI $FF6C: true = DMG-style X-coordinate object priority
    // (the CGB boot ROM sets it in DMG compatibility mode).
    bool objPriorityByX() const { return (opri & 0x01) != 0; }

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

    // WRAM access honoring the CGB bank selection (echo included).
    u8  readWRAM(u16 addr) const;
    void writeWRAM(u16 addr, u8 value);

    // Palette RAM access through BGPI/OBPI + BGPD/OBPD.
    u8  readPalette(bool bg) const;
    void writePalette(bool bg, u8 value);

    // HDMA engine: FF55 write (start/cancel), one 16-byte block move.
    void startHDMA(u8 value);
    // Returns false when the destination address overflowed (the
    // transfer stops prematurely in that case).
    bool hdmaCopyBlock();

    u8 hdma5Read() const {
        // Bit 7: 0 = transfer active, 1 = done; bits 0-6: remaining
        // 16-byte blocks minus 1 (0x7F when nothing is pending).
        if (!cgb) return 0xFF;
        u8 len = static_cast<u8>(hdma_blocks - 1);
        return hdma_active ? len : static_cast<u8>(0x80 | len);
    }

    std::unique_ptr<Cartridge> cartridge;

    bool cgb = false;

    // DMG: banks 0-1 used. CGB: 8 banks of 4 KiB.
    std::array<u8, 8 * 0x1000> wram;
    // DMG: bank 0 used. CGB: 2 banks of 8 KiB.
    std::array<u8, 2 * 0x2000> vram;
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

    // ---- CGB register state ----
    u8  vram_bank;                  // VBK bit 0
    u8  svbk_raw;                   // SVBK as written (0 maps bank 1)
    bool double_speed;              // KEY1 bit 7
    bool speed_armed;               // KEY1 bit 0
    u8  opri;                       // OPRI bit 0
    u8  rp;                         // RP (bits 7-6, 0 stored)
    u8  bgp_index;                  // FF68 (bit 7 = auto-increment)
    u8  obp_index;                  // FF6A (bit 7 = auto-increment)
    std::array<u8, 64> bg_palette;  // BG palette RAM (8 palettes x 4 colors)
    std::array<u8, 64> obj_palette; // OBJ palette RAM
    bool cram_locked;               // PPU mode 3: CRAM reads $FF, writes dropped

    // HDMA1-4 state and the running HBlank transfer
    u8   hdma1, hdma2, hdma3, hdma4;
    u16  hdma_blocks;   // 16-byte blocks remaining (0 = done)
    bool hdma_active;   // HBlank DMA in progress
    u8   hdma_dst_bank; // VRAM bank latched at transfer start
};
