#pragma once
#include "common/types.h"
#include <fstream>

// Forward declaration
class MMU;

/**
 * GameBoy Timer (DMG-accurate)
 *
 * Implements the real "system counter" circuit described in Pan Docs:
 *
 *   - A 16-bit internal counter increments every T-cycle (4.194304 MHz).
 *   - DIV ($FF04) is the upper 8 bits of that counter.
 *   - TIMA ($FF05) increments on each FALLING EDGE of the system-counter
 *     bit selected by TAC (bit 9/3/5/7 for TAC codes 0/1/2/3).
 *   - Writing DIV resets the whole counter, which can produce a falling
 *     edge and bump TIMA (obscure but relied-upon behavior).
 *   - Writing TAC can also produce a falling edge by changing which bit
 *     is selected.
 *   - On TIMA overflow, TIMA reloads from TMA one M-cycle later, and the
 *     interrupt is requested two M-cycles after the overflow.
 */
class Timer {
public:
    Timer();

    // Power-cycle: restore the power-on register state.
    void powerOn();

    // Advance the timer by `m_cycles` M-cycles (must be <= 2 per call).
    void tick(int m_cycles, MMU& mmu);

    // Reset the whole system counter (write to $FF04).
    void resetDIV(MMU& mmu);

    // Register access
    u8 readDIV() const { return static_cast<u8>(sys_counter >> 8); }
    u8 readTIMA() const { return tima; }
    u8 readTMA() const { return tma; }
    u8 readTAC() const { return 0xF8 | (tac & 0x07); } // unused bits read 1 (DMG)

    void writeTIMA(u8 v);
    void writeTMA(u8 v) { tma = v; }
    // dmg_mode: apply the DMG-only quirk where disabling the timer
    // while the selected bit reads 1 sends one final tick (CGBs don't).
    void writeTAC(u8 v, MMU& mmu, bool dmg_mode);

    // Save/Load state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);

private:
    u16 sys_counter;      // 16-bit system counter (DIV is the high byte)
    u8  tima;             // Timer counter
    u8  tma;              // Timer modulo (reload value)
    u8  tac;              // Timer control (only bits 0-2 meaningful)

    bool prev_bit;        // Last state of the selected system-counter bit

    int  reload_delay;    // M-cycles until TIMA reloads from TMA
    int  interrupt_delay; // M-cycles until the interrupt is requested

    // Schedule the delayed TMA reload + interrupt after an overflow.
    void serviceOverflow(MMU& mmu);

    static int selectedBit(u8 tac) {
        // TAC code -> system counter bit:
        // 0 -> bit 9 (4096 Hz), 1 -> bit 3 (262144 Hz),
        // 2 -> bit 5 (65536 Hz), 3 -> bit 7 (16384 Hz)
        switch (tac & 0x03) {
            case 0: return 9;
            case 1: return 3;
            case 2: return 5;
            default: return 7;
        }
    }

    bool currentBit() const { return (sys_counter >> selectedBit(tac)) & 1; }
};
