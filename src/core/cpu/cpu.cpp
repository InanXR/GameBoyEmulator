#include "cpu.h"
#include "memory/mmu.h"
#include <iostream>

/**
 * CPU Implementation
 * Sharp LR35902 (modified Z80)
 */

CPU::CPU() {
    reset();
}

void CPU::reset(bool cgb_mode) {
    // Register state right after the boot ROM hands control to the
    // cartridge (Pan Docs: Power-Up Sequence). DMG and CGB boot ROMs
    // leave different values; A is the model identifier ($01 DMG,
    // $11 CGB).
    if (cgb_mode) {
        A = 0x11;
        F = 0x80;   // Z=1, N=0, H=0, C=0
        B = 0x00;
        C = 0x00;
        D = 0xFF;
        E = 0x56;
        H = 0x00;
        L = 0x0D;
    } else {
        A = 0x01;   // GameBoy model identifier
        F = 0xB0;   // Flags: Z=1, N=0, H=1, C=1
        B = 0x00;
        C = 0x13;
        D = 0x00;
        E = 0xD8;
        H = 0x01;
        L = 0x4D;
    }

    SP = 0xFFFE;  // Stack pointer at top of RAM
    PC = 0x0100;  // Start of cartridge ROM (skip BIOS)

    halted = false;
    stopped = false;
    ime = false;        // Interrupts disabled initially
    ime_scheduled = false;
    halt_bug = false;
    stop_speed_switch = false;
    stop_pause = 0;
    cycles = 0;
}

void CPU::step(MMU& mmu) {
    // EI delay: IME becomes set before the instruction following EI
    // executes, so an interrupt can only be serviced after it.
    if (ime_scheduled) {
        ime = true;
        ime_scheduled = false;
    }

    // STOP mode: the CPU sleeps until a joypad press (IF bit 4) or,
    // for a CGB speed switch, until the ~2050 M-cycle switch pause
    // elapses. The system counter (DIV) stays frozen the whole time -
    // the emulator's cycle hook skips timer ticks while stopped.
    if (stopped) {
        cycles += 4;
        tickHook();
        if (stop_speed_switch) {
            if (--stop_pause == 0) {
                mmu.applySpeedSwitch();
                stopped = false;
                stop_speed_switch = false;
            }
            return;
        }
        u8 IF = mmu.read(0xFF0F) & 0x1F;
        if (IF & 0x10) {
            stopped = false;
        }
        return;
    }

    // If CPU is halted, wait for an interrupt enabled in IE to be
    // flagged in IF (regardless of IME - IME only gates dispatch).
    if (halted) {
        cycles += 4;
        tickHook();
        u8 IF = mmu.read(0xFF0F) & 0x1F;
        u8 IE = mmu.read(0xFFFF);

        if ((IF & IE) != 0) {
            halted = false;
            if (ime) {
                // With IME set, the pending interrupt is dispatched
                // immediately; the dispatch pushes the address of the
                // instruction after HALT (current PC), before anything
                // else executes.
                handleInterrupts(mmu);
            }
        }
        return;
    }

    // Fetch opcode at PC
    u8 opcode = read8(mmu, PC);
    if (halt_bug) {
        halt_bug = false;  // HALT bug: the fetched byte is re-read
    } else {
        PC++;
    }

    // Execute the instruction
    executeOpcode(opcode, mmu);

    // Service interrupts (between instructions on real hardware)
    if (ime) {
        handleInterrupts(mmu);
    }
}

// ============================================
// MEMORY ACCESS (with cycle counting)
// ============================================

u8 CPU::read8(MMU& mmu, u16 addr) {
    cycles += 4;  // Memory read takes 4 cycles (1 M-cycle)
    tickHook();
    return mmu.read(addr);
}

void CPU::write8(MMU& mmu, u16 addr, u8 value) {
    cycles += 4;  // Memory write takes 4 cycles
    tickHook();
    mmu.write(addr, value);
}

u16 CPU::read16(MMU& mmu, u16 addr) {
    // GameBoy is little-endian: low byte first, high byte second
    u8 lo = read8(mmu, addr);
    u8 hi = read8(mmu, addr + 1);
    return (static_cast<u16>(hi) << 8) | lo;
}

void CPU::write16(MMU& mmu, u16 addr, u16 value) {
    // Write low byte first, then high byte (little-endian)
    write8(mmu, addr, value & 0xFF);
    write8(mmu, addr + 1, value >> 8);
}

// ============================================
// STACK OPERATIONS
// ============================================

