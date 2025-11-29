#include "timer.h"
#include "../memory/mmu.h"

Timer::Timer() 
    : div_counter(0)
    , tima_counter(0)
{
}

void Timer::resetDIV() {
    div_counter = 0;
    // Note: In real hardware, resetting DIV can affect TIMA, but we'll skip that obscure behavior for now
}

void Timer::step(int cycles, MMU& mmu) {
    // 1. Handle DIV Register (0xFF04)
    // DIV increments at 16384Hz
    // CPU M-cycles = 1.048576 MHz
    // 1048576 / 16384 = 64 M-cycles
    
    div_counter += cycles;
    while (div_counter >= 64) {
        div_counter -= 64;
        u8 div = mmu.read(0xFF04);
        mmu.write(0xFF04, div + 1); // Direct write to bypass reset logic in MMU
    }
    
    // 2. Handle TIMA Register (0xFF05)
    checkTIMA(cycles, mmu);
}

void Timer::checkTIMA(int cycles, MMU& mmu) {
    u8 tac = mmu.read(0xFF07);
    
    // Bit 2: Timer Enable
    if (!(tac & 0x04)) {
        return;
    }
    
    // Bits 1-0: Input Clock Select
    // 00: 4096 Hz   (Every 256 M-cycles)
    // 01: 262144 Hz (Every 4 M-cycles)
    // 10: 65536 Hz  (Every 16 M-cycles)
    // 11: 16384 Hz  (Every 64 M-cycles)
    
    int threshold = 256;
    switch (tac & 0x03) {
        case 0: threshold = 256; break;
        case 1: threshold = 4;   break;
        case 2: threshold = 16;  break;
        case 3: threshold = 64;  break;
    }
    
    tima_counter += cycles;
    
    while (tima_counter >= threshold) {
        tima_counter -= threshold;
        
        u8 tima = mmu.read(0xFF05);
        if (tima == 0xFF) {
            // Overflow!
            u8 tma = mmu.read(0xFF06);
            mmu.write(0xFF05, tma);  // Reload from TMA
            
            // Request Timer Interrupt (Bit 2)
            u8 if_reg = mmu.read(0xFF0F);
            mmu.write(0xFF0F, if_reg | 0x04);
        } else {
            mmu.write(0xFF05, tima + 1);
        }
    }
}
