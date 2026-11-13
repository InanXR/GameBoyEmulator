# 04 — Testing

How the emulator is verified, and how to extend the verification.

**Contents**

1. [Overview](#1-overview)
2. [Unit Tests (CTest)](#2-unit-tests-ctest)
3. [The Accuracy Suite](#3-the-accuracy-suite)
4. [What Each Test Verifies](#4-what-each-test-verifies)
5. [Continuous Integration](#5-continuous-integration)
6. [Adding New Tests](#6-adding-new-tests)
7. [Manual Experiments](#7-manual-experiments)

---

## 1. Overview

Verification happens at three levels:

| Level | What | How to run |
|---|---|---|
| Unit | Timer edges, CPU flags, serial, joypad, save-state round-trips | `ctest` (or `./build/gb_unit_tests`) |
| Accuracy (ROM-based) | blargg CPU/timing suites + dmg-acid2 pixel comparison | `bash tests/run_tests.sh` |
| Smoke | Clean build on 3 OSes, warning-free compile | CI (GitHub Actions) |

The ROM-based suite is the ground truth: these are the same test ROMs
used by the major emulators, and they fail loudly on real hardware
deviations.

## 2. Unit Tests (CTest)

`tests/unit_tests.cpp` builds into `gb_unit_tests` and is registered
with CTest. It drives the components directly — no ROM required:

- **Timer**: exact 4-M-cycle increment spacing at 262144 Hz; rates for
  all TAC codes; freeze when disabled; TMA reload timing; interrupt
  arriving exactly 2 M-cycles after overflow; DIV period and DIV-write
  reset
- **CPU**: flag semantics for `XOR A`, `INC`/`SRA` edge cases
  (regression tests for two bugs that broke blargg 09/11), `DAA`
  addition and subtraction paths, `SWAP`
- **Serial**: byte captured on SC write, SC bit 7 clears, serial IRQ
  requested
- **Joypad**: P1 select-bit encoding for both button groups, press
  interrupt requested exactly once
- **Save states**: full save → mutate → load round-trip restores CPU,
  WRAM and timer state

Run:

```bash
cmake --build build && (cd build && ctest --output-on-failure)
# or by label:
ctest --test-dir build -L unit
```

CTest tests carry labels: `unit` (the checks above) and `accuracy`
(the ROM-downloading suite, only registered when CMake is configured
with `-DGB_DOWNLOAD_TESTS=ON` so that a plain `ctest` stays offline):

```bash
cmake -B build/release-accurate -S . -DCMAKE_BUILD_TYPE=Release -DGB_DOWNLOAD_TESTS=ON
ctest --test-dir build/release-accurate --output-on-failure   # unit + accuracy
```

## 3. The Accuracy Suite

`tests/run_tests.sh` is the one-command verification:

```bash
bash tests/run_tests.sh [path/to/gameboy]
```

By default it uses `build/gameboy`. ROMs are downloaded on first run
from the freely redistributable sources into `.test-roms/` (or
`$GB_TEST_ROM_DIR`) and cached:

- **Blargg cpu_instrs, individual ROMs 01–11** — full CPU verification.
  The ROM prints its progress over the serial port; the script looks
  for `Passed`/`Failed` in the output.
- **Blargg instr_timing** — M-cycle-exact instruction timing, verified
  through the timer (see [02-Architecture §3](02-ARCHITECTURE.md#3-the-m-cycle-lockstep-design)).
- **dmg-acid2** — PPU rendering. Runs 120 frames headless, dumps the
  framebuffer as a PPM, and compares it to the official reference image
  **pixel by pixel** (requires python3 + Pillow; skipped with a notice
  otherwise).
- **halt_bug.gb** — a survey ROM with no pass/fail verdict; the script
  verifies it runs to completion and reports it as `INFO`.

Output on a healthy tree:

```
PASS  01-special.gb
...
PASS  instr_timing.gb
INFO  halt_bug.gb (no verdict; survey ROM ran to completion)
PASS  dmg-acid2.gb (pixel-perfect)
==============================================
 Results: 13 passed, 0 failed
 All tests passed!
```

Environment knobs: `GB_TEST_FRAMES` (frame budget, default 30000),
`GB_TEST_ROM_DIR` (ROM cache location). Every download is validated
(minimum header size, `00 C3` entry-point bytes at offset 0x0100, PNG
magic for the reference image) before use, so a broken URL or a
redirect-to-HTML failure is reported loudly instead of producing a
mysterious emulator crash.

## 4. What Each Test Verifies

| Test | Exercises |
|---|---|
| `01-special` | ADD/ADC/SUB/SBC/CP flag behavior, DAA, RLCA/RLA/RRCA/RRA edge cases |
| `02-interrupts` | HALT wake + immediate dispatch pushing the correct resume PC, ISR ordering, RETI, EI delay |
| `03-op sp,hl` — `07-jr,jp,call,ret,rst` | The respective opcode groups, including illegal-opcode behavior in 08 |
| `08-misc instrs` | SWAP, DAA context, illegal opcodes |
| `09-op r,r`, `11-op a,(hl)` | Full ALU matrices incl. `SRA` sign preservation (regression: a one-line `bool` MSB bug once broke both) |
| `10-bit ops` | All CB bit/rotate/shift ops incl. `(HL)` variants |
| `instr_timing` | Every instruction's cycle count via timer-measured deltas; requires the falling-edge timer model *and* lockstep component ticking |
| `dmg-acid2` | Window (2 tile maps, internal line counter, mid-frame WX/LCDC), sprite priority (X, OAM, BG-flag, 10-sprite limit, 8×16 index masking), LYC/STAT interrupt choreography, LCD-off blanking, signed tile addressing |
| `halt_bug.gb` | HALT-bug trigger matrix across IE/IF combinations (informational) |

## 5. Continuous Integration

`.github/workflows/ci.yml` runs on every push/PR:

1. **Build matrix** — Ubuntu, macOS, Windows (vcpkg), Release builds.
2. **Accuracy job** — Ubuntu: builds, runs `ctest` and
   `tests/run_tests.sh`; the full 13-test suite must stay green.

Keep CI green: if a change intentionally alters timing or rendering,
expect `instr_timing` or `dmg-acid2` to fail *only if accuracy
regressed* — both are hard requirements, not aspirations.

## 6. Adding New Tests

**Unit test** — append a `test_*` function to
`tests/unit_tests.cpp` using the `check(cond, "description")` helper
and call it from `main()`. Components can be constructed standalone
(`MMU mmu; CPU cpu;`) and driven directly.

**ROM test** — extend the `TESTS` array in `tests/run_tests.sh` with
`"name|url-encoded-filename"`. The downloader validates every file
(starts with the `00 C3` entry-point bytes, minimum header size) and
deletes bad downloads, so a broken URL fails loudly instead of
mysteriously.

**Graphical comparison** — follow the dmg-acid2 block: run headless
with `--screenshot`, then compare against a reference with Pillow (map
RGB to the four DMG shades by luminance, since the exact channel
values differ between palettes).

## 7. Manual Experiments

Useful for debugging without a window:

```bash
# Any ROM, N frames, serial output to stdout
./build/gameboy rom.gb --headless 2400

# Render the last frame to an image
./build/gameboy rom.gb --headless 120 --screenshot out.ppm

# Convert to PNG for viewing
python3 -c "from PIL import Image; Image.open('out.ppm').save('out.png')"
```

A windowed smoke test on machines without a display:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 5 ./build/gameboy rom.gb
```

(Exit code 124 = still running when the timeout hit = good.)

---
*Next: [05 — Accuracy Status](05-ACCURACY.md)*
