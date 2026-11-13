# 07 — Dev Tools

Developer tooling that ships alongside the emulator source.

**Contents**

1. [Overview](#1-overview)
2. [Opcode Generator](#2-opcode-generator)
3. [Opcode Specification](#3-opcode-specification)
4. [Workflow](#4-workflow)

---

## 1. Overview

This directory contains two maintenance tools plus the numbered
documentation set:

| File | Purpose |
|---|---|
| `tools/opcodes/generate_opcodes.py` | Generate instruction dispatch skeletons from the opcode specification |
| `tools/opcodes/Opcodes.json` | Machine-readable specification of all 512 opcodes |
| `00-INDEX.md` … `06-REFERENCES.md` | The documentation set (see [00-INDEX](00-INDEX.md)) |

These tools are **not** required to build or run the emulator; they are
kept for reference and future regeneration work.

## 2. Opcode Generator

Generates C++ dispatch skeletons from `Opcodes.json`:

```bash
cd tools/opcodes
python3 generate_opcodes.py
```

The generator was used during initial development to scaffold
`src/core/cpu/instructions.cpp`. Treat its output as a starting point — the
checked-in implementation is hand-tuned for M-cycle-accurate timing
(see [03-Hardware Notes §1.2](03-HARDWARE-NOTES.md#12-cycle-accounting)),
so never overwrite the live source with generated code.

## 3. Opcode Specification

`Opcodes.json` describes all 512 opcodes (256 base + 256 CB-prefixed):

- mnemonic
- operands
- byte length
- cycle count (T-cycles — divide by 4 for the M-cycle counts used in
  the docs)
- flags affected

Source: derived from Pan Docs and the Game Boy programming community
opcode tables. It was cross-checked against blargg's verified timing
tables from the `instr_timing` readme.

## 4. Workflow

1. **Scaffold**: extend `Opcodes.json` if a new variant is ever needed,
   then run `generate_opcodes.py`.
2. **Implement**: fill in the real semantics in
   `src/core/cpu/instructions.cpp`, including the cycle hook calls
   (`tickHook()`) for every internal M-cycle.
3. **Verify**: the suite must stay green — see
   [04-Testing](04-TESTING.md):

   ```bash
   (cd build && ctest --output-on-failure)
   bash tests/run_tests.sh
   ```

---
*Back to [00 — Index](00-INDEX.md)*
