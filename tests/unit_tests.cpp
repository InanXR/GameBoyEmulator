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

// ---------------------------------------------------------------- CGB

static void test_cgb_boot_state() {
    MMU mmu;
    mmu.setCGB(true);

    // CGB register power-on values after the boot ROM (Pan Docs).
    check(mmu.read(0xFF4D) == 0x7E, "CGB KEY1 reads 0x7E at boot (normal speed)");
    check(mmu.read(0xFF4F) == 0xFE, "CGB VBK reads 0xFE at boot (bank 0)");
    check(mmu.read(0xFF55) == 0xFF, "CGB HDMA5 reads 0xFF at boot (no transfer)");
    check(mmu.read(0xFF70) == 0xF8, "CGB SVBK reads 0xF8 at boot (bank 1)");
    check(mmu.read(0xFF56) == 0x3E, "CGB RP reads 0x3E at boot (no IR reception)");
    check(mmu.read(0xFF51) == 0xFF && mmu.read(0xFF54) == 0xFF,
          "CGB HDMA1-4 are write-only (read 0xFF)");
    check(mmu.read(0xFF4C) == 0x00, "CGB KEY0 reads 0x00");

    // Background palette RAM is initialized to white by the boot ROM.
    check(mmu.read(0xFF69) == 0xFF && mmu.read(0xFF6B) == 0xFF,
          "palette RAM initialized to white (0xFF bytes)");

    // CPU boot registers differ between models.
    CPU cpu;
    cpu.reset(true);
    check(cpu.A == 0x11, "CGB boot: A = 0x11 (model identifier)");
    check(cpu.F == 0x80, "CGB boot: F = Z set, N/H/C clear");
    check(cpu.B == 0x00 && cpu.C == 0x00, "CGB boot: BC = 0x0000");
    check(cpu.D == 0xFF && cpu.E == 0x56, "CGB boot: DE = 0xFF56");
    check(cpu.H == 0x00 && cpu.L == 0x0D, "CGB boot: HL = 0x000D");

    // In DMG mode the CGB registers are absent.
    MMU dmg;
    check(dmg.read(0xFF4D) == 0xFF && dmg.read(0xFF4F) == 0xFF &&
          dmg.read(0xFF55) == 0xFF && dmg.read(0xFF70) == 0xFF,
          "DMG mode: CGB registers read 0xFF");
    dmg.write(0xFF70, 0x03);
    dmg.write(0xFF4F, 0x01);
    check(dmg.read(0xFF70) == 0xFF && dmg.read(0xFF4F) == 0xFF,
          "DMG mode: CGB register writes are ignored");
}

static void test_cgb_vram_banks() {
    MMU mmu;
    mmu.setCGB(true);

    check(mmu.read(0xFF4F) == 0xFE, "VBK: bank 0 selected");
    mmu.write(0xFF4F, 0x01);
    check(mmu.read(0xFF4F) == 0xFF, "VBK: bank 1 selected (reads 0xFF)");

    mmu.write(0x8000, 0xAA);       // -> VRAM bank 1
    check(mmu.vramAt(1, 0x8000) == 0xAA && mmu.vramAt(0, 0x8000) == 0x00,
          "VBK=1: write lands in VRAM bank 1 only");

    mmu.write(0xFF4F, 0x00);
    mmu.write(0x8000, 0x55);        // -> VRAM bank 0
    check(mmu.vramAt(0, 0x8000) == 0x55 && mmu.vramAt(1, 0x8000) == 0xAA,
          "VBK=0: write lands in VRAM bank 0 only");
    check(mmu.read(0x8000) == 0x55, "VBK=0: CPU read sees bank 0");

    // Only bit 0 is meaningful.
    mmu.write(0xFF4F, 0x02);
    check(mmu.read(0xFF4F) == 0xFE, "VBK: only bit 0 is respected");
}

