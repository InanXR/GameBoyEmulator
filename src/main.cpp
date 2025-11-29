#include "emulator.h"
#include <iostream>
#include <string>

/**
 * Main Entry Point
 * GameBoy Emulator with SDL2 Graphics
 */

void printHeader() {
    std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  GameBoy Emulator - C++17 + SDL2          ║" << std::endl;
    std::cout << "║  Sharp LR35902 CPU Emulation              ║" << std::endl;
    std::cout << "║  Built by Inan - 2025                     ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════╝\n" << std::endl;
}

int main(int argc, char* argv[]) {
    printHeader();
    
    // Check for ROM argument
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " roms/tetris.gb" << std::endl;
        return 1;
    }
    
    // Create emulator
    Emulator emulator;
    
    // Load ROM
    std::string romPath = argv[1];
    if (!emulator.loadROM(romPath)) {
        return 1;
    }
    
    // Run with SDL2 display!
    emulator.runWithDisplay();
    
    return 0;
}
