# GameBoy Emulator

<p align="center">
  <img src="assets/Nintendo_Game_Boy_Emulator_Logo.png" alt="GameBoy Emulator logo" width="400">
</p>

A Game Boy and Game Boy Color emulator in C++17 with an SDL2 frontend,
built for accuracy: the complete blargg CPU test suite passes, instruction
timing is verified to the M-cycle, and the PPU renders both the dmg-acid2
and cgb-acid2 reference images pixel-perfect.

![C++17](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)
![SDL2](https://img.shields.io/badge/SDL-2.0-green)
![Tests](https://img.shields.io/badge/tests-15%2F15%20passing-brightgreen)
![License](https://img.shields.io/badge/license-MIT-orange)

## Contents

1. [Accuracy](#1-accuracy)
2. [Features](#2-features)
3. [Documentation](#3-documentation)
4. [Requirements](#4-requirements)
5. [Building](#5-building)
6. [Running](#6-running)
7. [Controls](#7-controls)
8. [Testing](#8-testing)
9. [Project layout](#9-project-layout)
10. [Technical summary](#10-technical-summary)
11. [Known limitations](#11-known-limitations)
12. [References](#12-references)
13. [License and contributing](#13-license-and-contributing)

## 1. Accuracy

| Suite | Result |
|---|---|
| blargg `cpu_instrs` (01-11, all 11 ROMs) | pass |
| blargg `instr_timing` | pass (M-cycle-exact instruction timing) |
| blargg `mem_timing-2` | pass (T-cycle-precise memory access timing) |
| `dmg-acid2` (mattcurrie) | pixel-perfect: 0 of 23040 pixels differ from the reference |
| `cgb-acid2` (mattcurrie) | pixel-perfect: 0 of 23040 pixels differ, exact RGB |
| blargg `halt_bug` survey ROM | runs to completion; halt-bug trigger matrix matches hardware |
| Unit tests (timer edges, ALU flags, serial, joypad, CGB registers, HDMA, save states) | 99/99 |

Headless throughput is roughly 16x real time on a modern desktop core.

## 2. Features

### Emulation core

- **CPU (Sharp LR35902)**: all 512 opcodes; cycle-exact M-cycle timing;
  EI one-instruction delay; HALT bug; immediate interrupt dispatch on
  HALT wake-up
- **PPU**: background and window layers (internal window line counter,
  mid-frame WX/LCDC switching); sprites with full DMG priority rules
  (X-order, OAM-order, OBJ-to-BG priority, 10-sprites-per-line limit,
  8x16 tile index masking); STAT interrupts with rising-edge blocking;
  LYC=LY re-evaluated every cycle
- **APU**: all four channels — two pulse (CH1 with frequency sweep),
  32-step wave, noise LFSR (15/7-bit); stereo mixing via NR50/NR51;
  per-channel DACs; documented high-pass filter
- **Timer**: the DMG circuit — 16-bit system counter, DIV as its upper
  byte, TIMA clocked on falling edges of the TAC-selected bit,
  write-to-DIV/TAC edge cases, delayed TMA reload and interrupt
- **Serial port**: transfers echoed to stdout, which is how the
  automated test suite collects ROM results

### Game Boy Color (CGB)

Cartridges flagged CGB in the header (`0x143` = `0x80` or `0xC0`) boot as
a Game Boy Color with the CGB boot register state (A=`0x11`); everything
else boots as a DMG, and `--dmg` forces DMG mode for any ROM.

- **Color palettes**: 8 background and 8 object palettes in palette RAM
  (RGB555), accessed through BGPI/BGPD and OBPI/OBPD with write
  auto-increment; palette RAM locks while the PPU reads it (mode 3)
- **VRAM banking**: 16 KiB across two banks (VBK `$FF4F`); bank 1 holds
  tile data and the BG-map attribute table — per-tile palette, VRAM
  bank, X/Y flips and BG-over-OBJ priority
- **WRAM banking**: 32 KiB across eight banks (SVBK `$FF70`), bank 0
  always at C000, echo region included
- **CGB priority rules**: LCDC bit 0 becomes the BG/OBJ master-priority
  flag; per-pixel resolution honors the OAM priority flag, the BG map
  attribute priority bit and BG color 0, exactly per Pan Docs
- **Object priority**: OAM-index order in CGB mode, DMG-style
  X-coordinate order when OPRI selects it
- **VRAM DMA (HDMA/GDMA)**: general-purpose instant transfers and
  HBlank transfers moving one 16-byte block per HBlank of lines 0-143,
  with cancel, readback and destination-overflow semantics
- **Double speed**: KEY1-armed STOP toggles the CPU clock after the
  ~2050 M-cycle hardware pause; CPU, timer, DIV and OAM DMA run at
  double rate while the PPU and APU keep their normal timing
- **STOP semantics**: DIV (the system counter) freezes while the CPU
  sleeps, on both models; wake on joypad press
- **CGB register file**: RP (infrared), PCM12/PCM34 (live APU channel
  outputs), KEY0, OPRI and the undocumented `$FF72-75`; all read `$FF`
  and ignore writes in DMG mode

### Cartridges

- **MBC1**: both banking modes, including mode-1 remapping of the
  0000-3FFF region and RAM banking
- **MBC2**: built-in 512x4-bit RAM
- **MBC3**: real-time clock — 32768 Hz ticking, latched registers,
  halt flag, 512-day carry
- **MBC5**: 9-bit ROM banks; rumble-cart RAM bank masking
- **Battery saves**: cartridge RAM and RTC state persist to `<rom>.sav`
  next to the ROM, loaded automatically on start

### Frontend and tooling

- **Core/frontend split**: the entire machine is a platform-free static
  library (`gameboy_core`); SDL exists only in `src/platform/`
- **Save states**: complete machine state (CPU, memory, PPU
  scanline/window counters, APU channels, timer phase, cartridge/RTC),
  one `<rom>.state` file per ROM, version-checked
- **Headless mode**: `--headless N` runs N frames without a window;
  `--screenshot` writes the final framebuffer
- **Display**: hardware-accelerated, integer-scaled, resizable window;
  fullscreen toggle; gamepad support
- **Runtime controls**: turbo, pause, reset, volume, screenshots
- **Test suite**: one command runs 15 ROM-based accuracy tests plus 99
  unit checks through CTest

## 3. Documentation

The documentation set in [`docs/`](docs/00-INDEX.md) is numbered for
reading in order:

| # | Document | Contents |
|---|---|---|
| 01 | [Getting Started](docs/01-GETTING-STARTED.md) | build, run, controls, troubleshooting |
| 02 | [Architecture](docs/02-ARCHITECTURE.md) | components, the M-cycle lockstep design |
| 03 | [Hardware Notes](docs/03-HARDWARE-NOTES.md) | every DMG behavior implemented, with sources |
| 04 | [Testing](docs/04-TESTING.md) | accuracy suite, unit tests, CI |
| 05 | [Accuracy Status](docs/05-ACCURACY.md) | verification matrix, known limitations |
| 06 | [References](docs/06-REFERENCES.md) | Pan Docs, Gekkio gbctr, test ROM sources |
| 07 | [Dev Tools](docs/07-DEV-TOOLS.md) | opcode generator and specification |

## 4. Requirements

| Platform | Toolchain |
|---|---|
| Linux | GCC 9+ or Clang 10+, CMake 3.16+, SDL2 development package |
| macOS | Xcode command line tools, CMake 3.16+, SDL2 (Homebrew) |
| Windows | Visual Studio 2019+, CMake 3.16+, SDL2 via vcpkg |

C++17 is the minimum language standard.

Install dependencies:

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libsdl2-dev

# macOS
brew install cmake sdl2
```

```powershell
# Windows (vcpkg)
vcpkg install sdl2:x64-windows
vcpkg integrate install
```

## 5. Building

```bash
git clone https://github.com/InanXR/GameBoyEmulator.git
cd GameBoyEmulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, add
`-DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake`
and build with `cmake --build build --config Release`.

With CMake 3.21+, presets are available:

```bash
cmake --preset release && cmake --build --preset release
ctest --preset release-accurate    # unit + accuracy tests (downloads ROMs)
```

Build targets: `gameboy` (the emulator) and `gb_unit_tests` (unit
tests, also registered with CTest).

## 6. Running

```bash
./build/gameboy "roms/YourGame.gb"      # DMG game
./build/gameboy "roms/YourGame.gbc"     # Game Boy Color game
```

The hardware model follows the cartridge header (CGB flag `0x143`).
A ROM flagged for both systems boots as a CGB; use `--dmg` to force
monochrome mode.

| Option | Effect |
|---|---|
| `--dmg` | Force DMG mode (ignore the cartridge's CGB flag) |
| `--headless N` | Run N frames without a window or audio, then exit |
| `--screenshot FILE` | Headless only: write the final framebuffer as a PPM (P6) image |
| `--scale N` | Window integer scale, 1-8 (default 4) |

## 7. Controls

Keyboard and gamepad inputs are merged; both work simultaneously.

| Input | Button |
|---|---|
| Arrow keys / D-Pad | D-Pad |
| Z / gamepad A | A |
| X / gamepad B | B |
| Enter / gamepad Start | Start |
| Shift / gamepad Back | Select |

| Hotkey | Action |
|---|---|
| F1 | Reset console (battery save preserved) |
| F2 | Screenshot to `<rom>.ppm` |
| F5 | Save state to `<rom>.state` |
| F8 | Load state |
| P | Pause / resume |
| Tab | Turbo (hold; runs 5x) |
| F11 | Fullscreen toggle |
| + / - | Volume up / down |
| M | Mute / unmute |
| ESC | Quit |

## 8. Testing

The accuracy suite downloads freely redistributable test ROMs (blargg's
`cpu_instrs`, `instr_timing`, `mem_timing-2`, `halt_bug`, and mattcurrie's
`dmg-acid2` and `cgb-acid2`) into `.test-roms/` on first run, executes them
headless, and verifies the results — including pixel-for-pixel framebuffer
comparisons against both acid2 reference images. Details in
[docs/04-Testing](docs/04-TESTING.md).

```bash
bash tests/run_tests.sh
```

Expected output:

```
PASS  01-special.gb
PASS  02-interrupts.gb
...
PASS  instr_timing.gb
INFO  halt_bug.gb (no verdict; survey ROM ran to completion)
PASS  dmg-acid2.gb (pixel-perfect)
PASS  cgb-acid2.gbc (pixel-perfect, exact RGB)
PASS  mem_timing-2.gb (read/write/modify access timing)
==============================================
 Results: 15 passed, 0 failed
 All tests passed!
```

Unit tests run through CTest:

```bash
cd build && ctest --output-on-failure
```

CI builds on Linux, macOS, and Windows and runs both suites on every
push (`.github/workflows/ci.yml`).

## 9. Project layout

```
GameBoyEmulator/
├── CMakeLists.txt              root build: project, options, deps, subdirs
├── CMakePresets.json           debug / release / release-accurate presets
├── cmake/                      shared CMake helper modules
├── .github/workflows/ci.yml    CI: build (3 OS) + accuracy suite
├── docs/                       numbered documentation set (00-INDEX.md)
├── tools/opcodes/              opcode specification + generator
├── tests/
│   ├── run_tests.sh            accuracy suite (downloads ROMs, verifies)
│   └── unit_tests.cpp          CTest unit tests
├── roms/                       user ROMs (not committed)
└── src/
    ├── core/                   headless emulation library (no SDL)
    │   ├── cpu/                  LR35902: 512 opcodes, exact timing
    │   ├── memory/               MMU + cartridge (MBC1/2/3/5, RTC, .sav)
    │   ├── graphics/             PPU: BG + window + sprites, STAT IRQs
    │   ├── audio/                APU: 4 channels, mixer, sample buffer
    │   ├── timer/                DMG timer circuit (falling edges, DIV)
    │   ├── input/                joypad (+ press interrupt)
    │   ├── common/               shared types
    │   ├── emulator.{h,cpp}       session: frames, states, headless, battery
    │   └── savestate.{h,cpp}     versioned full-state serialization
    └── platform/               SDL2 frontend
        ├── main.cpp              CLI, windowed loop, pacing, hotkeys
        ├── display.{h,cpp}       window, input, gamepads
        └── audio_backend.{h,cpp} SDL audio device draining the APU
```

## 10. Technical summary

- **CPU**: Sharp LR35902 at 4.194304 MHz (8.388608 MHz in CGB double
  speed). Components tick in lockstep with the CPU every M-cycle, so
  register changes are visible to the instruction that reads them.
  Interrupt dispatch takes 5 M-cycles and pushes the correct resume
  address when waking from HALT.
- **Graphics**: 160x144, 59.7 Hz. DMG: 4 shades; CGB: RGB555 colors
  from palette RAM. The dot clock always runs at 4.19 MHz — in CGB
  double speed the CPU does 2 dots of work per M-cycle, so the frame
  is 140448 CPU T-cycles instead of 70224. Mode timing 80/172/204
  T-cycles (OAM scan, transfer, HBlank). LCDC bit 0 blanks both
  background and window on DMG; on CGB it is the BG/OBJ
  master-priority flag.
- **Audio**: 512 Hz frame sequencer (DIV-APU): length at 256 Hz, sweep
  at 128 Hz, envelope at 64 Hz. The core synthesizes 44.1 kHz stereo
  into a thread-safe ring buffer; the platform audio callback drains it.
- **Save data**: `<rom>.sav` (battery RAM plus RTC registers) and
  `<rom>.state` (complete machine state including the CGB register
  file, version-checked).

## 11. Known limitations

- CGB HBlank DMA moves each 16-byte block instantly at HBlank entry
  rather than stalling the CPU for the hardware's ~8 M-cycles per
  block; GDMA is likewise atomic. Data placement and completion flags
  are correct, transfer *duration* is not billed to the CPU.
- On DMG, hardware turns the LCD off during STOP; the emulator keeps
  rendering (the DIV freeze is emulated on both models).
- No link cable: the serial port is loopback-to-host only.
- No debugger or cheat-code interface.
- The DIV-APU coupling (DIV writes clocking the sound frame sequencer)
  is modeled by rate, not by the real falling-edge path, so blargg's
  `dmg_sound` and mooneye-grade T-cycle timer tests are out of reach
  without a finer bus model.

Planned: rewind, link-cable-over-network, debugger.

## 12. References

- [Pan Docs](https://gbdev.io/pandocs/) — the Game Boy hardware
  reference; MBC, timer, APU, CGB and STAT behavior follows it directly
- Blargg's test ROMs ([retrio mirror](https://github.com/retrio/gb-test-roms)) —
  the CPU and timing verification suites
- [dmg-acid2](https://github.com/mattcurrie/dmg-acid2) and
  [cgb-acid2](https://github.com/mattcurrie/cgb-acid2) (mattcurrie) —
  the PPU rendering acid tests
- [Gekkio's gbctr](https://gekkio.fi/files/gb-docs/gbctr.pdf) —
  interrupt and HALT hardware analysis
- SDL2 — cross-platform graphics and audio

## 13. License and contributing

MIT License — see [LICENSE](LICENSE).

Contributions are welcome. Requirements for changes: a warning-free
build (`-Wall -Wextra -Wshadow -Wold-style-cast`), passing `ctest`, and
a green `bash tests/run_tests.sh`. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the full guide and
[CHANGELOG.md](CHANGELOG.md) for release history.

---

Maintained by Inan. Original release 2025; accuracy overhaul and
core/platform restructure 2026; Game Boy Color support 2026 (v3.0).
