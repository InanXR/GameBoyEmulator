# GameBoy Emulator

<p align="center">
  <img src="assets/Nintendo_Game_Boy_Emulator_Logo.png" alt="GameBoy Emulator Logo" width="400">
</p>

A Game Boy (DMG) emulator written in C++17 with SDL2, built for accuracy:
the full blargg CPU test suite passes, instruction timing is verified to the
M-cycle, and the PPU renders the dmg-acid2 reference image **pixel-perfect**.

![GameBoy Emulator](https://img.shields.io/badge/GameBoy-Emulator-blue)
![C++17](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)
![SDL2](https://img.shields.io/badge/SDL-2.0-green)
![Tests](https://img.shields.io/badge/tests-13%2F13%20passing-brightgreen)
![License](https://img.shields.io/badge/license-MIT-orange)

## 1. 📚 Documentation

The full documentation set lives in [`docs/`](docs/00-INDEX.md) and is
numbered for reading in order:

| # | Document | |
|---|---|---|
| 01 | [Getting Started](docs/01-GETTING-STARTED.md) | build, run, controls, troubleshooting |
| 02 | [Architecture](docs/02-ARCHITECTURE.md) | components, the M-cycle lockstep design |
| 03 | [Hardware Notes](docs/03-HARDWARE-NOTES.md) | every DMG behavior implemented, with references |
| 04 | [Testing](docs/04-TESTING.md) | the accuracy suite, unit tests, CI |
| 05 | [Accuracy Status](docs/05-ACCURACY.md) | verification matrix, known limitations |
| 06 | [References](docs/06-REFERENCES.md) | Pan Docs, Gekkio, test ROM sources |
| 07 | [Dev Tools](docs/07-DEV-TOOLS.md) | opcode generator + specification |

## 2. ✨ Features

### Core Emulation
- **Sharp LR35902 CPU** — complete 512-opcode instruction set, cycle-exact
  M-cycle timing (verified by blargg's `instr_timing`), EI delay semantics,
  the HALT bug, and immediate interrupt dispatch on HALT wake-up
- **PPU** — background, window (with internal line counter and mid-frame
  WX/LCDC switching), sprites with full DMG priority rules (X-order,
  OAM-order, OBJ-to-BG priority, 10-sprite-per-line limit, 8×16 tile
  index masking), STAT interrupts with rising-edge blocking behavior,
  and LYC=LY re-evaluated every cycle
- **APU** — all four channels: pulse ×2 (CH1 with frequency sweep),
  32-step wave output, and noise (15/7-bit LFSR), mixed in stereo via
  NR50/NR51 with the documented high-pass filter
- **Timer** — the real DMG circuit: 16-bit system counter, DIV as its
  upper byte, TIMA clocked on falling edges of the selected bit,
  write-to-DIV/TAC edge cases, and the delayed TMA reload + interrupt
- **Serial port** — transfers are echoed to stdout (this is how the
  automated test suite collects results)

### Memory Bank Controllers
- **MBC1** — both banking modes, including mode-1 remapping of the
  0000-3FFF region and RAM banking
- **MBC2** — built-in 512×4-bit RAM
- **MBC3** — real-time clock: 32768 Hz ticking, latched registers,
  halt flag, and 512-day carry
- **MBC5** — 9-bit ROM banks, rumble-cart RAM bank masking
- **Battery saves** — battery-backed cartridge RAM (and RTC state)
  persists to a `.sav` file next to the ROM, automatically

### Modern Features
- **Save states** — complete emulator state (CPU, memory, PPU scanline
  and window counters, APU channels, timer phase, cartridge/RTC), one
  file per ROM
- **Headless core library** — the whole machine is a platform-free
  static library (`gameboy_core`); SDL lives only in the frontend
- **Headless mode** — `--headless N` runs N frames with no window for
  automated testing; `--screenshot` dumps the framebuffer
- **SDL2 display** — hardware accelerated, integer-scaled, resizable
  window, optional fullscreen (F11)
- **Gamepad support** — Xbox/PlayStation controllers work out of the box
- **Turbo** (Tab), **pause** (P), **reset** (F1), **volume** (+/-, M)
- **CI-ready test suite** — one command runs the entire accuracy suite
  (see [Testing](#-testing))

## 3. 🎯 Verified Accuracy

| Test Suite | Result |
|---|---|
| Blargg `cpu_instrs` (01–11, all 11 ROMs) | ✅ all pass |
| Blargg `instr_timing` | ✅ passes (M-cycle-exact instruction timing) |
| `dmg-acid2` (mattcurrie) | ✅ **pixel-perfect** — 0/23040 pixels differ from the reference |
| Blargg `halt_bug` survey ROM | ✅ runs to completion; halt-bug trigger matrix matches hardware |
| Unit tests (timer edges, ALU flags, serial, joypad, save states) | ✅ 30/30 |
| Blargg `mem_timing-2` | ⚠️ not passing — requires sub-M-cycle DIV phase modeling (known limitation) |

The emulator runs at ~16× real-time speed in headless mode.

## 4. 🚀 Getting Started

### Prerequisites

#### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev
```

#### macOS
```bash
brew install cmake sdl2
```

#### Windows
```powershell
vcpkg install sdl2:x64-windows
vcpkg integrate install
```

### Building

```bash
git clone https://github.com/InanXR/GameBoyEmulator.git
cd GameBoyEmulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

(On Windows, add `-DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake`
and build with `cmake --build build --config Release`.)

With CMake ≥ 3.21, presets are available:

```bash
cmake --preset release && cmake --build --preset release
ctest --preset release-accurate     # unit + accuracy tests (downloads ROMs)
```

### Running

```bash
./build/gameboy "roms/YourGame.gb"
```

Command line options:

| Option | Effect |
|---|---|
| `--headless N` | Run N frames without a window (testing) |
| `--screenshot FILE` | Headless only: write the final framebuffer as a PPM image |
| `--scale N` | Window scale factor 1–8 (default 4) |

## 5. 🎮 Controls

| Key | GameBoy Button |
|-----|----------------|
| **Arrow Keys** | D-Pad |
| **Z** | A |
| **X** | B |
| **Enter** | Start |
| **Shift** | Select |
| Gamepad A/B/Start/Back + D-Pad | Same buttons |

| Hotkey | Action |
|-----|----------------|
| **F1** | Reset console |
| **F2** | Screenshot → `rom.ppm` |
| **F5** | Save state → `rom.state` |
| **F8** | Load state |
| **P** | Pause |
| **Tab** | Turbo (hold) |
| **F11** | Fullscreen |
| **+ / -** | Volume up / down |
| **M** | Mute |
| **ESC** | Quit |

## 6. 🧪 Testing

The automated accuracy suite downloads freely redistributable test ROMs
(blargg's `cpu_instrs`, `instr_timing`, `halt_bug` and `dmg-acid2`) and runs
them against the emulator in headless mode
(details: [docs/04-Testing](docs/04-TESTING.md)):

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

Unit tests (timer edge cases, CPU flags, serial, joypad, save-state
round-trips) run through CTest:

```bash
cd build && ctest --output-on-failure
```

CI runs both on every push (see `.github/workflows/ci.yml`).

## 7. 📁 Project Structure

```
GameBoyEmulator/
├── CMakeLists.txt            # root build: project, options, deps, subdirs
├── CMakePresets.json         # debug / release / release-accurate presets
├── cmake/                    # shared CMake helper modules
├── .github/workflows/ci.yml  # CI: build (3 OS) + accuracy suite
├── docs/                     # numbered documentation set (00-INDEX.md)
├── tools/opcodes/            # opcode specification + generator
├── tests/
│   ├── run_tests.sh          # accuracy suite (downloads ROMs, verifies)
│   └── unit_tests.cpp        # CTest unit tests
├── roms/                     # your ROMs (not committed)
└── src/
    ├── core/                 # headless emulation library (no SDL)
    │   ├── cpu/              #   LR35902: 512 opcodes, M-cycle-exact timing
    │   ├── memory/           #   MMU + cartridge (MBC1/2/3/5, RTC, .sav)
    │   ├── graphics/         #   PPU: BG + window + sprites, STAT interrupts
    │   ├── audio/            #   APU: 4 channels, sweep, stereo mixing, HPF
    │   ├── timer/            #   DMG timer circuit (falling edges, DIV)
    │   ├── input/            #   joypad (+ interrupt on press)
    │   ├── common/           #   shared types
    │   ├── emulator.{h,cpp}  #   session: frames, states, headless, battery
    │   └── savestate.{h,cpp} #   versioned full-state serialization
    └── platform/             # SDL2 frontend
        ├── main.cpp          #   CLI, windowed loop, pacing, hotkeys
        ├── display.{h,cpp}   #   window, input, gamepads
        └── audio_backend.{h,cpp} # SDL audio device
```

## 8. 🔧 Technical Details

### CPU
- **Processor**: Sharp LR35902 (modified Z80), 4.194304 MHz
- **Timing**: components tick in lockstep with the CPU every M-cycle,
  so register changes are visible to the instruction that reads them
- **Interrupts**: dispatched in 5 M-cycles, push the correct resume
  address when waking from HALT

### Graphics
- 160×144, 4 shades, ~59.7 Hz
- Mode timing: 80 / 172 / 204 T-cycles (OAM, transfer, HBlank)
- LCDC bit 0 blanks both BG and window on DMG; sprites still render

### Audio
- 512 Hz frame sequencer (DIV-APU): length at 256 Hz, sweep at 128 Hz,
  envelope at 64 Hz
- Each channel has its own DAC; the mixer sums per NR51 panning and
  applies NR50 master volume before the high-pass filter

### Save Data
- Battery saves: `<rom>.sav` (cartridge RAM + RTC registers)
- Save states: `<rom>.state` (complete machine state, version-checked)

## 9. 📝 Known Limitations

- Blargg's `mem_timing-2` does not pass: it requires modeling the exact
  T-cycle within each M-cycle at which DIV advances relative to memory
  accesses
- No Game Boy Color support
- No serial link cable (the port is emulated locally for test output)
- STOP parks the CPU until a joypad press (DIV is not frozen)
- No debugger / cheat-code UI

## 10. 🎯 Future Improvements

- [ ] Game Boy Color (CGB) support
- [ ] Rewind functionality
- [ ] Link-cable-over-network
- [ ] Debugger interface
- [ ] Sub-M-cycle DIV phase accuracy (mem_timing-2)

## 11. 🙏 Acknowledgements

- **Pan Docs** — the comprehensive Game Boy hardware reference; the MBC,
  timer, APU and STAT behavior in this emulator follows it directly
- **Blargg's test ROMs** — the CPU/timing verification suite
- **dmg-acid2** (mattcurrie) — the PPU rendering acid test
- **Gekkio's gbctr** — interrupt/HALT hardware analysis
- **SDL2** — cross-platform graphics and audio

## 12. 📄 License

MIT License — see LICENSE file for details.

## 13. 🤝 Contributing

Contributions welcome! Run the test suite before submitting:
`bash tests/run_tests.sh` must stay green.

---

**Built with ❤️ by Inan — 2025–2026**
