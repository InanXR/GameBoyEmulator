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

bool Emulator::loadROM(const std::string& filepath, bool force_dmg) {
    std::cout << "Loading ROM: " << filepath << std::endl;
    std::cout << "==========================================" << std::endl;

    mmu.loadCartridge(filepath);

    // Boot as a CGB when the cartridge header says the game supports
    // color (unless the frontend forces DMG with --dmg).
    bool cgb = !force_dmg && mmu.cartridgeSupportsCGB();
    mmu.setCGB(cgb);
    std::cout << "Hardware mode: " << (cgb ? "Game Boy Color (CGB)"
                                           : "Game Boy (DMG)") << std::endl;

    cpu.reset(cgb);
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
    // Power-cycle every component; the battery save is preserved and
    // the hardware model (DMG/CGB) stays as chosen at load time.
    ppu.reset();
    cpu.reset(mmu.isCGB());
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
    //
    // CGB double speed: the CPU, timer and OAM DMA run at twice the
    // rate, but the PPU dot clock and the APU stay in the 4.19 MHz
    // domain. Per CPU M-cycle that means 2 PPU dots (instead of 4)
    // and 2 base T-cycles of APU (instead of 4). The timer's system
    // counter still advances 4 T-cycles per M-cycle: since M-cycles
    // are now consumed twice as fast, DIV and TIMA frequencies double
    // exactly like the hardware's. DIV is frozen while the CPU sleeps
    // in STOP mode.
    cpu.setCycleHook([this] {
        ppu.tick(mmu);
        if (!cpu.stopped) {
            mmu.getTimer().tick(1, mmu);
        }
        mmu.getAPU().tickT(mmu.isDoubleSpeed() ? 2 : 4);
    });

    // A newly pressed button raises the joypad interrupt before the
    // frame runs, so the game can react within the same frame (and a
    // STOPped console wakes up).
    if (mmu.getJoypad().consumeInterruptRequest()) {
        mmu.requestInterrupt(0x10);
    }

    // One frame of PPU time is always 70224 dots. In double-speed
    // mode each dot costs 2 CPU T-cycles, so the CPU cycle budget
    // doubles.
    const u32 CYCLES_PER_FRAME = 70224;
    const u32 frame_budget = mmu.isDoubleSpeed() ? CYCLES_PER_FRAME * 2
                                                 : CYCLES_PER_FRAME;
    u32 frame_cycles = 0;

    while (frame_cycles < frame_budget) {
        u32 before = cpu.cycles;
        cpu.step(mmu);
        frame_cycles += cpu.cycles - before;
    }

    // MBC3 real-time clock (seconds granularity; cheap per-frame
    // update). The RTC quartz is independent of the CPU speed, and a
    // frame always spans the same absolute time, so the budget is the
    // same in both speed modes.
    if (mmu.hasCartridge()) {
        mmu.tickCartridge(CYCLES_PER_FRAME / 4);
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