void CPU::push16(MMU& mmu, u16 value) {
    SP -= 2;
    write16(mmu, SP, value);
}

u16 CPU::pop16(MMU& mmu) {
    u16 value = read16(mmu, SP);
    SP += 2;
    return value;
}

// ============================================
// INTERRUPT HANDLING
// ============================================

void CPU::handleInterrupts(MMU& mmu) {
    u8 IF = mmu.read(0xFF0F) & 0x1F;  // Interrupt Flag register
    u8 IE = mmu.read(0xFFFF);         // Interrupt Enable register

    // Check which interrupts are both flagged and enabled
    u8 triggered = IF & IE;

    if (triggered == 0) return;  // No interrupts to handle

    // Process interrupts in priority order:
    // VBlank > LCD > Timer > Serial > Joypad
    for (int i = 0; i < 5; i++) {
        if (triggered & (1 << i)) {
            // Service this interrupt
            halted = false;  // Wake from HALT if halted

            // Clear the interrupt flag
            mmu.write(0xFF0F, static_cast<u8>(IF & ~(1 << i)));

            // Push current PC to stack
            push16(mmu, PC);

            // Jump to interrupt vector
            // VBlank: 0x40, LCD: 0x48, Timer: 0x50, Serial: 0x58, Joypad: 0x60
            PC = static_cast<u16>(0x0040 + i * 0x08);

            ime = false;      // Disable further interrupts

            // Interrupt dispatch takes 5 M-cycles total; the two stack
            // writes already ticked 2, so account for the remaining 3.
            cycles += 12;
            tickHook();
            tickHook();
            tickHook();
            break;  // Only service one interrupt per step
        }
    }
}

// ============================================
// BASIC INSTRUCTION IMPLEMENTATIONS
// ============================================

void CPU::NOP() {
    // Do nothing - 4 cycles already counted by read8
}

void CPU::HALT(MMU& mmu) {
    u8 IF = mmu.read(0xFF0F) & 0x1F;
    u8 IE = mmu.read(0xFFFF);

    if (!ime && (IF & IE) != 0) {
        // HALT bug: interrupts are pending but IME is 0, so HALT fails
        // and the CPU keeps executing - with PC failing to increment
        // once (the byte after HALT is fetched and executed twice).
        halt_bug = true;
        return;
    }

    halted = true;
    // CPU enters low-power mode until an enabled interrupt is flagged
}

void CPU::STOP(MMU& mmu) {
    // CGB speed switch: STOP after arming KEY1 bit 0 toggles the CPU
    // clock speed after a ~2050 M-cycle pause (DIV frozen meanwhile).
    if (mmu.isCGB() && mmu.speedSwitchArmed()) {
        stop_speed_switch = true;
        stop_pause = 2050;
        stopped = true;
        return;
    }
    // Low power mode; the CPU sleeps until a joypad press sets IF
    // bit 4. The byte after the opcode is skipped (handled at the
    // call site), and the system counter stays frozen while stopped.
    stop_speed_switch = false;
    stopped = true;
}

void CPU::DI() {
    ime = false;  // Disable interrupts
}

void CPU::EI() {
    // IME is enabled after the NEXT instruction (1 instruction delay)
    ime_scheduled = true;
}

// ============================================
// 8-BIT LOADS
// ============================================

void CPU::LD_r_r(u8& dest, u8 src) {
    dest = src;
}

void CPU::LD_r_n(u8& dest, u8 n) {
    dest = n;
}

void CPU::LD_r_HL(u8& dest, MMU& mmu) {
    dest = read8(mmu, getHL());
}

void CPU::LD_HL_r(MMU& mmu, u8 src) {
    write8(mmu, getHL(), src);
}

void CPU::LD_HL_n(MMU& mmu, u8 n) {
    write8(mmu, getHL(), n);
}

// ============================================
// 8-BIT ARITHMETIC
// ============================================

void CPU::ADD_A_r(u8 value) {
    u16 result = A + value;
    
    // Half carry: check if bit 3 -> 4 carried
    setFlag(FLAG_H, ((A & 0x0F) + (value & 0x0F)) > 0x0F);
    
    // Carry: check if result > 255
    setFlag(FLAG_C, result > 0xFF);
    
    A = result & 0xFF;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);  // Addition, not subtraction
}

void CPU::ADC_A_r(u8 value) {
    u8 carry = getFlag(FLAG_C) ? 1 : 0;
    u16 result = A + value + carry;
    
    setFlag(FLAG_H, ((A & 0x0F) + (value & 0x0F) + carry) > 0x0F);
    setFlag(FLAG_C, result > 0xFF);
    
    A = result & 0xFF;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
}