static void test_cgb_wram_banks() {
    MMU mmu;
    mmu.setCGB(true);

    // Bank 0 is fixed at C000; D000 starts at bank 1 (SVBK=0 maps 1).
    mmu.write(0xC000, 0x10);
    mmu.write(0xD000, 0x11);
    check(mmu.read(0xD000) == 0x11, "SVBK=0: D000 uses WRAM bank 1");

    mmu.write(0xFF70, 0x02);
    check(mmu.read(0xFF70) == 0xFA, "SVBK=2 readback (0xF8 | 2)");
    mmu.write(0xD000, 0x22);
    check(mmu.read(0xD000) == 0x22, "SVBK=2: D000 uses WRAM bank 2");

    // Echo RAM mirrors the banked region.
    mmu.write(0xF000, 0x99);        // echo of 0xD000
    check(mmu.read(0xD000) == 0x99 && mmu.read(0xF000) == 0x99,
          "echo RAM mirrors the banked D000 region");

    mmu.write(0xFF70, 0x00);        // 0 maps bank 1
    check(mmu.read(0xD000) == 0x11, "SVBK=0 again: bank 1 data intact");
    check(mmu.read(0xC000) == 0x10, "C000 always bank 0 regardless of SVBK");

    mmu.write(0xFF70, 0x07);
    mmu.write(0xD123, 0x77);
    check(mmu.read(0xD123) == 0x77, "SVBK=7: highest bank works");
}

static void test_cgb_palette_ram() {
    MMU mmu;
    mmu.setCGB(true);

    // Auto-increment from index 0: two writes land at 0 and 1.
    mmu.write(0xFF68, 0x80);
    mmu.write(0xFF69, 0x12);
    mmu.write(0xFF69, 0x34);
    check(mmu.read(0xFF68) == 0x82, "BGPI auto-increments after each BGPD write");

    // Reads never auto-increment.
    mmu.write(0xFF68, 0x00);
    check(mmu.read(0xFF69) == 0x12 && mmu.read(0xFF69) == 0x12,
          "BGPD reads do not auto-increment");
    mmu.write(0xFF68, 0x01);
    check(mmu.read(0xFF69) == 0x34, "BGPD second byte readable");

    // Palette color lookup (little-endian RGB555 words).
    check(mmu.bgPaletteColor(0, 0) == 0x3412, "BG palette color assembles both bytes");

    // OBJ palettes are a separate bank with identical behavior.
    mmu.write(0xFF6A, 0x80);
    mmu.write(0xFF6B, 0xAB);
    mmu.write(0xFF6B, 0xCD);
    check(mmu.objPaletteColor(0, 0) == 0xCDAB, "OBJ palette works like BG palette");

    // Auto-increment wraps 63 -> 0.
    mmu.write(0xFF68, 0xBF);        // auto-inc, index 63
    mmu.write(0xFF69, 0x77);
    check(mmu.read(0xFF68) == 0x80, "BGPI wraps 63 -> 0");

    // CRAM lock (PPU mode 3): reads 0xFF, writes dropped, but the
    // auto-increment still runs.
    mmu.write(0xFF68, 0x00);
    mmu.setCramLocked(true);
    check(mmu.read(0xFF69) == 0xFF, "locked CRAM reads 0xFF");
    mmu.write(0xFF69, 0x99);        // must be dropped
    mmu.setCramLocked(false);
    check(mmu.read(0xFF69) == 0x12, "locked BGPD write was dropped");

    mmu.write(0xFF68, 0x80);
    mmu.setCramLocked(true);
    mmu.write(0xFF69, 0x55);
    check(mmu.read(0xFF68) == 0x81,
          "auto-increment runs even when the write is blocked");
    mmu.setCramLocked(false);
}

