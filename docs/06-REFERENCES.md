# 06 — References

The sources this emulator's behavior is built on. When documentation
and test ROMs disagreed, the test ROMs won.

**Contents**

1. [Primary Hardware Documentation](#1-primary-hardware-documentation)
2. [Test ROMs](#2-test-roms)
3. [Further Reading](#3-further-reading)

---

## 1. Primary Hardware Documentation

| Source | Link | Used for |
|---|---|---|
| **Pan Docs** | <https://gbdev.io/pandocs/> | The backbone reference: memory map, register semantics, MBC1/2/3/5, timer circuit and obscure behaviors, STAT/LCDC, window behavior, audio registers + audio details (DIV-APU, sweep algorithm, DACs, high-pass filter), power-on state |
| **Gekkio's "The Game Boy: a hardware autopsy" (gbctr)** | <https://gekkio.fi/files/gb-docs/gbctr.pdf> | Interrupt dispatch internals, HALT/HALT-bug wake conditions, interrupt lines existing only for IF/IE bits 0–4 |
| Gekkio's DMG schematics analysis | <https://gekkio.fi/gb-ctr/> | Timer falling-edge circuit confirmation |

Specific Pan Docs chapters that shaped the implementation:

- `Timer_Obscure_Behaviour` — the system counter, DIV↔TIMA falling
  edge coupling, write-to-DIV/TAC edge cases
- `Audio_details` — DIV-APU 512 Hz sequencer, channel trigger
  semantics, sweep shadow register algorithm, DAC rules, mixer + HPF
  reference implementation
- `MBC1` — the two banking modes and the mode-1 remapping of
  0000-3FFF (banks 20/40/60 reachability)
- `Window` — WX/WY validity ranges, the internal line counter
- `halt` — HALT wake vs. dispatch, the halt bug
- `Power_Up_Sequence` — DMG register power-on values

## 2. Test ROMs

| ROM | Source | License status |
|---|---|---|
| blargg `cpu_instrs` (individual 01–11), `instr_timing`, `halt_bug.gb` | <https://github.com/retrio/gb-test-roms> | Freely redistributable test ROMs (Shay Green / retrio mirror) |
| `dmg-acid2` + reference image | <https://github.com/mattcurrie/dmg-acid2> | MIT; the reference-dmg.png is the pixel-exact target the suite compares against |
| `cgb-acid2` + reference image | <https://github.com/mattcurrie/cgb-acid2> | MIT; the reference.png is the exact-RGB target the CGB suite compares against |

The test suite downloads these on demand; nothing copyrighted is ever
committed to this repository.

## 3. Further Reading

Useful material consulted during development, and for anyone
continuing it:

- **Awesome Game Boy Development** — <https://github.com/gbdev/awesome-gbdev>
  — index of documentation, emulators, and tools
- **gbdev.io** — community hub and chat for Game Boy development
- **SameBoy** — <https://github.com/LIJI32/SameBoy> — a high-accuracy
  reference emulator; its handling of APU corner cases and the sameboy
  test suite (SameSuite) are valuable comparisons
- **Gambatte** — <https://github.com/gb-archive/gambatte> — the
  long-standing accuracy benchmark
- **gameboy-doctor** — <https://github.com/robert/gameboy-doctor> —
  per-instruction expected register values for early boot debugging
- **Blargg's testing philosophy** — the `readme.txt` files inside the
  test ROM source trees document how the timing tests synchronize to
  the timer, which is what makes them such a precise instrument

### 3.1 Why serial output matters

Blargg's ROM framework prints everything on screen *and* to the serial
port (write the character to SB, then 0x81 to SC). Emulating that
one-register handshake gave this project automated verification for
nearly free — the headless mode exists largely to exploit it. Any new
test ROM that follows the same convention works with
`tests/run_tests.sh` unchanged.

---
*Next: [07 — Dev Tools](07-DEV-TOOLS.md)*