void CPU::SUB_A_r(u8 value) {
    // Half carry (borrow): check if lower nibble borrows
    setFlag(FLAG_H, (A & 0x0F) < (value & 0x0F));
    
    // Carry (borrow): check if A < value
    setFlag(FLAG_C, A < value);
    
    A -= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, true);  // Subtraction flag
}

void CPU::SBC_A_r(u8 value) {
    u8 carry = getFlag(FLAG_C) ? 1 : 0;
    
    // Calculate half-carry (borrow from bit 4)
    // Must check if lower nibble of A is less than (lower nibble of value + carry)
    setFlag(FLAG_H, (A & 0x0F) < ((value & 0x0F) + carry));
    
    // Calculate carry (borrow) - check if A < (value + carry)
    setFlag(FLAG_C, A < (value + carry));
    
    A = A - value - carry;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, true);
}

void CPU::AND_A_r(u8 value) {
    A &= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, true);   // Always set for AND
    setFlag(FLAG_C, false);
}

void CPU::OR_A_r(u8 value) {
    A |= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, false);
}

void CPU::XOR_A_r(u8 value) {
    A ^= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, false);
}

void CPU::CP_A_r(u8 value) {
    // Compare (SUB without storing result)
    setFlag(FLAG_Z, A == value);
    setFlag(FLAG_N, true);
    setFlag(FLAG_H, (A & 0x0F) < (value & 0x0F));
    setFlag(FLAG_C, A < value);
}

void CPU::INC_r(u8& reg) {
    // Half carry: check if incrementing bit 3 causes carry to bit 4
    setFlag(FLAG_H, (reg & 0x0F) == 0x0F);
    
    reg++;
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    // Carry flag unaffected
}

void CPU::DEC_r(u8& reg) {
    // Half carry (borrow): check if decrementing bit 4 causes borrow from bit 3
    setFlag(FLAG_H, (reg & 0x0F) == 0);
    
    reg--;
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, true);
    // Carry flag unaffected
}

// ============================================
// JUMP INSTRUCTIONS
// ============================================

void CPU::JP_nn(u16 addr) {
    PC = addr;
}

void CPU::JP_cc_nn(bool condition, u16 addr) {
    if (condition) {
        PC = addr;
        cycles += 4;  // Extra cycle for taken branch
        tickHook();
    }
}

void CPU::JR_n(i8 offset) {
    PC += offset;
    cycles += 4;  // JR always takes extra cycle
    tickHook();
}

void CPU::JR_cc_n(bool condition, i8 offset) {
    if (condition) {
        PC += offset;
        cycles += 4;  // Extra cycle for taken branch
        tickHook();
    }
}

// ============================================
// CALL AND RETURN
// ============================================

void CPU::CALL_nn(MMU& mmu, u16 addr) {
    push16(mmu, PC);
    PC = addr;
    cycles += 4;  // 6 M-cycles: fetch + operand + internal delay + push
    tickHook();
}

void CPU::CALL_cc_nn(MMU& mmu, bool condition, u16 addr) {
    if (condition) {
        push16(mmu, PC);
        PC = addr;
        cycles += 4;  // taken: same 6 M-cycles as CALL nn
        tickHook();
    }
}

void CPU::RET(MMU& mmu) {
    PC = pop16(mmu);
    cycles += 4;  // 4 M-cycles: fetch + pop + internal delay
    tickHook();
}

void CPU::RET_cc(MMU& mmu, bool condition) {
    if (condition) {
        PC = pop16(mmu);
        cycles += 8;  // taken: 5 M-cycles (fetch + condition + pop + set PC)
        tickHook();
        tickHook();
    } else {
        cycles += 4;  // not taken: 2 M-cycles (fetch + internal condition check)
        tickHook();
    }
}

void CPU::RETI(MMU& mmu) {
    PC = pop16(mmu);
    ime = true;   // RETI re-enables interrupts immediately
    cycles += 4;  // 4 M-cycles like RET
    tickHook();
}

void CPU::RST(MMU& mmu, u8 vector) {
    push16(mmu, PC);
    PC = vector;  // Jump to fixed vector (0x00, 0x08, 0x10, ..., 0x38)
    cycles += 4;  // 4 M-cycles: fetch + internal delay + push
    tickHook();
}

// ============================================
// 16-BIT ARITHMETIC
// ============================================

