# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [3.0.0] — 2026-11-13

Full Game Boy Color support, verified pixel-perfect against the
cgb-acid2 reference test, plus the first accurate STOP emulation.

### Added

- **Game Boy Color hardware model.** Cartridges flagged CGB in the
  header ($0143 = $80/$C0) boot as a CGB with the correct post-boot
  register state (A=$11, SC=$7F, KEY1=$7E, VBK=$FE, HDMA1-5=$FF,
  RP=$3E, SVBK=$F8, BG palette RAM initialized to white); `--dmg`
  forces DMG mode for any ROM. In DMG mode the CGB registers read
  $FF and ignore writes.
- **Color palettes**: 8 background + 8 object palettes in palette RAM
  (RGB555) through BGPI/BGPD and OBPI/OBPD with write auto-increment
  (wrapping, running even when the write is blocked) and the mode-3
  CRAM lock (reads $FF, writes dropped).
- **VRAM banking** (VBK $FF4F): 16 KiB across two banks; BG map
  attributes in bank 1 (palette, tile bank, X/Y flips, BG-over-OBJ
  priority); sprite tile banks via OAM attribute bit 3.
- **WRAM banking** (SVBK $FF70): 32 KiB across eight banks, bank 0
  fixed at C000, writing 0 selects bank 1, echo region included.
- **HDMA/GDMA** (HDMA1-5 $FF51-55): general-purpose and HBlank VRAM
  DMA with nibble-masked addresses, FF55 readback (bit 7 = active),
  cancel-keeps-length semantics, destination-overflow stop, and the
  VRAM destination bank latched at transfer start.
- **Double-speed mode** (KEY1 $FF4D + STOP): the documented
  arm-then-STOP sequence with the ~2050 M-cycle hardware pause; CPU,
  timer/DIV, serial and OAM DMA run at double rate while the PPU (2
  dots per M-cycle) and APU (ticked in base T-cycles) keep their
  4.19 MHz timing.
- **CGB priority rules**: LCDC bit 0 as BG/OBJ master-priority flag,
  per-pixel resolution honoring the OAM priority flag, BG attribute
  priority and BG color 0 (Pan Docs table); OAM-index sprite priority
  in CGB mode, DMG-style X priority via OPRI.
- **CGB register file**: RP infrared port, PCM12/PCM34 live APU
  channel outputs, KEY0, OPRI, undocumented $FF72-75.
- **cgb-acid2** added to the accuracy suite (14 tests total) with an
  exact-RGB pixel comparison; rendered pixel-perfect.
- 69 new unit checks (99 total) covering CGB boot state, VRAM/WRAM
  banking, palette RAM auto-increment and locking, GDMA and HBlank
  DMA, the speed switch, and CGB save states.

### Changed

- **STOP now freezes the system counter (DIV) on both models** while
  the CPU sleeps (previously documented as not emulated); waking on a
  joypad press is unchanged.
- Framebuffer is now RGB555 (DMG stores palette shades 0-3); the
  frontend and screenshots convert CGB colors with (v << 3) | (v >> 2)
  and DMG shades with the green palette.
- Save states are version 3 (v2 states are rejected cleanly); the
  format adds the hardware mode, speed, banks, palette RAM and HDMA
  state.
- Serial: transfers only complete with the internal clock selected
  (SC bit 0), matching slave-mode behavior; SC bit 1 (CGB fast clock)
  is preserved.
- The frame budget doubles to 140448 CPU T-cycles in double-speed
  mode (a PPU frame is always 70224 dots).

### Fixed

- DMG-only timer quirk: disabling the timer while the TAC-selected
  bit reads 1 sends one final TIMA increment (does not happen on CGB).
- SC write storage masked the internal-clock bit out on DMG, and the
  boot SC value now reads back $7E as documented.

## [2.0.0] — 2026-11-13

Accuracy-first overhaul of the 2025 codebase: every blargg CPU test now
passes, instruction timing is M-cycle exact, and the PPU renders the
dmg-acid2 reference image pixel-perfect.

### Added

- **dmg-acid2 pixel-perfect rendering**: window layer (two tile maps,
  internal line counter, mid-frame WX/LCDC switching), full DMG sprite
  priority rules (X order, OAM order, OBJ-to-BG priority, 10-per-line
  limit, 8×16 tile index masking), STAT interrupts with rising-edge
  blocking, LYC re-evaluated every M-cycle
- **Complete APU**: channels 3 (wave) and 4 (noise LFSR), CH1 frequency
  sweep with shadow register, stereo NR50/NR51 mixing, per-channel DACs,
  documented high-pass filter, NR52 power control
- **DMG-accurate timer**: 16-bit system counter, DIV as its upper byte,
  TIMA clocked on falling edges of the TAC-selected bit, write-to-DIV/
  TAC edge cases, delayed TMA reload and interrupt
- **Serial port emulation** — ROM output is echoed to stdout, enabling
  automated verification
- **Battery saves**: cartridge RAM + MBC3 RTC persist to `<rom>.sav`
- **Headless mode** (`--headless N`, `--screenshot`) and the automated
  accuracy suite (`tests/run_tests.sh`, 13 tests) and unit tests
  (`tests/unit_tests.cpp`, 30 checks, via CTest)
- **Save states v2**: complete machine state (PPU scanline/window, APU
  channels, timer phase, cartridge/RTC), per-ROM files
- **Modern frontend**: gamepad support, turbo (Tab), pause, reset,
  fullscreen, volume/mute, screenshots, resizable window, per-game
  quicksaves, headless core library (`gameboy_core`) with SDL confined
  to `src/platform/`
- **Infrastructure**: CMakePresets, CI on 3 platforms, warning-clean
  build (`-Wall -Wextra -Wshadow -Wold-style-cast`), numbered
  documentation set under `docs/`

### Changed

- MBC1 now implements both banking modes including mode-1 remapping of
  the 0000-3FFF region; MBC2 allocates its built-in 512×4-bit RAM;
  MBC3 RTC actually ticks (halt flag, latched registers, 512-day
  carry); MBC5 masks the rumble bit
- CPU interrupt dispatch after HALT wake pushes the correct resume
  address; HALT bug implemented; STOP parks until a joypad press
- Components tick in lockstep with the CPU every M-cycle (cycle hook),
  making mid-instruction register changes observable

### Fixed

- `DIV` never advanced (always read 0) — timer register plumbing was
  self-defeating in the 2025 code
- `SRA` corrupted bit 0 for negative values (MSB stored in a `bool`)
- Cycle counts: `CALL cc`, `RET cc` (taken and not taken), `RETI`,
  `RST`, `PUSH`, CB ops on `(HL)`, interrupt dispatch
- STAT mode bits and LYC flag were unwritable by the PPU through the
  CPU write mask, freezing coincidence at its power-on value
- Sprite buffers leaked the previous scanline's sprites when OBJ was
  disabled mid-frame

## [1.0.0] — 2025-11-29

Initial release: Sharp LR35902 CPU (512 opcodes), PPU (background and
sprites), APU (square channels 1–2), timer, MBC1/2/3/5, SDL2 display,
save states, keyboard input.

[2.0.0]: https://github.com/InanXR/GameBoyEmulator/releases/tag/v2.0.0
[1.0.0]: https://github.com/InanXR/GameBoyEmulator/releases/tag/v1.0.0