static void test_cgb_gdma() {
    MMU mmu;
    mmu.setCGB(true);

    // Pattern in WRAM at 0xC030: HDMA2's low nibble is ignored, so
    // writing 0x3F to it makes the source 0xC030 (bits 3-0 read 0).
    for (int i = 0; i < 32; i++) {
        mmu.write(static_cast<u16>(0xC030 + i), static_cast<u8>(0xA0 + i));
    }
    mmu.write(0xFF51, 0xC0);  // HDMA1: source high
    mmu.write(0xFF52, 0x3F);  // HDMA2: low nibble ignored -> 0x30
    mmu.write(0xFF53, 0x00);  // HDMA3: destination high
    mmu.write(0xFF54, 0x0F);  // HDMA4: low nibble ignored -> 0x00
    check(mmu.read(0xFF52) == 0xFF, "HDMA2 is write-only like HDMA1-4");
    mmu.write(0xFF55, 0x01);  // GDMA, (1+1) blocks = 32 bytes

    bool ok = true;
    for (int i = 0; i < 32; i++) {
        if (mmu.read(static_cast<u16>(0x8000 + i)) != 0xA0 + i) ok = false;
    }
    check(ok, "GDMA copies 32 bytes WRAM -> VRAM bank 0");
    check(mmu.read(0xFF55) == 0xFF, "GDMA completion: FF55 reads 0xFF");
    check(mmu.read(0x8020) == 0x00, "GDMA copies exactly the requested length");

    // The destination bank follows VBK at transfer start; the source
    // registers are reset explicitly since the first GDMA advanced them.
    for (int i = 0; i < 16; i++) {
        mmu.write(static_cast<u16>(0xC050 + i), static_cast<u8>(0x50 + i));
    }
    mmu.write(0xFF4F, 0x01);
    mmu.write(0xFF51, 0xC0);
    mmu.write(0xFF52, 0x50);
    mmu.write(0xFF53, 0x00);
    mmu.write(0xFF54, 0x00);
    mmu.write(0xFF55, 0x00);  // 1 block, GDMA -> VRAM bank 1
    check(mmu.read(0x8000) == 0x50, "GDMA destination uses the selected VRAM bank");
    mmu.write(0xFF4F, 0x00);
    check(mmu.read(0x8000) == 0xA0, "GDMA wrote bank 1 only (bank 0 untouched)");
    check(mmu.vramAt(1, 0x8000) == 0x50 && mmu.vramAt(0, 0x8000) == 0xA0,
          "GDMA: banks hold their own transfers");
}

static void test_cgb_hdma_hblank() {
    MMU mmu;
    mmu.setCGB(true);

    for (int i = 0; i < 48; i++) {
        mmu.write(static_cast<u16>(0xC100 + i), static_cast<u8>(0x10 + i));
    }
    mmu.write(0xFF51, 0xC1);
    mmu.write(0xFF52, 0x00);
    mmu.write(0xFF53, 0x00);
    mmu.write(0xFF54, 0x00);
    mmu.write(0xFF55, 0x81);  // HBlank DMA, 2 blocks

    check((mmu.read(0xFF55) & 0x80) == 0x00, "HBlank DMA active: FF55 bit 7 clear");
    check((mmu.read(0xFF55) & 0x7F) == 0x01, "HBlank DMA: 1 block remains after start");

    mmu.hblankHDMA();  // first HBlank
    check((mmu.read(0xFF55) & 0x7F) == 0x00, "HBlank DMA: last block remains");

    mmu.hblankHDMA();  // second HBlank: completes
    check(mmu.read(0xFF55) == 0xFF, "HBlank DMA completed: FF55 reads 0xFF");

    bool ok = true;
    for (int i = 0; i < 32; i++) {
        if (mmu.read(static_cast<u16>(0x8000 + i)) != 0x10 + i) ok = false;
    }
    check(ok, "HBlank DMA moved both blocks");
    check(mmu.read(0x8020) == 0x00, "HBlank DMA moves nothing past the length");

    // Cancelling: restart 1 block, write bit 7 = 0 before it moves.
    mmu.write(0xFF55, 0x80);  // 1 block
    mmu.write(0xFF55, 0x00);  // cancel
    check(mmu.read(0xFF55) == 0x80, "cancelled HBlank DMA: bit 7 set, length kept");
    mmu.hblankHDMA();
    check(mmu.read(0x8020) == 0x00, "cancelled HBlank DMA moves nothing");
}