void CPU::ADD_HL_rr(u16 value) {
    u32 result = getHL() + value;

    setFlag(FLAG_N, false);
    setFlag(FLAG_H, ((getHL() & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF);
    setFlag(FLAG_C, result > 0xFFFF);

    setHL(result & 0xFFFF);
    cycles += 4;
    tickHook();
}

// ============================================
// ROTATE OPERATIONS
// ============================================

void CPU::RRCA() {
    bool carry = (A & 0x01) != 0;
    A = (A >> 1) | (carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::RLA() {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (A & 0x80) != 0;
    
    A = (A << 1) | (old_carry ? 1 : 0);
    
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

void CPU::RRA() {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (A & 0x01) != 0;
    
    A = (A >> 1) | (old_carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

// ============================================
// CB PREFIX ROTATE/SHIFT OPERATIONS
// ============================================

void CPU::RRC_r(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg = (reg >> 1) | (carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::RL_r(u8& reg) {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (reg & 0x80) != 0;
    
    reg = (reg << 1) | (old_carry ? 1 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

void CPU::RR_r(u8& reg) {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (reg & 0x01) != 0;
    
    reg = (reg >> 1) | (old_carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

void CPU::SLA_r(u8& reg) {
    bool carry = (reg & 0x80) != 0;
    reg <<= 1;
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::SRA_r(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    u8 msb = reg & 0x80;        // preserve the sign bit
    reg = static_cast<u8>((reg >> 1) | msb);  // arithmetic shift

    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::SRL_r(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg >>= 1;  // Logical shift (MSB becomes 0)
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::SWAP_r(u8& reg) {
    reg = ((reg & 0x0F) << 4) | ((reg & 0xF0) >> 4);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, false);
}

// ============================================
// SAVE STATE
// ============================================

void CPU::saveState(std::ofstream& file) const {
    // Save all registers
    file.write(reinterpret_cast<const char*>(&A), sizeof(A));
    file.write(reinterpret_cast<const char*>(&F), sizeof(F));
    file.write(reinterpret_cast<const char*>(&B), sizeof(B));
    file.write(reinterpret_cast<const char*>(&C), sizeof(C));
    file.write(reinterpret_cast<const char*>(&D), sizeof(D));
    file.write(reinterpret_cast<const char*>(&E), sizeof(E));
    file.write(reinterpret_cast<const char*>(&H), sizeof(H));
    file.write(reinterpret_cast<const char*>(&L), sizeof(L));
    file.write(reinterpret_cast<const char*>(&SP), sizeof(SP));
    file.write(reinterpret_cast<const char*>(&PC), sizeof(PC));
    
    // Save state flags
    u8 flags = (halted ? 0x01 : 0) | (stopped ? 0x02 : 0) |
               (ime ? 0x04 : 0) | (ime_scheduled ? 0x08 : 0) |
               (halt_bug ? 0x10 : 0) | (stop_speed_switch ? 0x20 : 0);
    file.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
    file.write(reinterpret_cast<const char*>(&cycles), sizeof(cycles));
    file.write(reinterpret_cast<const char*>(&stop_pause), sizeof(stop_pause));
}

void CPU::loadState(std::ifstream& file) {
    // Load all registers
    file.read(reinterpret_cast<char*>(&A), sizeof(A));
    file.read(reinterpret_cast<char*>(&F), sizeof(F));
    file.read(reinterpret_cast<char*>(&B), sizeof(B));
    file.read(reinterpret_cast<char*>(&C), sizeof(C));
    file.read(reinterpret_cast<char*>(&D), sizeof(D));
    file.read(reinterpret_cast<char*>(&E), sizeof(E));
    file.read(reinterpret_cast<char*>(&H), sizeof(H));
    file.read(reinterpret_cast<char*>(&L), sizeof(L));
    file.read(reinterpret_cast<char*>(&SP), sizeof(SP));
    file.read(reinterpret_cast<char*>(&PC), sizeof(PC));

    // Load state flags
    u8 flags = 0;
    file.read(reinterpret_cast<char*>(&flags), sizeof(flags));
    halted = (flags & 0x01) != 0;
    stopped = (flags & 0x02) != 0;
    ime = (flags & 0x04) != 0;
    ime_scheduled = (flags & 0x08) != 0;
    halt_bug = (flags & 0x10) != 0;
    stop_speed_switch = (flags & 0x20) != 0;
    file.read(reinterpret_cast<char*>(&cycles), sizeof(cycles));
    file.read(reinterpret_cast<char*>(&stop_pause), sizeof(stop_pause));
}

