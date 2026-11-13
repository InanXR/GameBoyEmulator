#include "timer.h"
#include "memory/mmu.h"

Timer::Timer()
    : sys_counter(0xAB00)  // DIV reads 0xAB right after the DMG boot ROM
    , tima(0x00)
    , tma(0x00)
    , tac(0x00)
    , prev_bit(false)
    , reload_delay(0)
    , interrupt_delay(0)
{
    prev_bit = currentBit();
}

void Timer::powerOn() {
    sys_counter = 0xAB00;
    tima = 0x00;
    tma = 0x00;
    tac = 0x00;
    reload_delay = 0;
    interrupt_delay = 0;
    prev_bit = currentBit();
}

void Timer::writeTIMA(u8 v) {
    tima = v;
    // A write during the delayed-reload window cancels the scheduled reload.
    reload_delay = 0;
    interrupt_delay = 0;
}

void Timer::writeTAC(u8 v, MMU& mmu, bool dmg_mode) {
    u8 old_tac = tac;
    tac = v & 0x07;

    if ((old_tac & 0x04) && (tac & 0x04)) {
        // The edge detector follows whatever bit TAC selects. Changing
        // the selection while the timer is enabled can therefore create
        // a falling edge (old bit high, new bit low).
        bool old_selected = (sys_counter >> selectedBit(old_tac)) & 1;
        bool new_selected = currentBit();
        if (old_selected && !new_selected) {
            tima++;
            if (tima == 0) serviceOverflow(mmu);
        }
    } else if ((old_tac & 0x04) && !(tac & 0x04) && dmg_mode) {
        // DMG-only: disabling the timer while the selected bit reads 1
        // sends one final tick. (Does not happen on CGB.)
        if (currentBit()) {
            tima++;
            if (tima == 0) serviceOverflow(mmu);
        }
    }
    prev_bit = currentBit();
}

void Timer::resetDIV(MMU& mmu) {
    bool old_bit = currentBit();
    sys_counter = 0;
    bool new_bit = currentBit();

    // Writing DIV while the selected bit reads 1 creates a falling edge.
    if ((tac & 0x04) && old_bit && !new_bit) {
        tima++;
        if (tima == 0) serviceOverflow(mmu);
    }
    prev_bit = new_bit;
}

void Timer::tick(int m_cycles, MMU& mmu) {
    // Advance in single M-cycles so edges are never missed.
    while (m_cycles-- > 0) {
        // Pending delayed reload / interrupt bookkeeping first.
        if (interrupt_delay > 0 && --interrupt_delay == 0) {
            mmu.requestInterrupt(0x04);  // Timer interrupt (IF bit 2)
        }
        if (reload_delay > 0 && --reload_delay == 0) {
            tima = tma;
        }

        sys_counter += 4;  // one M-cycle = 4 T-cycles

        bool new_bit = currentBit();
        if ((tac & 0x04) && prev_bit && !new_bit) {
            // Falling edge of the selected bit -> TIMA increments.
            tima++;
            if (tima == 0) serviceOverflow(mmu);
        }
        prev_bit = new_bit;
    }
}

void Timer::serviceOverflow(MMU& mmu) {
    // TIMA reads 0 for one M-cycle, then reloads from TMA; the interrupt
    // is requested two M-cycles after the overflow.
    reload_delay = 1;
    interrupt_delay = 2;
    (void)mmu;
}

// ============================================
// SAVE STATE
// ============================================

void Timer::saveState(std::ofstream& file) const {
    file.write(reinterpret_cast<const char*>(&sys_counter), sizeof(sys_counter));
    file.write(reinterpret_cast<const char*>(&tima), sizeof(tima));
    file.write(reinterpret_cast<const char*>(&tma), sizeof(tma));
    file.write(reinterpret_cast<const char*>(&tac), sizeof(tac));
    u8 prev = prev_bit ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&prev), sizeof(prev));
    file.write(reinterpret_cast<const char*>(&reload_delay), sizeof(reload_delay));
    file.write(reinterpret_cast<const char*>(&interrupt_delay), sizeof(interrupt_delay));
}

void Timer::loadState(std::ifstream& file) {
    file.read(reinterpret_cast<char*>(&sys_counter), sizeof(sys_counter));
    file.read(reinterpret_cast<char*>(&tima), sizeof(tima));
    file.read(reinterpret_cast<char*>(&tma), sizeof(tma));
    file.read(reinterpret_cast<char*>(&tac), sizeof(tac));
    u8 prev = 0;
    file.read(reinterpret_cast<char*>(&prev), sizeof(prev));
    prev_bit = (prev != 0);
    file.read(reinterpret_cast<char*>(&reload_delay), sizeof(reload_delay));
    file.read(reinterpret_cast<char*>(&interrupt_delay), sizeof(interrupt_delay));
}
