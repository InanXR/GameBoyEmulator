# GameBoy Emulator — Documentation Index

Welcome to the GameBoy Emulator documentation. The documents below are
numbered and organized to be read in order; each is self-contained and
cross-referenced where relevant.

| # | Document | Contents |
|---|----------|----------|
| 01 | [Getting Started](01-GETTING-STARTED.md) | Prerequisites, building on every platform, running, CLI options, controls and hotkeys |
| 02 | [Architecture](02-ARCHITECTURE.md) | How the codebase is organized, how components connect, the M-cycle lockstep design, data flow |
| 03 | [Hardware Notes](03-HARDWARE-NOTES.md) | The DMG hardware behavior implemented in the emulator, subsystem by subsystem, with references |
| 04 | [Testing](04-TESTING.md) | The automated accuracy suite, unit tests, CI, and how to add new tests |
| 05 | [Accuracy Status](05-ACCURACY.md) | Verification matrix, what passes, known limitations and why |
| 06 | [References](06-REFERENCES.md) | Source material: Pan Docs, Gekkio's gbctr, blargg's test ROMs, dmg-acid2/cgb-acid2 |
| 07 | [Dev Tools](07-DEV-TOOLS.md) | The opcode generation tooling and opcode specification tables |

## Quick Paths

- **"I want to build and play"** → [01-Getting Started](01-GETTING-STARTED.md)
- **"I want to understand the code"** → [02-Architecture](02-ARCHITECTURE.md)
- **"Which hardware quirks are emulated?"** → [03-Hardware Notes](03-HARDWARE-NOTES.md)
- **"How do I run the tests?"** → [04-Testing](04-TESTING.md)
- **"How accurate is it, really?"** → [05-Accuracy Status](05-ACCURACY.md)

## Conventions

- All cycle counts use Game Boy terminology: **T-cycles** (4.194304 MHz
  dot clock) and **M-cycles** (1.048576 MHz machine cycles, 4 T-cycles
  each) unless stated otherwise.
- Register names follow Pan Docs (`LCDC`, `STAT`, `NR52`, `TAC`, ...).
- Hexadecimal values are written `0xNN`, memory ranges use Pan Docs
  notation (`0000-3FFF`).

---
*Documentation set: November 2026. Keep these files in sync with the
source code; PRs that change emulator behavior should update the
relevant document.*
