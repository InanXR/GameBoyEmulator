# GameBoy Emulator

<p align="center">
  <img src="assets/Nintendo_Game_Boy_Emulator_Logo.png" alt="GameBoy Emulator logo" width="400">
</p>

A Game Boy (DMG) emulator in C++17 with an SDL2 frontend, built for
accuracy: the complete blargg CPU test suite passes, instruction timing
is verified to the M-cycle, and the PPU renders the dmg-acid2 reference
image pixel-perfect.

![C++17](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)
![SDL2](https://img.shields.io/badge/SDL-2.0-green)
![Tests](https://img.shields.io/badge/tests-13%2F13%20passing-brightgreen)
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
| `dmg-acid2` (mattcurrie) | pixel-perfect: 0 of 23040 pixels differ from the reference |
| blargg `halt_bug` survey ROM | runs to completion; halt-bug trigger matrix matches hardware |
| Unit tests (timer edges, ALU flags, serial, joypad, save states) | 30/30 |
| blargg `mem_timing-2` | not passing; requires sub-M-cycle DIV phase modeling (see [11](#11-known-limitations)) |

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
- **Test suite**: one command runs 13 ROM-based accuracy tests plus 30
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
./build/gameboy "roms/YourGame.gb"
```

| Option | Effect |
|---|---|
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
`cpu_instrs`, `instr_timing`, `halt_bug`, and `dmg-acid2`) into
`.test-roms/` on first run, executes them headless, and verifies the
results — including a pixel-for-pixel framebuffer comparison against
the dmg-acid2 reference image. Details in
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
==============================================
 Results: 13 passed, 0 failed
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

- **CPU**: Sharp LR35902 at 4.194304 MHz. Components tick in lockstep
  with the CPU every M-cycle, so register changes are visible to the
  instruction that reads them. Interrupt dispatch takes 5 M-cycles and
  pushes the correct resume address when waking from HALT.
- **Graphics**: 160x144, 4 shades, 59.7 Hz. Mode timing 80/172/204
  T-cycles (OAM scan, transfer, HBlank). LCDC bit 0 blanks both
  background and window on DMG; sprites still render.
- **Audio**: 512 Hz frame sequencer (DIV-APU): length at 256 Hz, sweep
  at 128 Hz, envelope at 64 Hz. The core synthesizes 44.1 kHz stereo
  into a thread-safe ring buffer; the platform audio callback drains it.
- **Save data**: `<rom>.sav` (battery RAM plus RTC registers) and
  `<rom>.state` (complete machine state, version-checked).

## 11. Known limitations

- blargg `mem_timing-2` does not pass: it requires modeling the exact
  T-cycle within each M-cycle at which DIV advances relative to memory
  accesses. The architecture is M-cycle granular by design.
- No Game Boy Color support; CGB-flagged ROMs run in DMG mode.
- No link cable: the serial port is loopback-to-host only.
- STOP parks the CPU until a joypad press but does not freeze DIV.
- No debugger or cheat-code interface.

Planned: CGB support, rewind, link-cable-over-network, debugger,
sub-M-cycle DIV phase accuracy.

## 12. References

- [Pan Docs](https://gbdev.io/pandocs/) — the Game Boy hardware
  reference; MBC, timer, APU and STAT behavior follows it directly
- Blargg's test ROMs ([retrio mirror](https://github.com/retrio/gb-test-roms)) —
  the CPU and timing verification suites
- [dmg-acid2](https://github.com/mattcurrie/dmg-acid2) (mattcurrie) —
  the PPU rendering acid test
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
core/platform restructure 2026.
