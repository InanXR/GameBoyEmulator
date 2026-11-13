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
    (void)timer;  // timer state is serialized inside MMU::saveState
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "Failed to create save state file: " << filename << std::endl;
        return false;
    }

    // Header
    file.write(MAGIC, 7);
    u8 version = VERSION;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Component states (MMU's state includes the timer and cartridge)
    cpu.saveState(file);
    mmu.saveState(file);
    ppu.saveState(file);
    apu.saveState(file);

    bool ok = static_cast<bool>(file);
    file.close();

    if (ok) {
        std::cout << "Save state written: " << filename << std::endl;
    } else {
        std::cerr << "Error while writing save state: " << filename << std::endl;
    }
    return ok;
}

bool SaveState::load(const std::string& filename,
                     CPU& cpu, MMU& mmu, PPU& ppu,
                     APU& apu, Timer& timer) {
    (void)timer;  // timer state is restored inside MMU::loadState
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open save state file: " << filename << std::endl;
        return false;
    }

    char magic[8] = {0};
    u8 version = 0;
    file.read(magic, 7);
    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (std::strcmp(magic, MAGIC) != 0) {
        std::cerr << "Invalid save state file (bad magic): " << filename << std::endl;
        return false;
    }
    if (version != VERSION) {
        std::cerr << "Incompatible save state version " << static_cast<int>(version)
                  << " (expected " << static_cast<int>(VERSION) << ")" << std::endl;
        return false;
    }

    cpu.loadState(file);
    mmu.loadState(file);
    ppu.loadState(file);
    apu.loadState(file);

    bool ok = static_cast<bool>(file);
    file.close();

    if (ok) {
        std::cout << "Save state loaded: " << filename << std::endl;
    } else {
        std::cerr << "Error while reading save state: " << filename << std::endl;
    }
    return ok;
}