static void test_cgb_speed_switch() {
    MMU mmu;
    mmu.setCGB(true);
    CPU cpu;
    cpu.reset(true);

    check(!mmu.isDoubleSpeed(), "starts in normal speed");
    mmu.write(0xFF4D, 0x01);  // arm the switch
    check(mmu.read(0xFF4D) == 0x7F, "KEY1 armed: bit 0 set, bit 7 clear");

    cpu.executeOpcode(0x10, mmu);  // STOP consumes the armed switch
    check(cpu.stopped, "speed-switch STOP pauses the CPU");

    // The switch pause is ~2050 M-cycles; the timer stays frozen
    // while stopped (the emulator skips timer ticks), then the speed
    // flips and the armed bit clears.
    int guard = 3000;
    while (cpu.stopped && guard-- > 0) {
        cpu.step(mmu);
    }
    check(mmu.read(0xFF4D) == 0xFE, "after switch: KEY1 bit 7 set (double speed)");
    check(mmu.isDoubleSpeed(), "double-speed mode engaged");

    // A second armed STOP switches back.
    mmu.write(0xFF4D, 0x01);
    cpu.executeOpcode(0x10, mmu);
    guard = 3000;
    while (cpu.stopped && guard-- > 0) {
        cpu.step(mmu);
    }
    check(mmu.read(0xFF4D) == 0x7E, "second switch: back to normal speed");

    // An unarmed STOP is a plain sleep until a joypad press.
    cpu.executeOpcode(0x10, mmu);
    check(cpu.stopped && !mmu.isDoubleSpeed(),
          "unarmed STOP sleeps without changing speed");
    mmu.write(0xFF0F, 0x10);  // joypad interrupt flag
    cpu.step(mmu);
    check(!cpu.stopped, "STOP wakes on IF bit 4 (joypad)");

    // On DMG, KEY1 does not exist and STOP never switches speed.
    MMU dmg;
    dmg.write(0xFF4D, 0x01);
    CPU dmg_cpu;
    dmg_cpu.reset(false);
    dmg_cpu.executeOpcode(0x10, dmg);
    int g2 = 3000;
    while (dmg_cpu.stopped && g2-- > 0) {
        dmg_cpu.step(dmg);
    }
    check(dmg_cpu.stopped, "DMG STOP: sleeps (no speed switch path)");
}

static void test_cgb_save_state() {
    MMU mmu;
    mmu.setCGB(true);
    CPU cpu;
    PPU ppu;
    Timer& timer = mmu.getTimer();

    // Distinct CGB state: VRAM bank, WRAM bank, palette, HDMA.
    mmu.write(0xFF4F, 0x01);
    mmu.write(0x8000, 0xC3);
    mmu.write(0xFF4F, 0x00);
    mmu.write(0xFF70, 0x03);
    mmu.write(0xD000, 0x42);
    mmu.write(0xFF68, 0x80);
    mmu.write(0xFF69, 0x00);
    mmu.write(0xFF69, 0x3F);  // BG palette 0 color 0 = 0x3F00
    cpu.reset(true);
    cpu.A = 0x99;

    const char* path = "/tmp/gb_cgb_state_test.state";
    if (!SaveState::save(path, cpu, mmu, ppu, mmu.getAPU(), timer)) {
        check(false, "CGB save state written");
        return;
    }

    // Dirty everything: switch to DMG defaults, other banks, palettes.
    mmu.setCGB(false);
    mmu.write(0xFF4F, 0x01);
    mmu.write(0x8000, 0x00);
    cpu.A = 0;

    bool loaded = SaveState::load(path, cpu, mmu, ppu, mmu.getAPU(), timer);
    check(loaded, "CGB save state loaded");
    check(mmu.isCGB(), "hardware mode restored from the save state");
    check(cpu.A == 0x99, "CGB save state: CPU restored");
    check(mmu.vramAt(1, 0x8000) == 0xC3, "CGB save state: VRAM bank 1 restored");
    check(mmu.read(0xFF4F) == 0xFE, "CGB save state: VBK restored");
    check(mmu.read(0xD000) == 0x42, "CGB save state: WRAM bank 3 restored");
    check(mmu.read(0xFF70) == 0xFB, "CGB save state: SVBK readback restored");
    check(mmu.bgPaletteColor(0, 0) == 0x3F00, "CGB save state: palette RAM restored");

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
    test_cgb_boot_state();
    test_cgb_vram_banks();
    test_cgb_wram_banks();
    test_cgb_palette_ram();
    test_cgb_gdma();
    test_cgb_hdma_hblank();
    test_cgb_speed_switch();
    test_cgb_save_state();

    std::printf("%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
