#pragma once
#include "../utils/types.h"
#include <fstream>

// Forward declaration
class MMU;

/**
 * GameBoy Timer
 * 
 * Handles the DIV and TIMA registers.
 * - DIV (0xFF04): Increments at 16384Hz
 * - TIMA (0xFF05): Increments at frequency specified by TAC
 * - TMA (0xFF06): Modulo (reload value for TIMA)
 * - TAC (0xFF07): Control register
 */
class Timer {
public:
    Timer();
    
    // Step timer by n M-cycles
    void step(int cycles, MMU& mmu);
    
    // Reset DIV register (called when writing to 0xFF04)
    void resetDIV();
    
    // Save/Load state (minimal stub)
    void saveState(std::ofstream& file) const { /* TODO: implement */ }
    void loadState(std::ifstream& file) { /* TODO: implement */ }
    
private:
    // Internal counters (track M-cycles)
    int div_counter;   // For DIV register
    int tima_counter;  // For TIMA register
    
    // Check if TIMA should increment
    void checkTIMA(int cycles, MMU& mmu);
};
