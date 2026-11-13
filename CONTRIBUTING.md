# Contributing

Thanks for contributing! This project is an accuracy-first Game Boy
emulator; the bar for changes is **the verification suite stays green**.

## 1. Ground Rules

1. Build clean: zero warnings under `-Wall -Wextra -Wshadow
   -Wold-style-cast` (enforced by the `gb_warnings` target).
2. `ctest` passes (30 unit checks).
3. `tests/run_tests.sh` passes (13 accuracy tests, including the
   dmg-acid2 pixel comparison). If a change intentionally alters
   timing or rendering behavior, it must come with evidence that the
   new behavior matches hardware (see `docs/06-REFERENCES.md`).
4. Documentation stays in sync: behavior changes update
   `docs/03-HARDWARE-NOTES.md`, test-suite changes update
   `docs/04-TESTING.md`, and user-visible changes update `README.md`
   and `CHANGELOG.md`.

## 2. Repository Layout

```
src/core/      headless emulation library (gameboy_core): no SDL, no UI
src/platform/  SDL2 frontend: window, input, audio device, main loop
tests/         unit tests + ROM-based accuracy suite
cmake/         shared CMake helper modules
docs/          numbered documentation set (start at 00-INDEX.md)
tools/opcodes/ opcode specification + code generator
```

The core/platform boundary is strict: `src/core` must never include
SDL headers. Platform code reaches the core through the public include
roots (`core/...`), core code stays root-relative (`memory/mmu.h`).

## 3. Building & Testing

```bash
# classic flow
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && ctest --output-on-failure)
bash tests/run_tests.sh

# or with presets (CMake >= 3.21)
cmake --preset release
cmake --build --preset release
ctest --preset release-accurate    # includes the ROM-downloading suite
```

## 4. Style

- Formatting: `.clang-format` (4-space indents, attached braces,
  100-column limit, no include sorting). Run `clang-format -i` on
  touched files.
- Includes: same-directory headers are bare (`#include "cpu.h"`);
  cross-directory core includes are root-relative
  (`#include "memory/mmu.h"`); platform/test code prefixes with the
  library root (`#include "core/emulator.h"`).
- Editor settings: `.editorconfig`.
- Cycles are always documented in T-cycles or M-cycles explicitly;
  hardware reasoning is written in comments and cross-referenced to
  Pan Docs.

## 5. Commits & Pull Requests

- Commits: imperative subject line (`Fix TIMA reload timing`), body
  explains *why* when the reason isn't obvious.
- PRs: fill in the pull request template checklist.
