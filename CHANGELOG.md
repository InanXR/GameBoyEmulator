# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

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

[2.0.0]: https://github.com/InanXR/GameBoyEmulator/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/InanXR/GameBoyEmulator/releases/tag/v1.0.0
