# GameBoy Emulator - Development Notes

## Purpose
This directory contains development tools and documentation for maintaining the emulator.

## Files

### `generate_opcodes.py`
Python script to generate opcode implementations from the `Opcodes.json` specification.

**Usage**:
```bash
python generate_opcodes.py
```

This tool was used during initial development to scaffold the CPU instruction implementations.

### `Opcodes.json`
Complete GameBoy opcode specification in JSON format. Contains all 512 opcodes (256 main + 256 CB-prefixed) with:
- Mnemonic
- Operands
- Byte length
- Cycle count
- Flags affected

**Source**: Derived from GameBoy technical documentation and Pan Docs.

## Development Workflow

1. **Opcode Generation**: Use `generate_opcodes.py` with `Opcodes.json` to generate skeleton code
2. **Implementation**: Fill in the actual CPU logic in `src/cpu/instructions.cpp`
3. **Testing**: Validate with Blargg's test ROMs

## Notes
- These files are kept for reference and potential future updates
- Not required for building or running the emulator
- Excluded from release builds
