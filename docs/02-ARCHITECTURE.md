# 02 — Architecture

How the emulator is put together: the components, who owns what, and
the design decisions that matter for accuracy.

**Contents**

1. [Big Picture](#1-big-picture)
2. [Component Reference](#2-component-reference)
3. [The M-Cycle Lockstep Design](#3-the-m-cycle-lockstep-design)
4. [Main Loop & Frame Pacing](#4-main-loop--frame-pacing)
5. [Save State Format](#5-save-state-format)
6. [Headless Mode](#6-headless-mode)
7. [Directory Map](#7-directory-map)

---

## 1. Big Picture

The codebase is split into two layers, the same split used by the major
emulator projects (mGBA, SameBoy, libretro cores):

- **`src/core` — the emulation library** (`gameboy_core`): a headless,
  platform-free machine. Pure C++17; no SDL, no windowing, no UI.
- **`src/platform` — the SDL2 frontend**: window, input, audio device,
  the main loop, and the CLI.

The frontend drives the core one frame at a time (`Emulator::runFrame`);
the test suite drives the same core headless. This keeps every hardware
behavior testable without any window system:

```
 +----------------------------------------------------------+
 | Frontend  (src/platform)                                  |
 |   main.cpp: loop, pacing, hotkeys, CLI                    |
 |   Display: window + keyboard/gamepad input                 |
 |   AudioBackend: SDL device draining the APU ring buffer    |
 +----------------------------+-----------------------------+
                              |  runFrame() / saveState() / reset()
              +---------------v----------------+
              |  Emulator (session)             |
              |  src/core/emulator.cpp          |
              +---------------+----------------+
                              |
             +----------------+----------------+
             |                |                |
        +----v----+      +----v----+     +----v-----+
        |   CPU   |      |   PPU   |     |   APU    |
        | (LR35902)|     | (video) |     | (audio)  |
        +----+----+      +----+----+     +----+-----+
             |                |               |
             |  +---------+   |               |
             +->|   MMU   |<--+---------------+
                | (bus/I/O)|
                +--+----+--+
                   |    |
          +--------+    +---------+
          |                       |
   +------v--------+      +-------v---------+
   |  Cartridge    |      | Timer / Joypad  |
   | (MBC1-5 + RTC)|      | / Serial        |
   +---------------+      +-----------------+
```

Key rules:

1. **All hardware state lives inside the component that owns it** (the
   DIV phase belongs to the Timer, the window line counter to the PPU).
   Components communicate through register reads/writes via the MMU,
   exactly like the silicon.
2. **`src/core` never includes SDL.** The core synthesizes audio into a
   thread-safe ring buffer; the frontend's audio callback drains it.
   This decoupling is what lets the unit tests link the core alone.

## 2. Component Reference

### 2.1 Core library (`gameboy_core`, `src/core`)

| Component | Files | Owns |
|---|---|---|
| CPU | `core/cpu/cpu.cpp`, `core/cpu/instructions.cpp` | Registers, IME/EI-delay/HALT-bug state, instruction decode, cycle accounting |
| MMU | `core/memory/mmu.cpp` | Address decode, VRAM/WRAM/OAM/HRAM, I/O register semantics (incl. read/write masks), serial port, OAM DMA, interrupt flags |
| Cartridge | `core/memory/cartridge.cpp` | ROM banks, MBC1/2/3/5 controllers, RTC, battery persistence (`.sav`) |
| PPU | `core/graphics/ppu.cpp` | Line timing, BG/window rendering, sprite priority, STAT/LYC interrupt line, framebuffer |
| APU | `core/audio/apu.cpp` | 4 channels, frame sequencer, sweep, DACs, stereo mixing, high-pass filter, thread-safe sample ring buffer |
| Timer | `core/timer/timer.cpp` | 16-bit system counter, DIV, TIMA/TMA/TAC, falling-edge detection, delayed reload/IRQ |
| Joypad | `core/input/joypad.cpp` | Button state, P1 register encoding, press-interrupt request |
| Emulator | `core/emulator.cpp` | Session: frame stepping, cycle hook wiring, headless mode, save states, battery management |
| SaveState | `core/savestate.cpp` | Versioned serialization of the whole machine |

### 2.2 Frontend (`gameboy`, `src/platform`)

| Component | Files | Owns |
|---|---|---|
| Entry / main loop | `platform/main.cpp` | CLI parsing, windowed loop, frame pacing, hotkeys, screenshots, headless dispatch |
| Display | `platform/display.cpp` | SDL window/texture, keyboard+gamepad input merge, hotkey queue, fullscreen, FPS title |
| AudioBackend | `platform/audio_backend.cpp` | SDL audio device; drains the core APU ring buffer from its callback |

## 3. The M-Cycle Lockstep Design

The single most important architectural decision in this codebase:

> The CPU does not just count cycles — every M-cycle of every
> instruction drives the rest of the machine through a callback, so
> PPU/Timer/APU state changes become visible to the *same* instruction
> that reads them.

### 3.1 How it works

1. The emulator installs a hook on the CPU:

   ```cpp
   cpu.setCycleHook([this] {
       ppu.tick(mmu);                        // 4 dots (2 in CGB double speed)
       if (!cpu.stopped) {
           mmu.getTimer().tick(1, mmu);      // 4 T-cycles of timer
       }                                     // (frozen while in STOP)
       mmu.getAPU().tickT(mmu.isDoubleSpeed() ? 2 : 4);  // base T-cycles of APU
   });
   ```

2. The CPU invokes the hook exactly once per M-cycle:
   - in `read8()`/`write8()` (fetch, operand, and memory accesses),
   - after every `cycles += 4/8/12` block that represents internal
     or stack M-cycles,
   - while HALT/STOP sleeping (4 T-cycles per step).

3. Nothing else ticks the components during normal execution.

The timer advances its system counter 4 T-cycles per M-cycle in both
speed modes: in CGB double speed the CPU consumes M-cycles twice as
fast, so DIV and TIMA frequencies double exactly like the hardware's,
with no changes to the timer circuit itself. The PPU dot clock and the
APU stay in the 4.19 MHz domain, hence the 2-dot and 2-T-cycle hook
adjustments. While the CPU sleeps in STOP, the system counter is
frozen (DIV stops on both models).

### 3.2 Why it matters

blargg's `instr_timing` verifies that a timer IRQ requested
mid-instruction is observable by a register read a few M-cycles later.
With "tick everything after each instruction" the request would be
delayed by up to one instruction (4–24 T-cycles) and the test fails —
this exact failure was observed and fixed during development. The
lockstep design is what makes `instr_timing` and the STAT-choreography
of dmg-acid2 (per-line LCDC/WX rewrites via LYC interrupts) work.

### 3.3 What is deliberately *not* modeled

The hook fires at M-cycle granularity. Within an M-cycle, the exact
T-cycle at which DIV's edge occurs relative to the memory access is
not modeled. That is the (documented) reason blargg's `mem_timing-2`
does not pass — see [05-Accuracy §3](05-ACCURACY.md#3-known-limitations).

## 4. Main Loop & Frame Pacing

One frame is always 70224 PPU dots (154 scanlines × 456). In normal
speed one dot costs one CPU T-cycle; in CGB double speed two:

```cpp
const u32 frame_budget = mmu.isDoubleSpeed() ? 70224 * 2 : 70224;
while (frame_cycles < frame_budget) {
    u32 before = cpu.cycles;
    cpu.step(mmu);                       // components tick via the hook
    frame_cycles += cpu.cycles - before;
}
```

- **Windowed mode** (the loop lives in the frontend,
  `platform/main.cpp`): after each frame the framebuffer is presented
  with vsync; if the frame completed faster than 16.742 ms and vsync is
  unavailable, `SDL_Delay` throttles to 59.7275 fps. Holding **Tab**
  skips throttling and runs 5 frames per iteration.
- **Joypad interrupt**: button transitions are latched by the Joypad;
  at the start of each frame, a pending press requests IF bit 4 so the
  game can react within the same frame.
- **RTC**: the cartridge real-time clock is advanced once per frame
  (17536 M-cycles) — seconds granularity, matching how games read it.
- **Battery RAM**: written to disk on exit and whenever a save state is
  taken.

## 5. Save State Format

`.state` files are version-checked binary dumps, written in a fixed
component order:

```
"GBSTATE"          magic, 7 bytes
u8                 version (currently 2)
CPU state          A,F,B,C,D,E,H,L,SP,PC, flags byte, u32 cycles
MMU state          WRAM, VRAM, HRAM, OAM, I/O block, IE, joypad select,
                   Timer state, Cartridge state (RAM + MBC regs + RTC)
PPU state          mode, dots, scanline, window line/flags, framebuffer
APU state          registers, channel timers/lengths/volumes, sweep,
                   frame sequencer, HPF capacitors
```

Version 1 (2025) only stored CPU + MMU + cartridge; version 2 (2026)
added the previously-stubbed PPU/APU/Timer state. Loading prints a
clear error for incompatible versions instead of guessing.

## 6. Headless Mode

`runHeadless(frames, screenshotPath)` runs the same emulation loop with
no SDL video/audio initialization:

- serial output is collected and echoed to stdout at the end (this is
  how blargg ROMs report results),
- `--screenshot` writes the final framebuffer as a PPM (P6) image for
  pixel comparison (used by the dmg-acid2 check),
- battery saves are still written on completion.

Headless performance is roughly 16× real time on a modern CPU, so even
the longest test ROMs finish in seconds.

## 7. Directory Map

```
GameBoyEmulator/
├── CMakeLists.txt              # root: project, options, deps, subdirs
├── CMakePresets.json           # debug / release / release-accurate presets
├── cmake/
│   └── CompilerWarnings.cmake  # shared warning flags (INTERFACE target)
├── .github/
│   ├── workflows/ci.yml         # CI: build (3 OS) + accuracy suite
│   └── PULL_REQUEST_TEMPLATE.md
├── docs/                        # numbered documentation set (00-INDEX.md)
├── tools/
│   └── opcodes/                 # opcode spec + generator (see doc 07)
├── tests/
│   ├── CMakeLists.txt           # unit target + optional accuracy ctest
│   ├── run_tests.sh             # accuracy suite (downloads ROMs, verifies)
│   └── unit_tests.cpp           # component unit tests
├── roms/                        # your ROMs (git-ignored)
└── src/
    ├── CMakeLists.txt           # gameboy_core library + gameboy exe
    ├── core/                    # === emulation library (no SDL) ===
    │   ├── emulator.{h,cpp}     # session: frames, states, headless, battery
    │   ├── savestate.{h,cpp}    # versioned full-machine serialization
    │   ├── common/types.h       # fixed-width types, bit macros
    │   ├── cpu/
    │   │   ├── cpu.{h,cpp}      # registers, step, interrupts, cycle hook
    │   │   └── instructions.cpp # all 256 + 256 CB opcodes
    │   ├── memory/
    │   │   ├── mmu.{h,cpp}      # bus, I/O semantics, serial, DMA
    │   │   └── cartridge.{h,cpp}# MBC1/2/3/5, RTC, battery saves
    │   ├── graphics/ppu.{h,cpp}# line renderer, STAT interrupts
    │   ├── audio/apu.{h,cpp}    # 4 channels, mixer, sample ring buffer
    │   ├── timer/timer.{h,cpp} # DMG timer circuit
    │   └── input/joypad.{h,cpp}# joypad + press interrupt
    └── platform/                # === SDL2 frontend ===
        ├── main.cpp             # CLI, windowed loop, pacing, hotkeys
        ├── display.{h,cpp}      # window, input merge, gamepads
        └── audio_backend.{h,cpp}# SDL audio device draining the APU
```

Include-path convention (enforced by the public include roots, see
`src/CMakeLists.txt`):

- core-internal, cross-directory: root-relative to `src/core`
  (`#include "memory/mmu.h"`, `#include "common/types.h"`)
- same-directory: bare (`#include "cpu.h"`)
- platform and tests: prefixed (`#include "core/emulator.h"`)

---
*Next: [03 — Hardware Notes](03-HARDWARE-NOTES.md)*
