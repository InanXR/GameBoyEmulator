/**
 * Unit tests for the core components, driven directly (no ROM needed).
 *
 * These complement the integration test suite (tests/run_tests.sh) by
 * pinning down cycle-accurate behavior of the timer, CPU flag logic,
 * and MBC banking in isolation.
 */
#include "core/memory/mmu.h"
#include "core/timer/timer.h"
#include "core/cpu/cpu.h"
#include "core/graphics/ppu.h"
#include "core/savestate.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <functional>

static int tests_run = 0;
static int tests_failed = 0;

static void check(bool cond, const char* what) {
    tests_run++;
    if (!cond) {
        tests_failed++;
        std::printf("FAIL: %s\n", what);
    }
}

// ---------------------------------------------------------------- Timer

static void test_timer_rate() {
    MMU mmu;
    // No cartridge loaded: MBC writes go nowhere, but the timer is fine.
    Timer& t = mmu.getTimer();

    mmu.write(0xFF07, 0x05);  // TAC: enabled, 262144 Hz (TIMA += 1 every 4 M-cycles)
    mmu.write(0xFF06, 0x00);  // TMA = 0
    mmu.write(0xFF05, 0x00);  // TIMA = 0

    // Track when TIMA changes to verify the period is exactly 4 M-cycles.
    std::vector<int> increment_at;
    u8 last = 0;
    for (int m = 0; m < 200; m++) {
        t.tick(1, mmu);
        u8 now = t.readTIMA();
        if (now != last) {
            increment_at.push_back(m);
            last = now;
        }
    }
    check(!increment_at.empty(), "timer increments at 262144 Hz");
    check(static_cast<int>(increment_at.size()) == 50,
          "timer increments 50 times in 200 M-cycles");

    bool spacing_ok = true;
    for (size_t i = 1; i < increment_at.size(); i++) {
        if (increment_at[i] - increment_at[i - 1] != 4) spacing_ok = false;
    }
    check(spacing_ok, "TIMA increments exactly every 4 M-cycles");
}

static void test_timer_other_rates() {
    MMU mmu;
    Timer& t = mmu.getTimer();
    mmu.write(0xFF07, 0x07);  // 16384 Hz: every 64 M-cycles
    mmu.write(0xFF05, 0x00);
    for (int i = 0; i < 256; i++) t.tick(1, mmu);
    check(t.readTIMA() == 4, "16384 Hz: TIMA=4 after 256 M-cycles");

    mmu.write(0xFF07, 0x00);  // disabled (TAC bit 2 clear)
    u8 frozen = t.readTIMA();
    for (int i = 0; i < 256; i++) t.tick(1, mmu);
    check(t.readTIMA() == frozen, "timer frozen when TAC bit 2 clear");

    mmu.write(0xFF07, 0x05);  // back to 262144 Hz
    mmu.write(0xFF05, 0x00);
    mmu.write(0xFF06, 0x30);  // TMA = 0x30
    for (int i = 0; i < 1020; i++) t.tick(1, mmu);
    check(t.readTIMA() == 0xFF, "TIMA=0xFF just before overflow (255 increments)");
    // Overflow on the 256th increment (M-cycle 1024); TMA reloads one
    // M-cycle later.
    for (int i = 0; i < 5; i++) t.tick(1, mmu);
    check(t.readTIMA() == 0x30, "TIMA reloads from TMA after overflow");
}

static void test_timer_interrupt_delay() {
    MMU mmu;
    Timer& t = mmu.getTimer();
    mmu.write(0xFF07, 0x05);
    mmu.write(0xFF06, 0x00);
    mmu.write(0xFF05, 0xFD);  // 3 increments until overflow

    mmu.write(0xFF0F, 0x00);  // clear IF

    // First 11 M-cycles: no interrupt yet
    for (int i = 0; i < 11; i++) t.tick(1, mmu);
    check((mmu.read(0xFF0F) & 0x04) == 0, "no timer IRQ before overflow");

    // Overflow on the 12th increment; IRQ lands 2 M-cycles later
    // (i.e. during the 14th M-cycle).
    for (int i = 0; i < 2; i++) t.tick(1, mmu);
    check((mmu.read(0xFF0F) & 0x04) == 0, "IRQ not yet set 1 cycle after overflow");
    t.tick(1, mmu);
    check((mmu.read(0xFF0F) & 0x04) != 0, "timer IRQ requested after overflow");
}

static void test_div() {
    MMU mmu;
    Timer& t = mmu.getTimer();
    u8 div0 = mmu.read(0xFF04);
    for (int i = 0; i < 64; i++) t.tick(1, mmu);
    check(static_cast<u8>(div0 + 1) == mmu.read(0xFF04),
          "DIV increments every 64 M-cycles");

    // Writing DIV resets it
    mmu.write(0xFF04, 0x00);
    check(mmu.read(0xFF04) == 0x00, "DIV write resets DIV");
    for (int i = 0; i < 64; i++) t.tick(1, mmu);
    check(mmu.read(0xFF04) == 0x01, "DIV restarts from 0 after write");
}

// ---------------------------------------------------------------- CPU

