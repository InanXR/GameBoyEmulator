#include "savestate.h"
#include "cpu/cpu.h"
#include "memory/mmu.h"
#include "graphics/ppu.h"
#include "audio/apu.h"
#include "timer/timer.h"
#include <iostream>
#include <cstring>

bool SaveState::save(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create save state file: " << filename << std::endl;
        return false;
    }
    
    try {
        // Write header
        file.write(MAGIC, 7);
        file.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));
        
        // Save each component
        cpu.saveState(file);
        mmu.saveState(file);
        ppu.saveState(file);
        apu.saveState(file);
        timer.saveState(file);
        
        std::cout << "✅ Save state saved: " << filename << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving state: " << e.what() << std::endl;
        return false;
    }
}

bool SaveState::load(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open save state file: " << filename << std::endl;
        return false;
    }
    
    try {
        // Read and verify header
        char magic[8] = {0};
        u8 version = 0;
        file.read(magic, 7);
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        
        if (std::strcmp(magic, MAGIC) != 0) {
            std::cerr << "Invalid save state file (bad magic)" << std::endl;
            return false;
        }
        
        if (version != VERSION) {
            std::cerr << "Incompatible save state version" << std::endl;
            return false;
        }
        
        // Load each component
        cpu.loadState(file);
        mmu.loadState(file);
        ppu.loadState(file);
        apu.loadState(file);
        timer.loadState(file);
        
        std::cout << "✅ Save state loaded: " << filename << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading state: " << e.what() << std::endl;
        return false;
    }
}
