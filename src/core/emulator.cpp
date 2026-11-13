#include "emulator.h"
#include "savestate.h"
#include <iostream>

Emulator::Emulator()
    : running(false)
    , frameCount(0)
{
}

Emulator::~Emulator() {
    running = false;
    if (mmu.hasCartridge()) {
        mmu.saveBattery();
    }
}

bool Emulator::loadROM(const std::string& filepath) {
    std::cout << "Loading ROM: " << filepath << std::endl;
    std::cout << "==========================================" << std::endl;

    mmu.loadCartridge(filepath);
    cpu.reset();
    ppu = PPU();
    rom_path = filepath;
    state_path = siblingPath(".state");
    running = true;

    std::cout << "==========================================" << std::endl;
    std::cout << "Emulator ready!" << std::endl;
    std::cout << "==========================================" << std::endl;
    return true;
}

std::string Emulator::siblingPath(const char* extension) const {
    std::string base = rom_path;
    auto dot = base.find_last_of('.');
    auto slash = base.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || slash < dot)) {
        base = base.substr(0, dot);
    }
    return base + extension;
}

void Emulator::reset() {
    // Power-cycle every component; the battery save is preserved.
    ppu.reset();
    cpu.reset();
    mmu.reset();
    mmu.reloadCartridge();
    frameCount = 0;
    std::cout << "Console reset" << std::endl;
}

bool Emulator::saveState() {
    return SaveState::save(state_path, cpu, mmu, ppu, mmu.getAPU(), mmu.getTimer());
}

bool Emulator::loadState() {
    return SaveState::load(state_path, cpu, mmu, ppu, mmu.getAPU(), mmu.getTimer());
}

void Emulator::runFrame() {
    // Components are ticked in lockstep with the CPU through its
    // M-cycle hook (see setCycleHook) for cycle-accurate behavior.
    cpu.setCycleHook([this] {
        ppu.tick(mmu);
        mmu.getTimer().tick(1, mmu);
        mmu.getAPU().tick(1);
    });

    // A newly pressed button raises the joypad interrupt before the
    // frame runs, so the game can react within the same frame.
    if (mmu.getJoypad().consumeInterruptRequest()) {
        mmu.requestInterrupt(0x10);
    }

    constexpr u32 CYCLES_PER_FRAME = 70224;
    u32 frame_cycles = 0;

    while (frame_cycles < CYCLES_PER_FRAME) {
        u32 before = cpu.cycles;
        cpu.step(mmu);
        frame_cycles += cpu.cycles - before;
    }

    // MBC3 real-time clock (seconds granularity; cheap per-frame update)
    if (mmu.hasCartridge()) {
        mmu.tickCartridge(70224 / 4);
    }

    frameCount++;
}

void Emulator::runHeadless(int frames) {
    frameCount = 0;
    running = true;

    for (int i = 0; i < frames && running; i++) {
        runFrame();
    }

    mmu.saveBattery();
    running = false;
}