static void runAsm(MMU& mmu, CPU& cpu, std::initializer_list<u8> code) {
    // Place code at 0x0150 (ROM, bank 0 - readable without a cartridge?
    // With no cart, reads return 0xFF; use HRAM instead).
    u16 addr = 0xFF80;
    int i = 0;
    for (u8 b : code) mmu.write(static_cast<u16>(addr + i++), b);

    CPU clean;
    cpu.reset();
    cpu.PC = addr;
    // enough steps for straight-line code + implied halt
    for (int s = 0; s < 64; s++) {
        if (cpu.halted || cpu.stopped) break;
        cpu.step(mmu);
    }
}

static void test_cpu_alu_flags() {
    MMU mmu;
    CPU cpu;

    // XOR A (AF = 0x00F0 expected: Z set)
    runAsm(mmu, cpu, {0xAF, 0x76});
    check(cpu.A == 0x00 && cpu.F == 0x80, "XOR A clears A, sets Z");

    // LD A,0xFF ; INC A -> A=0, Z set, H set (0xF->0 carry), N clear
    runAsm(mmu, cpu, {0x3E, 0xFF, 0x3C, 0x76});
    check(cpu.A == 0x00 && (cpu.F & 0x80) && (cpu.F & 0x20) && !(cpu.F & 0x40),
          "INC A at 0xFF: Z=1 H=1 N=0");

    // SRA of 0x81 -> 0xC0, carry out = 1 (regression test for the old
    // bool-MSB bug that broke blargg 09/11)
    runAsm(mmu, cpu, {0x3E, 0x81, 0xCB, 0x2F, 0x76});
    check(cpu.A == 0xC0, "SRA 0x81 -> 0xC0 (sign preserved)");

    // SRA of 0x01 -> 0x00 with carry set
    runAsm(mmu, cpu, {0x3E, 0x01, 0xCB, 0x2F, 0x76});
    check(cpu.A == 0x00 && (cpu.F & 0x10), "SRA 0x01 -> 0x00, C=1");

    // DAA after ADD: 0x09 + 0x08 = 0x11 -> DAA -> 0x17
    runAsm(mmu, cpu, {0x3E, 0x09, 0xC6, 0x08, 0x27, 0x76});
    check(cpu.A == 0x17, "DAA after BCD addition 9+8=17");

    // DAA after SUB: 0x45 - 0x38 = 0x0D -> DAA -> 0x07
    runAsm(mmu, cpu, {0x3E, 0x45, 0xD6, 0x38, 0x27, 0x76});
    check(cpu.A == 0x07, "DAA after BCD subtraction 45-38=07");

    // SWAP A of 0x3C -> 0xC3
    runAsm(mmu, cpu, {0x3E, 0x3C, 0xCB, 0x37, 0x76});
    check(cpu.A == 0xC3, "SWAP nibbles");
}

// ---------------------------------------------------------------- serial

static void test_serial() {
    MMU mmu;
    mmu.setSerialCallback(nullptr);
    mmu.write(0xFF01, 'X');
    mmu.write(0xFF02, 0x81);
    check(mmu.getSerialOutput() == "X", "serial byte captured on SC write");
    check((mmu.read(0xFF02) & 0x80) == 0, "SC bit 7 cleared after transfer");
    check((mmu.read(0xFF0F) & 0x08) != 0, "serial IRQ requested");
}

// ---------------------------------------------------------------- joypad

static void test_joypad() {
    MMU mmu;
    Joypad& jp = mmu.getJoypad();

    mmu.write(0xFF00, 0x20);  // select d-pad (P14 low)
    jp.setButton(Joypad::BTN_UP, true);
    check((mmu.read(0xFF00) & 0x0F) == 0x0B, "d-pad selected: UP reads as bit 2 low");
    check(jp.consumeInterruptRequest(), "button press requests joypad IRQ");
    check(!jp.consumeInterruptRequest(), "IRQ request consumed once");

    mmu.write(0xFF00, 0x10);  // select action buttons
    jp.setButton(Joypad::BTN_A, true);
    check((mmu.read(0xFF00) & 0x0F) == 0x0E, "action selected: A reads as bit 0 low");
}

// ---------------------------------------------------------------- save state

static void test_save_state_roundtrip() {
    MMU mmu;
    CPU cpu;
    PPU ppu;
    Timer& timer = mmu.getTimer();

    // Mutate some state so a restore is meaningful
    cpu.reset();
    cpu.A = 0x42;
    cpu.PC = 0x1234;
    mmu.write(0xFF40, 0x91);
    mmu.write(0xC000, 0xAB);
    mmu.write(0xFF07, 0x05);
    mmu.write(0xFF05, 0x10);
    for (int i = 0; i < 100; i++) timer.tick(1, mmu);

    const char* path = "/tmp/gb_state_test.state";
    if (!SaveState::save(path, cpu, mmu, ppu, mmu.getAPU(), timer)) {
        check(false, "save state written");
        return;
    }

    // Dirty everything
    cpu.A = 0;
    cpu.PC = 0;
    mmu.write(0xC000, 0x00);

    bool loaded = SaveState::load(path, cpu, mmu, ppu, mmu.getAPU(), timer);
    check(loaded, "save state loaded");
    check(cpu.A == 0x42 && cpu.PC == 0x1234, "CPU state restored");
    check(mmu.read(0xC000) == 0xAB, "WRAM state restored");

    std::remove(path);
}

int main() {
    std::printf("Running unit tests...\n");

    test_timer_rate();
    test_timer_other_rates();
    test_timer_interrupt_delay();
    test_div();
    test_cpu_alu_flags();
    test_serial();
    test_joypad();
    test_save_state_roundtrip();

    std::printf("%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
