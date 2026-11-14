# 05 — Accuracy Status

What is verified, how, and where the known limits are.

**Contents**

1. [Verification Matrix](#1-verification-matrix)
2. [Notes on Individual Results](#2-notes-on-individual-results)
3. [Known Limitations](#3-known-limitations)

---

## 1. Verification Matrix

| Suite | Scope | Result | Verified by |
|---|---|---|---|
| blargg `cpu_instrs` 01–11 | Full instruction set + interrupts | **11/11 pass** | `tests/run_tests.sh` |
| blargg `instr_timing` | Per-opcode M-cycle timing | **pass** | `tests/run_tests.sh` |
| `dmg-acid2` | PPU rendering (window, sprite priority, STAT choreography) | **pixel-perfect** (0/23040 px differ) | `tests/run_tests.sh` |
| `halt_bug.gb` | HALT-bug trigger matrix over IE/IF | runs to completion; matrix matches hardware expectations | `tests/run_tests.sh` (INFO) |
| `cgb-acid2` | CGB PPU rendering (palettes, attributes, banks, priority, HDMA) | **pixel-perfect** (0/23040 px, exact RGB) | `tests/run_tests.sh` |
| blargg `mem_timing-2` | T-cycle-precise memory access timing (read, write, modify) | **pass** (verdict read from the ROM's battery RAM) | `tests/run_tests.sh` |
| Unit tests | Timer edges, CPU flags, serial, joypad, CGB registers/banking/HDMA/speed, save states | **99/99 pass** | `ctest` |
| blargg `dmg_sound` / `cgb_sound` | APU register-level behavior | not run | — |
| Blargg `oam_bug` | OAM corruption bug | not implemented, not run | — |

Performance: ~16× real time headless (≈990 fps on a modern desktop
core), so windowed emulation holds 59.7 fps with ample headroom.

## 2. Notes on Individual Results

### 2.1 cpu_instrs: 8/11 → 11/11

The 2025 codebase passed 8/11. The three failures each traced to a
single root cause:

- `02-interrupts` — interrupt dispatch after HALT woke pushed the
  wrong resume address (dispatch happened one instruction late).
- `09-op r,r` + `11-op a,(hl)` — `SRA` stored the preserved MSB in a
  `bool`, which promoted to `1` instead of `0x80`, so every
  arithmetic-right-shift of a value with bit 7 set corrupted bit 0.

### 2.2 instr_timing

Two architectural fixes were required:

1. A full pass over every conditional-opcode cycle count (notably
   `RET cc` not-taken = 2 M-cycles, not 1) — blargg's delay macros are
   built out of `ret cc` and their arithmetic is exact.
2. Lockstep component ticking (the CPU cycle hook): a timer IRQ
   raised mid-instruction must be readable by a later M-cycle of the
   *same* instruction. Ticking components once per instruction loses
   that and fails "Timer doesn't work properly".

### 2.3 dmg-acid2

Reaching 0/23040 required, in order:

1. PPU-internal STAT writes (mode bits, coincidence) bypassing the
   CPU-facing write mask — otherwise the coincidence flag froze at its
   power-on value and no STAT interrupt ever fired (9929 px wrong).
2. Re-evaluating the LYC=LY coincidence every M-cycle so LYC writes
   take effect immediately (1439 px wrong).
3. Clearing the per-line sprite buffers even when sprites are disabled
   for that line, which was leaking row N−1's mouth sprites into
   rows with OBJ off.

### 2.4 halt_bug.gb

This ROM is not a pass/fail test: it surveys when the HALT bug fires
across a matrix of IE/IF values and renders the results as an on-screen
table (9 rows). The emulator runs it to completion; the recorded matrix
shows the bug firing exactly when `IE & IF & 0x1F != 0`, which matches
the DMG behavior described by Gekkio (the interrupt-controller lines
only exist for bits 0–4, so upper IF/IE bits alone must not trigger
it).

### 2.5 mem_timing-2 (passes at M-cycle granularity)

These ROMs time memory accesses against a 64-cycle TIMA tick to
T-cycle resolution, using sync loops that walk the access phase in
1-cycle steps. They were long documented here as "not passing" on the
assumption that M-cycle-granular timing could not resolve them — an
assumption that had never actually been tested. Running the suite for
real shows all three tests (read, write, modify timing) passing on
both DMG and CGB models: the sync loops converge on M-cycle phases,
and every access/edge alignment the suite probes resolves correctly
in the lockstep design.

The verdict is not printed over serial: the newer blargg framework
parks with the result in battery-backed cartridge RAM (result byte at
`$A000`, signature `$DEB061`, text report at `$A004`), which the
test script reads back from the emulator's `.sav` file.

What genuinely remains out of reach without sub-M-cycle modeling is
the DIV-APU coupling (DIV writes clocking the sound frame sequencer
early — blargg `dmg_sound`) and mooneye-grade T-cycle timer edge
tests; no commercial game depends on either.

## 3. Known Limitations

1. **CGB DMA billing** — GDMA and HBlank DMA transfers are
   instantaneous; hardware stalls the CPU ~8 M-cycles per 16-byte
   block. Data placement, readback and completion flags are correct,
   but the CPU is not charged the transfer time.
2. **DMG STOP LCD-off** — hardware blanks the LCD during STOP; the
   emulator keeps rendering (the DIV freeze is emulated).
3. **No link cable** — the serial port is loopback-to-host only
   (sufficient for test ROMs; no Game Link network play).
4. **OAM corruption bug** (writing OAM during mode 1/2 corrupts
   sprite data on real DMG) is not emulated — writes simply take
   effect.
5. **Wave RAM trigger corruption** (DMG wave-RAM clobber when
   retriggering CH3 mid-read) is not emulated.
6. **Mode 3 length is fixed** at 172 T-cycles — real hardware varies
   it with sprite/scroll load; line-based renderers like this one
   don't need the variation for any known game.
7. **DIV-APU is rate-modeled**, not edge-coupled: the frame sequencer
   advances at the correct 512 Hz, but a DIV write does not send a
   real falling-edge pulse into it (blargg `dmg_sound` and mooneye
   T-cycle timer tests would require that plus sub-M-cycle phases).
8. **The 11 illegal opcodes** execute as NOPs with reasonable timing,
   rather than locking up the CPU as some hardware revisions do.

---
*Next: [06 — References](06-REFERENCES.md)*
